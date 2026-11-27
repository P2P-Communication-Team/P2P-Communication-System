// udp_socket.hpp -- small cross-platform IPv4 UDP socket wrapper (C++17)
//
// The operating-system socket is kept non-blocking internally. This lets the
// wrapper provide predictable non-blocking and timeout-based operations on
// both POSIX systems and Windows without changing socket mode for each call.
//
// Endpoint addresses use HOST byte order, matching p2p::PeerIdentity:
//   ipv4 = 0x7F000001 for 127.0.0.1, port = 9000
// Conversion to/from network byte order is handled inside this class.
//
// Usage:
//   p2p::UdpSocket socket;
//   if (!socket.open() || !socket.bind(9000)) { /* handle error */ }
//   socket.setNonBlocking(true); // default: calls without timeout return at once
//   auto sent = socket.sendTo(data, size, destination);
//   auto received = socket.receiveFrom(buffer, sizeof(buffer), sender,
//                                      std::chrono::milliseconds(500));
//
// A timeout of 0 means do not wait; a positive timeout is a maximum wait;
// a negative timeout means wait indefinitely. Explicit timeout overloads
// always use the supplied timeout, regardless of the default mode.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace p2p {

#ifdef _WIN32
using NativeUdpSocket = SOCKET;
constexpr NativeUdpSocket INVALID_UDP_SOCKET = INVALID_SOCKET;
#else
using NativeUdpSocket = int;
constexpr NativeUdpSocket INVALID_UDP_SOCKET = -1;
#endif

enum class UdpStatus {
    OK,
    WOULD_BLOCK,
    TIMEOUT,
    NOT_OPEN,
    INVALID_ARGUMENT,
    ERROR
};

struct UdpResult {
    UdpStatus status = UdpStatus::ERROR;
    std::size_t bytes = 0;
    int error_code = 0; // WSA error on Windows, errno on POSIX

    explicit operator bool() const { return status == UdpStatus::OK; }
};

// IPv4 address and UDP port are both stored in host byte order.
struct UdpEndpoint {
    std::uint32_t ipv4 = 0; // 0 means INADDR_ANY when used as a local bind address
    std::uint16_t port = 0;
};

class UdpSocket {
public:
    UdpSocket() = default;

