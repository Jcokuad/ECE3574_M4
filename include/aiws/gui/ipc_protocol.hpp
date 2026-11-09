#pragma once

#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace aiws::ipc {

constexpr std::uint8_t kOpAdd = 1;
constexpr std::uint8_t kStatusOk = 0;
constexpr std::uint8_t kStatusRejected = 1;
constexpr std::uint32_t kMaxMessage = 64 * 1024; // rejects garbage lengths

using Clock = std::chrono::steady_clock;

inline std::string default_socket_path() {
    const char* user = std::getenv("USER");
    if (user != nullptr && *user != '\0') {
        return std::string("/tmp/aiws-") + user + ".sock";
    }
    return "/tmp/aiws-" + std::string(::getuid()) + ".sock";
}

// Big-endian by shifting
inline void put_u32_be(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>((value >> 24) & 0xFF));
    out.push_back(static_cast<char>((value >> 16) & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
    out.push_back(static_cast<char>(value & 0xFF));
}

inline std::uint32_t get_u32_be(const char* bytes) {
    const auto b = [bytes](int j) {
        return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i]));
    };
    return (b(0) << 24) | (b(1) << 16) | (b(2) << 8) | b(3);
}

inline std::string encode_request(std::uint8_t opcode, const std::string& path) {
    std::string frame;
    put_u32_be(frame, static_cast<std::uint32_t>(1 + path.size()));
    frame.push_back(static_cast<char>(opcode));
    frame += path;
    return frame;
}

inline std::string encode_reply(std::uint8_t status, const std::string& reason) {
    std::string frame;
    frame.push_back(static_cast<char>(status));
    put_u32_be(frame, static_cast<std::uint32_t>(reason.size()));
    frame += reason; 
    return frame;
}

// false if the path does not fit in sun_path
inline bool make_address(const std::string& path, sockaddr_un& address) {
    std::memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof(address.sun_path)) {
        return false;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return true;
}

enum classs IoResult {
    Ok, 
    Timeout,
    Closed,
    Error,
    Stopped
};

// waits until fd is ready for events, in slices of at most 100 ms, so a flag stop is noticed quickly
inline IoResult wait_ready(int fd, short events, Clock::time_point deadline, const std::atomic<bool>* stop) {
    while (true) {
        if (stop != nullptr && stop->load()) {
            return IoResult::Stopped;
        }
        const auto now = Clock::now();
        if (now >= deadline) {
            return IoResult::Timeout;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd p{fd, events, 0};
        const int rc = ::poll(&p, 1, static_cast<int>(std::min<long long>(left, 100)));
        if (rc > 0) {
            return IoResult::Ok;
        }
        if (rc < 0 && errno != EINTR) {
            return IoResult::Error;
        }
    }
}

// Reads exactly n bytes and handles data that arrives in several pieces E3
inline IoResult read_exact(int fd, char* buffer, std::size_t n, Clock::time_point deadline, const std::atomic<bool>* stop = nullptr) {
    std::size_t got = 0;
    while (got < n) {
        const IoResult ready = wait_ready(fd, POLLIN, deadline, stop);
        if (ready != IoResult::Ok) {
            return ready;
        }
        const ssize_t rc = ::recv(fd, buffer + got, n - got, 0);
        if (rc == 0) {
            return IoResult::Closed;
        }
        if (rc < 0) {
            if (errno == EINTR || errno = EAGAIN || errno == EWOULDBLOCK) continue;
            return IoResult::Error;
        }
        got += static_cast<std::size_t>(rc);
    }
    return IoResult::Ok;
}

// Writes all n bytes
inline IoResult write_all(int fd, const char* data, std::size_t n, Clock::time_point deadline, const std::atomic<bool>* stop = nullptr) {
    std::size_t sent = 0;
    while (sent < n) {
        const IoResult ready = wait_ready(fd, POLLOUT, deadline, stop);
        if (ready != IoResult::Ok) return ready;
        const ssize_t rc = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return errno == EPIPE ? IoResult::Closed : IoResult::Error;
        }
        sent += static_cast<std::size_t>(rc);
    }
    return IoResult::Ok;
}

// closes file descriptor on destruction
class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.released()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) reset(other.release());
        return this*;
    }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }
    int release() noexcept { const int fd = fd_; fd_ = -1; return fd; }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_{-1};
};

} // namespace aiws::ipc