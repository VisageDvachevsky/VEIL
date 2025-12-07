#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace veil::transport {

// Socket address
struct SocketAddress {
    std::string host;
    uint16_t port{0};

    bool operator==(const SocketAddress& other) const {
        return host == other.host && port == other.port;
    }
};

// UDP socket configuration
struct UdpSocketConfig {
    SocketAddress bind_address;        // Address to bind to
    bool reuse_port = true;            // SO_REUSEPORT
    bool nonblocking = true;           // Non-blocking mode
    size_t recv_buffer_size = 1048576; // Receive buffer size (1MB)
    size_t send_buffer_size = 1048576; // Send buffer size (1MB)
};

// Received packet
struct ReceivedPacket {
    SocketAddress from;
    std::vector<uint8_t> data;
};

// UDP socket wrapper with epoll support
class UdpSocket {
public:
    using RecvCallback = std::function<void(ReceivedPacket packet)>;
    using ErrorCallback = std::function<void(int error_code, const std::string& message)>;

    UdpSocket() = default;
    ~UdpSocket();

    // Disable copy
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Enable move
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // Open and bind socket
    bool open(const UdpSocketConfig& config);

    // Close socket
    void close();

    // Check if socket is open
    [[nodiscard]] bool is_open() const { return fd_ >= 0; }

    // Get socket file descriptor
    [[nodiscard]] int fd() const { return fd_; }

    // Set callbacks
    void set_recv_callback(RecvCallback callback);
    void set_error_callback(ErrorCallback callback);

    // Send data to address
    bool send_to(const SocketAddress& to, std::span<const uint8_t> data);

    // Send multiple packets (sendmmsg if available)
    size_t send_many(const std::vector<std::pair<SocketAddress, std::vector<uint8_t>>>& packets);

    // Receive single packet (non-blocking)
    std::optional<ReceivedPacket> recv();

    // Receive multiple packets (recvmmsg if available)
    std::vector<ReceivedPacket> recv_many(size_t max_packets);

    // Poll for events (blocking with timeout)
    // Returns number of packets received, -1 on error
    int poll_recv(int timeout_ms);

    // Run event loop (calls recv_callback for each packet)
    void run_once(int timeout_ms);

    // Get bound address
    [[nodiscard]] const SocketAddress& local_address() const { return local_addr_; }

    // Statistics
    [[nodiscard]] uint64_t packets_sent() const { return packets_sent_; }
    [[nodiscard]] uint64_t packets_received() const { return packets_received_; }
    [[nodiscard]] uint64_t bytes_sent() const { return bytes_sent_; }
    [[nodiscard]] uint64_t bytes_received() const { return bytes_received_; }
    [[nodiscard]] uint64_t send_errors() const { return send_errors_; }
    [[nodiscard]] uint64_t recv_errors() const { return recv_errors_; }

private:
    int fd_{-1};
    int epoll_fd_{-1};
    SocketAddress local_addr_;
    UdpSocketConfig config_;

    RecvCallback recv_callback_;
    ErrorCallback error_callback_;

    // Statistics
    uint64_t packets_sent_{0};
    uint64_t packets_received_{0};
    uint64_t bytes_sent_{0};
    uint64_t bytes_received_{0};
    uint64_t send_errors_{0};
    uint64_t recv_errors_{0};

    // Internal helpers
    void handle_error(int error_code, const char* context);
};

}  // namespace veil::transport
