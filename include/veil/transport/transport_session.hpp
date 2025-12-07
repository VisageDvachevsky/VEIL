#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "udp_socket.hpp"
#include "veil/crypto/crypto.hpp"
#include "veil/crypto/hkdf.hpp"
#include "veil/handshake/handshake.hpp"
#include "veil/mux/ack_bitmap.hpp"
#include "veil/mux/fragment_assembler.hpp"
#include "veil/mux/rate_limiter.hpp"
#include "veil/mux/reorder_buffer.hpp"
#include "veil/mux/replay_window.hpp"
#include "veil/mux/retransmission.hpp"
#include "veil/mux/session_rotator.hpp"
#include "veil/packet/packet_builder.hpp"
#include "veil/packet/packet_parser.hpp"

namespace veil::transport {

// Transport session configuration
struct TransportSessionConfig {
    SocketAddress local_address;           // Local bind address
    SocketAddress peer_address;            // Remote peer address
    handshake::PSK psk{};                  // Pre-shared key
    size_t mtu = 1400;                     // Maximum transmission unit
    mux::RateLimiterConfig rate_limiter;
    mux::ReorderBufferConfig reorder;
    mux::FragmentAssemblerConfig fragment;
    mux::RetransmissionConfig retransmission;
    mux::SessionRotatorConfig session_rotator;
};

// Transport session state
enum class SessionState {
    DISCONNECTED,
    HANDSHAKING,
    CONNECTED,
    CLOSING,
    CLOSED
};

// Transport statistics
struct TransportStats {
    uint64_t packets_sent{0};
    uint64_t packets_received{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint64_t packets_dropped_rate_limit{0};
    uint64_t packets_dropped_replay{0};
    uint64_t packets_retransmitted{0};
    uint64_t messages_fragmented{0};
    uint64_t messages_assembled{0};
    uint64_t session_rotations{0};
    uint64_t handshake_failures{0};
    uint64_t decryption_failures{0};
};

// Transport session handler
class TransportSession {
public:
    using DataCallback = std::function<void(std::vector<uint8_t> data)>;
    using StateCallback = std::function<void(SessionState state)>;
    using ErrorCallback = std::function<void(const std::string& error)>;

    explicit TransportSession(const TransportSessionConfig& config);
    ~TransportSession();

    // Disable copy
    TransportSession(const TransportSession&) = delete;
    TransportSession& operator=(const TransportSession&) = delete;

    // Set callbacks
    void set_data_callback(DataCallback callback);
    void set_state_callback(StateCallback callback);
    void set_error_callback(ErrorCallback callback);

    // Start the session (opens socket, begins handshake if peer set)
    bool start();

    // Stop the session
    void stop();

    // Send application data
    // May fragment if larger than MTU
    bool send(std::span<const uint8_t> data);

    // Send control message (ping/pong)
    bool send_ping();
    bool send_pong(uint64_t echo_timestamp);

    // Process events (call periodically)
    // Returns true if any events were processed
    bool process(int timeout_ms = 0);

    // Get current state
    [[nodiscard]] SessionState state() const { return state_; }

    // Get statistics
    [[nodiscard]] const TransportStats& stats() const { return stats_; }

    // Get RTT estimate (ms)
    [[nodiscard]] uint64_t rtt_ms() const;

    // Check if connected
    [[nodiscard]] bool is_connected() const { return state_ == SessionState::CONNECTED; }

private:
    TransportSessionConfig config_;
    SessionState state_{SessionState::DISCONNECTED};

    // Components
    UdpSocket socket_;
    std::unique_ptr<handshake::Handshake> handshake_;
    packet::PacketBuilder packet_builder_;
    packet::PacketParser packet_parser_;
    mux::ReplayWindow replay_window_;
    mux::RateLimiter rate_limiter_;
    mux::AckBitmap ack_bitmap_;
    mux::ReorderBuffer reorder_buffer_;
    mux::FragmentAssembler fragment_assembler_;
    mux::RetransmissionManager retransmission_;
    mux::SessionRotator session_rotator_;

    // Session state
    uint64_t send_sequence_{1};
    uint64_t last_ack_sent_{0};
    uint32_t next_message_id_{1};
    crypto::SessionKeys session_keys_{};

    // Statistics
    TransportStats stats_{};

    // Callbacks
    DataCallback data_callback_;
    StateCallback state_callback_;
    ErrorCallback error_callback_;

    // Internal methods
    void set_state(SessionState new_state);
    void handle_received_packet(ReceivedPacket packet);
    void handle_frame(const packet::Frame& frame);
    void handle_data_frame(const packet::DataFrame& frame);
    void handle_ack_frame(const packet::AckFrame& frame);
    void handle_control_frame(const packet::ControlFrame& frame);
    void handle_fragment_frame(const packet::FragmentFrame& frame);
    void handle_handshake_frame(const packet::HandshakeFrame& frame);

    void send_ack();
    void process_timeouts();
    bool send_packet_internal(const packet::Frame& frame);
    std::vector<packet::FragmentFrame> fragment_data(std::span<const uint8_t> data);

    uint64_t get_time_ms();
};

}  // namespace veil::transport
