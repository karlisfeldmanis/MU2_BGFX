#pragma once

// TCP, as MU used, one stream per client (docs/server-plan.md phase 4). A thin, non-blocking
// wrapper over POSIX sockets, which the Mac and the Linux box both have: connect or listen,
// accept, and move bytes in and out of a buffer without ever blocking the frame or the tick.
// Framing is wire.h's.

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

private:
    int fd_ = -1;
    std::vector<uint8_t> pending_;
};

}  // namespace mu::net
