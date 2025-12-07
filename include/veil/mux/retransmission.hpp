#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace veil::mux {

// Retransmission configuration
struct RetransmissionConfig {
    uint64_t initial_rto_ms = 200;      // Initial retransmission timeout
    uint64_t min_rto_ms = 100;          // Minimum RTO
    uint64_t max_rto_ms = 10000;        // Maximum RTO (10 seconds)
    uint32_t max_retries = 5;           // Max retransmission attempts
    size_t max_unacked_packets = 1024;  // Max packets in flight
    size_t max_unacked_bytes = 1048576; // Max bytes in flight (1MB)
    double rtt_alpha = 0.125;           // RTT smoothing factor
    double rtt_beta = 0.25;             // RTT variance factor
};

// Retransmission manager for reliable delivery
class RetransmissionManager {
public:
    using RetransmitCallback = std::function<void(uint64_t seq, const std::vector<uint8_t>& data)>;
    using DropCallback = std::function<void(uint64_t seq)>;

    explicit RetransmissionManager(const RetransmissionConfig& config = {});

    // Set callbacks
    void set_retransmit_callback(RetransmitCallback callback);
    void set_drop_callback(DropCallback callback);

    // Register a sent packet (stores for potential retransmission)
    bool register_packet(uint64_t seq, std::vector<uint8_t> data, uint64_t send_time_ms);

    // Process an ACK for a sequence number
    // Updates RTT estimate and removes from unacked
    void ack_packet(uint64_t seq, uint64_t ack_time_ms);

    // Process a SACK bitmap
    void process_sack(uint64_t ack_number, uint64_t bitmap, uint64_t ack_time_ms);

    // Check for packets that need retransmission
    // Returns sequence numbers to retransmit
    std::vector<uint64_t> check_timeouts(uint64_t current_time_ms);

    // Trigger retransmission of timed-out packets
    size_t retransmit_expired(uint64_t current_time_ms);

    // Get current RTT estimate (smoothed)
    [[nodiscard]] uint64_t get_srtt_ms() const { return srtt_ms_; }

    // Get current RTO
    [[nodiscard]] uint64_t get_rto_ms() const { return rto_ms_; }

    // Get statistics
    [[nodiscard]] size_t unacked_count() const { return unacked_.size(); }
    [[nodiscard]] size_t unacked_bytes() const { return unacked_bytes_; }
    [[nodiscard]] uint64_t total_retransmits() const { return total_retransmits_; }
    [[nodiscard]] uint64_t total_drops() const { return total_drops_; }

    // Check if we can send more (congestion control)
    [[nodiscard]] bool can_send(size_t bytes) const;

    // Reset state
    void reset();

private:
    struct UnackedPacket {
        std::vector<uint8_t> data;
        uint64_t send_time_ms;
        uint64_t last_sent_ms;
        uint32_t retransmit_count{0};
    };

    RetransmissionConfig config_;
    std::map<uint64_t, UnackedPacket> unacked_;
    size_t unacked_bytes_{0};

    // RTT estimation
    uint64_t srtt_ms_{0};        // Smoothed RTT
    uint64_t rttvar_ms_{0};      // RTT variance
    uint64_t rto_ms_;            // Retransmission timeout
    bool rtt_initialized_{false};

    // Statistics
    uint64_t total_retransmits_{0};
    uint64_t total_drops_{0};

    // Callbacks
    RetransmitCallback retransmit_callback_;
    DropCallback drop_callback_;

    // Update RTT estimate with new sample
    void update_rtt(uint64_t rtt_sample_ms);
};

}  // namespace veil::mux
