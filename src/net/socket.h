#pragma once

// TCP, as MU used, one stream per client (docs/server-plan.md phase 4). A thin, non-blocking
// wrapper over POSIX sockets, which the Mac and the Linux box both have: connect or listen,
// accept, and move bytes in and out of a buffer without ever blocking the frame or the tick.
// Framing is wire.h's.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace mu::net {

class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Socket& operator=(Socket&& other) noexcept;

    // A connection to host:port, blocking for at most `seconds`, then made non-blocking.
    bool connect(const std::string& host, int port, double seconds, std::string& error);
    // A listening socket on every address, non-blocking.
    bool listen(int port, std::string& error);
    // The next waiting connection, or an empty Socket.
    Socket accept();

    // Everything waiting to be read, appended to `into`. False when the far end has gone.
    bool receive(std::vector<uint8_t>& into);
    // As much of `bytes` as the stream takes now; the rest stays in `pending_` and goes on the
    // next flush(). False when the far end has gone.
    bool send(const std::vector<uint8_t>& bytes);
    bool flush();

    bool open() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    void close();

    // What is still to go, and how long it has waited with nothing taken: a far end that stopped
    // reading holds the server's bytes for him forever otherwise (docs/sprints/24-the-quiet-line.md).
    size_t waiting() const { return pending_.size(); }
    double stalledSeconds() const;

private:
    int fd_ = -1;
    std::vector<uint8_t> pending_;
    // When `pending_` last went out in part, or began to wait: kept only while it is not empty.
    std::chrono::steady_clock::time_point moved_{};
};

// Where a datagram came from or goes: an address of either family, as the kernel gave it.
struct Address {
    uint8_t bytes[128] = {};  // a sockaddr_storage
    uint32_t size = 0;
    bool operator==(const Address& other) const;
};

// UDP, beside the stream (docs/sprints/24-the-quiet-line.md): the server's ticks a second way,
// each datagram carrying the last few, so one lost on the stream is not a stall. Non-blocking.
// A datagram is at most kMostDatagram bytes, under any line's MTU.
constexpr size_t kMostDatagram = 1200;
class Datagram {
public:
    Datagram() = default;
    ~Datagram() { close(); }
    Datagram(const Datagram&) = delete;
    Datagram& operator=(const Datagram&) = delete;

    // The server's: bound on every address at `port`.
    bool bind(int port, std::string& error);
    // The client's: aimed at host:port.
    bool aim(const std::string& host, int port, std::string& error);
    // One datagram out: to `to`, or where aim() pointed. False only for a socket that is gone;
    // a datagram the kernel would not take is simply lost, as a datagram may be.
    bool send(const uint8_t* bytes, size_t size, const Address* to = nullptr);
    // The next waiting datagram into `into`, and who sent it; false when none waits.
    bool receive(std::vector<uint8_t>& into, Address* from = nullptr);

    bool open() const { return fd_ >= 0; }
    int fd() const { return fd_; }
    void close();

private:
    int fd_ = -1;
};

}  // namespace mu::net
