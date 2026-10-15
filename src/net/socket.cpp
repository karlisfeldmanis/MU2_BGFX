#include "net/socket.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
// Winsock (docs/plans/windows-client.md). A SOCKET is a handle, not an fd, but Windows keeps
// them small, and the class has held it in an int since the start. Its errors come from
// WSAGetLastError, never errno: socketError() below reads the right one, and the names it is
// compared with are Winsock's own.
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mu::net {

namespace {

#ifdef _WIN32
// Winsock has to be started once before the first socket; it is never stopped (the process's
// end does that).
const bool g_started = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}();
int socketError() { return WSAGetLastError(); }
bool wouldBlock(int e) { return e == WSAEWOULDBLOCK; }
bool connecting(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
bool interrupted(int e) { return e == WSAEINTR; }
std::string describe(int e) {
    char text[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, DWORD(e), 0,
                   text, sizeof text, nullptr);
    std::string said = text;
    while (!said.empty() && (said.back() == '\n' || said.back() == '\r' || said.back() == '.'))
        said.pop_back();
    return said.empty() ? "winsock error " + std::to_string(e) : said;
}
void closeFd(int fd) { ::closesocket(SOCKET(fd)); }
int pollOne(int fd, short events, int ms) {
    WSAPOLLFD wait{SOCKET(fd), events, 0};
    return WSAPoll(&wait, 1, ms);
}
// Whether a connect under way has finished, either way, within `ms`. select, not WSAPoll:
// before Windows 10 2004 WSAPoll never reported a refused connect, and a server that was down
// waited out the whole timeout. A failed connect is in the except set on every Windows.
bool connectDone(int fd, int ms) {
    fd_set wrote, failed;
    FD_ZERO(&wrote);
    FD_ZERO(&failed);
    FD_SET(SOCKET(fd), &wrote);
    FD_SET(SOCKET(fd), &failed);
    timeval wait{ms / 1000, (ms % 1000) * 1000};
    return select(0, nullptr, &wrote, &failed, &wait) > 0;
}
bool nonBlocking(int fd) {
    u_long on = 1;
    return ioctlsocket(SOCKET(fd), FIONBIO, &on) == 0;
}
// A socket() that failed is INVALID_SOCKET, which as an int is -1, as on the Mac.
int openSocket(int family, int type, int protocol) { return int(::socket(family, type, protocol)); }
#else
int socketError() { return errno; }
bool wouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool connecting(int e) { return e == EINPROGRESS; }
bool interrupted(int e) { return e == EINTR; }
std::string describe(int e) { return std::strerror(e); }
void closeFd(int fd) { ::close(fd); }
int pollOne(int fd, short events, int ms) {
    pollfd wait{fd, events, 0};
    return poll(&wait, 1, ms);
}
bool connectDone(int fd, int ms) { return pollOne(fd, POLLOUT, ms) == 1; }
bool nonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
int openSocket(int family, int type, int protocol) { return ::socket(family, type, protocol); }
#endif

// Winsock's calls take char* where POSIX's take void*; this one cast serves both.
template <typename T> char* bytesOf(T* p) { return reinterpret_cast<char*>(p); }
template <typename T> const char* bytesOf(const T* p) { return reinterpret_cast<const char*>(p); }

// A tick's message is small and goes at once: no Nagle wait on it.
void noDelay(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, bytesOf(&one), sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, bytesOf(&one), sizeof one);
#endif
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

std::atomic<bool> g_abandoned{false};

}  // namespace

void abandonWaits() { g_abandoned = true; }
bool waitsAbandoned() { return g_abandoned.load(); }

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        pending_ = std::move(other.pending_);
        moved_ = other.moved_;
        other.fd_ = -1;
    }
    return *this;
}

void Socket::close() {
    if (fd_ >= 0) closeFd(fd_);
    fd_ = -1;
    pending_.clear();
}