    ~UdpSocket() { close(); }

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& other) noexcept
        : socket_(other.socket_), non_blocking_(other.non_blocking_) {
        other.socket_ = INVALID_UDP_SOCKET;
    }

    UdpSocket& operator=(UdpSocket&& other) noexcept {
        if (this != &other) {
            close();
            socket_ = other.socket_;
            non_blocking_ = other.non_blocking_;
            other.socket_ = INVALID_UDP_SOCKET;
        }
        return *this;
    }

    bool open() {
        if (isOpen()) return true;
#ifdef _WIN32
        if (!winsockReady()) return false;
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#else
        socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
#endif
        if (!isOpen()) return false;

        // Keep the native descriptor non-blocking. The wrapper implements
        // optional waiting itself, which avoids blocking past a requested timeout.
#ifdef _WIN32
        u_long enabled = 1;
        if (ioctlsocket(socket_, FIONBIO, &enabled) != 0) {
            close();
            return false;
        }
#else
        const int flags = fcntl(socket_, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
            close();
            return false;
        }
#endif
        return true;
    }

    bool bind(std::uint16_t port, std::uint32_t local_ipv4 = 0) {
        if (!isOpen() && !open()) return false;

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(local_ipv4);

        return ::bind(socket_, reinterpret_cast<const sockaddr*>(&address),
                      static_cast<SocketLength>(sizeof(address))) == 0;
    }

    void close() noexcept {
        if (!isOpen()) return;
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = INVALID_UDP_SOCKET;
    }

    bool isOpen() const noexcept { return socket_ != INVALID_UDP_SOCKET; }
    NativeUdpSocket nativeHandle() const noexcept { return socket_; }

    // Selects the default behavior for overloads without an explicit timeout.
    // true: return immediately if the socket cannot proceed.
    // false: wait indefinitely until the operation can proceed.
    // The native descriptor remains non-blocking in both modes.
    void setNonBlocking(bool enabled) noexcept { non_blocking_ = enabled; }
    bool isNonBlocking() const noexcept { return non_blocking_; }

    UdpResult sendTo(const void* data, std::size_t length,
                     const UdpEndpoint& destination) {
        return sendTo(data, length, destination,
                      non_blocking_ ? std::chrono::milliseconds(0)
                                    : std::chrono::milliseconds(-1));
    }

    UdpResult sendTo(const void* data, std::size_t length,
                     const UdpEndpoint& destination,
                     std::chrono::milliseconds timeout) {
        if (!isOpen()) return {UdpStatus::NOT_OPEN, 0, 0};
        if ((data == nullptr && length != 0) || length > 65507 || destination.port == 0)
            return {UdpStatus::INVALID_ARGUMENT, 0, 0};

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(destination.port);
        address.sin_addr.s_addr = htonl(destination.ipv4);

        const auto start = Clock::now();
        for (;;) {
#ifdef _WIN32
            const int sent = ::sendto(socket_, static_cast<const char*>(data),
                                      static_cast<int>(length), 0,
                                      reinterpret_cast<const sockaddr*>(&address),
                                      static_cast<int>(sizeof(address)));
            if (sent != SOCKET_ERROR) {
                if (static_cast<std::size_t>(sent) != length)
                    return {UdpStatus::ERROR, static_cast<std::size_t>(sent), 0};
                return {UdpStatus::OK, static_cast<std::size_t>(sent), 0};
            }
            const int error = lastSocketError();
#else
            const ssize_t sent = ::sendto(socket_, data, length, 0,
                                          reinterpret_cast<const sockaddr*>(&address),
                                          static_cast<socklen_t>(sizeof(address)));
            if (sent >= 0) {
                if (static_cast<std::size_t>(sent) != length)
                    return {UdpStatus::ERROR, static_cast<std::size_t>(sent), 0};
                return {UdpStatus::OK, static_cast<std::size_t>(sent), 0};
            }
            const int error = lastSocketError();
#endif
            if (isInterrupted(error)) continue;
            if (!isWouldBlock(error)) return {UdpStatus::ERROR, 0, error};

            const WaitResult wait = waitFor(true, remainingTimeout(timeout, start));
            if (wait == WaitResult::TIMEOUT)
                return {timeout.count() == 0 ? UdpStatus::WOULD_BLOCK : UdpStatus::TIMEOUT, 0, error};
            if (wait == WaitResult::ERROR)
                return {UdpStatus::ERROR, 0, lastSocketError()};
        }
    }

    UdpResult receiveFrom(void* buffer, std::size_t capacity, UdpEndpoint& sender) {
        return receiveFrom(buffer, capacity, sender,
                           non_blocking_ ? std::chrono::milliseconds(0)
                                         : std::chrono::milliseconds(-1));
    }

    UdpResult receiveFrom(void* buffer, std::size_t capacity, UdpEndpoint& sender,
                          std::chrono::milliseconds timeout) {
        if (!isOpen()) return {UdpStatus::NOT_OPEN, 0, 0};
        if (buffer == nullptr || capacity == 0)
            return {UdpStatus::INVALID_ARGUMENT, 0, 0};

        const auto start = Clock::now();
        for (;;) {
            const WaitResult wait = waitFor(false, remainingTimeout(timeout, start));
            if (wait == WaitResult::TIMEOUT)
                return {timeout.count() == 0 ? UdpStatus::WOULD_BLOCK : UdpStatus::TIMEOUT, 0, 0};
            if (wait == WaitResult::ERROR)
                return {UdpStatus::ERROR, 0, lastSocketError()};

            sockaddr_in address{};
#ifdef _WIN32
            int address_length = sizeof(address);
            const int received = ::recvfrom(socket_, static_cast<char*>(buffer),
                                            static_cast<int>(capacity), 0,
                                            reinterpret_cast<sockaddr*>(&address),
                                            &address_length);
            if (received != SOCKET_ERROR) {
                sender.ipv4 = ntohl(address.sin_addr.s_addr);
                sender.port = ntohs(address.sin_port);
                return {UdpStatus::OK, static_cast<std::size_t>(received), 0};
            }
            const int error = lastSocketError();
#else
            socklen_t address_length = sizeof(address);
            const ssize_t received = ::recvfrom(socket_, buffer, capacity, 0,
                                                 reinterpret_cast<sockaddr*>(&address),
                                                 &address_length);
            if (received >= 0) {
                sender.ipv4 = ntohl(address.sin_addr.s_addr);
                sender.port = ntohs(address.sin_port);
                return {UdpStatus::OK, static_cast<std::size_t>(received), 0};
            }
            const int error = lastSocketError();
#endif
            if (isInterrupted(error)) continue;
            if (isWouldBlock(error)) {
                if (timeout.count() == 0)
                    return {UdpStatus::WOULD_BLOCK, 0, error};
                continue; // another reader may have consumed readiness; wait again
            }
            return {UdpStatus::ERROR, 0, error};
        }
    }

