#include "net/socket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mu::net {

namespace {

bool nonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// A tick's message is small and goes at once: no Nagle wait on it.
void noDelay(int fd) {
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

}  // namespace

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        pending_ = std::move(other.pending_);
        other.fd_ = -1;
    }
    return *this;
}

void Socket::close() {
    if (fd_ >= 0) ::close(fd_);
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
    for (addrinfo* one = found; one != nullptr && fd_ < 0; one = one->ai_next) {
        const int fd = ::socket(one->ai_family, one->ai_socktype, one->ai_protocol);
        if (fd < 0) continue;
        nonBlocking(fd);
        int status = ::connect(fd, one->ai_addr, one->ai_addrlen);
        if (status != 0 && errno == EINPROGRESS) {
            pollfd wait{fd, POLLOUT, 0};
            if (poll(&wait, 1, int(seconds * 1000.0)) == 1) {
                int problem = 0;
                socklen_t size = sizeof problem;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &problem, &size);
                status = problem == 0 ? 0 : -1;
                if (problem != 0) errno = problem;
            } else {
                errno = ETIMEDOUT;
            }
        }
        if (status == 0) {
            fd_ = fd;
            noDelay(fd);
        } else {
            error = std::string("cannot reach ") + host + ":" + service + ": " + std::strerror(errno);
            ::close(fd);
        }
    }
    freeaddrinfo(found);
    return fd_ >= 0;
}

bool Socket::listen(int port, std::string& error) {
    close();
    const int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        error = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    int one = 1, zero = 0;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof zero);  // IPv4 too
    sockaddr_in6 at{};
    at.sin6_family = AF_INET6;
    at.sin6_addr = in6addr_any;
    at.sin6_port = htons(uint16_t(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&at), sizeof at) != 0 || ::listen(fd, 16) != 0) {
        error = std::string("cannot listen on ") + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        return false;
    }
    nonBlocking(fd);
    fd_ = fd;
    return true;
}

Socket Socket::accept() {
    if (fd_ < 0) return Socket();
    const int fd = ::accept(fd_, nullptr, nullptr);
    if (fd < 0) return Socket();
    nonBlocking(fd);
    noDelay(fd);
    return Socket(fd);
}

bool Socket::receive(std::vector<uint8_t>& into) {
    if (fd_ < 0) return false;
    uint8_t chunk[16384];
    while (true) {
        const ssize_t got = ::recv(fd_, chunk, sizeof chunk, 0);
        if (got > 0) {
            into.insert(into.end(), chunk, chunk + got);
            continue;
        }
        if (got == 0) return false;  // the far end closed
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        if (errno == EINTR) continue;
        return false;
    }
}

bool Socket::send(const std::vector<uint8_t>& bytes) {
    pending_.insert(pending_.end(), bytes.begin(), bytes.end());
    return flush();
}

bool Socket::flush() {
    if (fd_ < 0) return false;
    size_t sent = 0;
    while (sent < pending_.size()) {
        const ssize_t put = ::send(fd_, pending_.data() + sent, pending_.size() - sent, kSendFlags);
        if (put > 0) {
            sent += size_t(put);
            continue;
        }
        if (put < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (put < 0 && errno == EINTR) continue;
        return false;
    }
    pending_.erase(pending_.begin(), pending_.begin() + long(sent));
    return true;
}

}  // namespace mu::net