bool Socket::connect(const std::string& host, int port, double seconds, std::string& error) {
    close();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (const int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &found); status != 0) {
        error = std::string("cannot resolve ") + host + ": " + gai_strerror(status);
        return false;
    }
    for (addrinfo* one = found; one != nullptr && fd_ < 0 && !waitsAbandoned(); one = one->ai_next) {
        const int fd = openSocket(one->ai_family, one->ai_socktype, one->ai_protocol);
        if (fd < 0) continue;
        nonBlocking(fd);
        int status = ::connect(fd, one->ai_addr, int(one->ai_addrlen));
        int problem = status != 0 ? socketError() : 0;
        std::string why;
        if (status != 0 && connecting(problem)) {
            // A tenth of a second at a time, so a window closed while it waits is not held
            // for the rest of `seconds` (abandonWaits).
            const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
            bool done = false;
            while (!done && !waitsAbandoned() && std::chrono::steady_clock::now() < until) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                    until - std::chrono::steady_clock::now()).count();
                done = connectDone(fd, int(std::clamp<long long>(left, 0, 100)));
            }
            if (done) {
                problem = 0;
                socklen_t size = sizeof problem;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, bytesOf(&problem), &size);
                status = problem == 0 ? 0 : -1;
            } else {
                why = waitsAbandoned() ? "given up, the window closed" : "timed out";
            }
        }
        if (status == 0) {
            fd_ = fd;
            noDelay(fd);
        } else {
            if (why.empty()) why = describe(problem);
            error = std::string("cannot reach ") + host + ":" + service + ": " + why;
            closeFd(fd);
        }
    }
    freeaddrinfo(found);
    return fd_ >= 0;
}

bool Socket::listen(int port, std::string& error) {
    close();
    const int fd = openSocket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + describe(socketError());
        return false;
    }
    int one = 1, zero = 0;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, bytesOf(&one), sizeof one);
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, bytesOf(&zero), sizeof zero);  // IPv4 too
    sockaddr_in6 at{};
    at.sin6_family = AF_INET6;
    at.sin6_addr = in6addr_any;
    at.sin6_port = htons(uint16_t(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0 || ::listen(fd, 16) != 0) {
        error = std::string("cannot listen on ") + std::to_string(port) + ": " + describe(socketError());
        closeFd(fd);
        return false;
    }
    nonBlocking(fd);
    fd_ = fd;
    return true;
}

Socket Socket::accept() {
    if (fd_ < 0) return Socket();
    const int fd = int(::accept(fd_, nullptr, nullptr));
    if (fd < 0) return Socket();
    nonBlocking(fd);
    noDelay(fd);
    return Socket(fd);
}

bool Socket::receive(std::vector<uint8_t>& into) {
    if (fd_ < 0) return false;
    uint8_t chunk[16384];
    while (true) {
        const ssize_t got = ::recv(fd_, bytesOf(chunk), sizeof chunk, 0);
        if (got > 0) {
            into.insert(into.end(), chunk, chunk + got);
            continue;
        }
        if (got == 0) return false;  // the far end closed
        const int e = socketError();
        if (wouldBlock(e)) return true;
        if (interrupted(e)) continue;
        return false;
    }
}

bool Socket::send(const std::vector<uint8_t>& bytes) {
    // Fast path: nothing pending, try to push straight into the kernel without copying into
    // pending_ first. The common case for a command -- a few dozen bytes, the socket ready --
    // saves a vector append and its memcpy.
    if (pending_.empty() && fd_ >= 0) {
        const ssize_t put = ::send(fd_, bytesOf(bytes.data()), int(bytes.size()), kSendFlags);
        if (put == ssize_t(bytes.size())) return true;
        if (put > 0) {
            pending_.insert(pending_.end(), bytes.begin() + put, bytes.end());
            moved_ = std::chrono::steady_clock::now();
            return true;
        }
        const int e = put < 0 ? socketError() : 0;
        if (put < 0 && wouldBlock(e)) {
            pending_.insert(pending_.end(), bytes.begin(), bytes.end());
            moved_ = std::chrono::steady_clock::now();
            return true;
        }
        if (put < 0 && interrupted(e)) {
            // Fall through to the old path.
        } else {
            return false;
        }
    }
    if (pending_.empty()) moved_ = std::chrono::steady_clock::now();
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    return flush();
}