private:
    using Clock = std::chrono::steady_clock;

#ifdef _WIN32
    using SocketLength = int;
    enum class WaitResult { READY, TIMEOUT, ERROR };

    static bool winsockReady() {
        struct WinsockRuntime {
            bool ready = false;
            WinsockRuntime() {
                WSADATA data{};
                ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }
            ~WinsockRuntime() { if (ready) WSACleanup(); }
        };
        static WinsockRuntime runtime;
        return runtime.ready;
    }

    static int lastSocketError() { return WSAGetLastError(); }
    static bool isInterrupted(int error) { return error == WSAEINTR; }
    static bool isWouldBlock(int error) {
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
    }

    WaitResult waitFor(bool writing, std::chrono::milliseconds timeout) const {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket_, &set);
        timeval tv{};
        timeval* tv_ptr = nullptr;
        if (timeout.count() >= 0) {
            tv.tv_sec = static_cast<long>(timeout.count() / 1000);
            tv.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
            tv_ptr = &tv;
        }
        for (;;) {
            FD_ZERO(&set);
            FD_SET(socket_, &set);
            const int result = writing ? select(0, nullptr, &set, nullptr, tv_ptr)
                                       : select(0, &set, nullptr, nullptr, tv_ptr);
            if (result > 0) return WaitResult::READY;
            if (result == 0) return WaitResult::TIMEOUT;
            if (lastSocketError() != WSAEINTR) return WaitResult::ERROR;
        }
    }
#else
    using SocketLength = socklen_t;
    enum class WaitResult { READY, TIMEOUT, ERROR };

    static int lastSocketError() { return errno; }
    static bool isInterrupted(int error) { return error == EINTR; }
    static bool isWouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK; }

    WaitResult waitFor(bool writing, std::chrono::milliseconds timeout) const {
        pollfd descriptor{};
        descriptor.fd = socket_;
        descriptor.events = writing ? POLLOUT : POLLIN;

        int wait_ms = -1;
        if (timeout.count() >= 0) {
            const auto bounded = timeout.count() > std::numeric_limits<int>::max()
                                     ? std::numeric_limits<int>::max()
                                     : timeout.count();
            wait_ms = static_cast<int>(bounded);
        }

        for (;;) {
            const int result = ::poll(&descriptor, 1, wait_ms);
            if (result > 0) {
                if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))
                    return WaitResult::ERROR;
                return WaitResult::READY;
            }
            if (result == 0) return WaitResult::TIMEOUT;
            if (errno != EINTR) return WaitResult::ERROR;
        }
    }
#endif

    static std::chrono::milliseconds remainingTimeout(
        std::chrono::milliseconds timeout, Clock::time_point start) {
        if (timeout.count() < 0) return std::chrono::milliseconds(-1);
        if (timeout.count() == 0) return std::chrono::milliseconds(0);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
        if (elapsed >= timeout) return std::chrono::milliseconds(0);
        return timeout - elapsed;
    }

    NativeUdpSocket socket_ = INVALID_UDP_SOCKET;
    bool non_blocking_ = true;
};

} // namespace p2p