void Socket::queue(const std::vector<uint8_t>& bytes) {
    if (pending_.empty()) moved_ = std::chrono::steady_clock::now();
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
}

bool Socket::flush() {
    if (fd_ < 0) return false;
    size_t sent = 0;
    while (sent < pending_.size()) {
        const ssize_t put =
            ::send(fd_, bytesOf(pending_.data() + sent), int(pending_.size() - sent), kSendFlags);
        if (put > 0) {
            sent += size_t(put);
            continue;
        }
        const int e = put < 0 ? socketError() : 0;
        if (put < 0 && wouldBlock(e)) break;
        if (put < 0 && interrupted(e)) continue;
        return false;
    }
    pending_.erase(pending_.begin(), pending_.begin() + long(sent));
    if (sent > 0) moved_ = std::chrono::steady_clock::now();
    return true;
}

double Socket::stalledSeconds() const {
    if (pending_.empty()) return 0.0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - moved_).count();
}

bool Address::operator==(const Address& other) const {
    return size == other.size && std::memcmp(bytes, other.bytes, size) == 0;
}

void Datagram::close() {
    if (fd_ >= 0) closeFd(fd_);
    fd_ = -1;
}

bool Datagram::bind(int port, std::string& error) {
    close();
    const int fd = openSocket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0) {
        error = std::string("udp socket: ") + describe(socketError());
        return false;
    }
    int zero = 0;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, bytesOf(&zero), sizeof zero);  // IPv4 too
    sockaddr_in6 at{};
    at.sin6_family = AF_INET6;
    at.sin6_addr = in6addr_any;
    at.sin6_port = htons(uint16_t(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0) {
        error = std::string("cannot bind udp ") + std::to_string(port) + ": " + describe(socketError());
        closeFd(fd);
        return false;
    }
    nonBlocking(fd);
    fd_ = fd;
    return true;
}

bool Datagram::aim(const std::string& host, int port, std::string& error) {
    close();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (const int status = getaddrinfo(host.c_str(), service.c_str(), &hints, &found); status != 0) {
        error = std::string("cannot resolve ") + host + ": " + gai_strerror(status);
        return false;
    }
    for (addrinfo* one = found; one != nullptr && fd_ < 0; one = one->ai_next) {
        const int fd = openSocket(one->ai_family, one->ai_socktype, one->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, one->ai_addr, int(one->ai_addrlen)) == 0) {
            nonBlocking(fd);
            fd_ = fd;
        } else {
            closeFd(fd);
        }
    }
    freeaddrinfo(found);
    if (fd_ < 0) error = "no udp route to " + host + ":" + service;
    return fd_ >= 0;
}

bool Datagram::send(const uint8_t* bytes, size_t size, const Address* to) {
    if (fd_ < 0) return false;
    const ssize_t put = to != nullptr
        ? ::sendto(fd_, bytesOf(bytes), int(size), kSendFlags, reinterpret_cast<const sockaddr*>(to->bytes), socklen_t(to->size))
        : ::send(fd_, bytesOf(bytes), int(size), kSendFlags);
    (void)put;
    return true;
}

bool Datagram::receive(std::vector<uint8_t>& into, Address* from) {
    if (fd_ < 0) return false;
    uint8_t chunk[2048];
    sockaddr_storage at{};
    socklen_t size = sizeof at;
    while (true) {
        const ssize_t got = ::recvfrom(fd_, bytesOf(chunk), sizeof chunk, 0, reinterpret_cast<sockaddr*>(&at), &size);
        if (got < 0 && interrupted(socketError())) continue;
        if (got < 0) return false;
        into.assign(chunk, chunk + got);
        if (from != nullptr) {
            from->size = std::min<uint32_t>(uint32_t(size), sizeof from->bytes);
            std::memcpy(from->bytes, &at, from->size);
        }
        return true;
    }
}

}  // namespace mu::net
