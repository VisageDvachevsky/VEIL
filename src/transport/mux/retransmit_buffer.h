#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace veil::mux {

// Configuration for retransmission behavior.
struct RetransmitConfig {
  // Initial RTT estimate in milliseconds.
  std::chrono::milliseconds initial_rtt{100};
  // Minimum RTO (retransmit timeout).
  std::chrono::milliseconds min_rto{50};
  // Maximum RTO.
  std::chrono::milliseconds max_rto{10000};
  // Maximum number of retransmit attempts before giving up.
  std::uint32_t max_retries{5};
  // Maximum bytes buffered for retransmission.
  std::size_t max_buffer_bytes{1 << 20};  // 1 MB
  // Exponential backoff factor (multiplied on each retry).
  double backoff_factor{2.0};
  // RTT smoothing factor (alpha for EWMA).
  double rtt_alpha{0.125};
  // RTT variance factor (beta for EWMA).
  double rtt_beta{0.25};
};

// Entry representing a packet awaiting acknowledgment.
struct PendingPacket {
  std::uint64_t sequence{0};
  std::vector<std::uint8_t> data;
  std::chrono::steady_clock::time_point first_sent;
  std::chrono::steady_clock::time_point last_sent;
  std::chrono::steady_clock::time_point next_retry;
  std::uint32_t retry_count{0};
};

// Statistics for observability.
struct RetransmitStats {
  std::uint64_t packets_sent{0};
  std::uint64_t packets_acked{0};
  std::uint64_t packets_retransmitted{0};
  std::uint64_t packets_dropped{0};
  std::uint64_t bytes_sent{0};
  std::uint64_t bytes_retransmitted{0};
};

// Manages a buffer of unacknowledged packets with RTT estimation and retransmission.
class RetransmitBuffer {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Duration = Clock::duration;

  explicit RetransmitBuffer(RetransmitConfig config = {},
                            std::function<TimePoint()> now_fn = Clock::now);

  // Insert a newly sent packet into the buffer.
  // Returns false if buffer is full (exceeds max_buffer_bytes).
  bool insert(std::uint64_t sequence, std::vector<std::uint8_t> data);

  // Acknowledge a packet. Updates RTT estimate and removes from buffer.
  // Returns true if the sequence was found and acknowledged.
  bool acknowledge(std::uint64_t sequence);

  // Acknowledge all packets up to and including sequence (cumulative ACK).
  void acknowledge_cumulative(std::uint64_t sequence);

  // Get packets that need retransmission now.
  // Returns references to packets whose next_retry has passed.
  std::vector<const PendingPacket*> get_packets_to_retransmit();

  // Mark a packet as retransmitted (updates retry count and next_retry time).
  // Returns false if max retries exceeded (packet should be dropped).
  bool mark_retransmitted(std::uint64_t sequence);

  // Remove a packet that has exceeded max retries.
  void drop_packet(std::uint64_t sequence);

  // Get current RTT estimate.
  std::chrono::milliseconds estimated_rtt() const { return estimated_rtt_; }

  // Get current RTO (retransmit timeout).
  std::chrono::milliseconds current_rto() const { return current_rto_; }

  // Get current buffer utilization.
  std::size_t buffered_bytes() const { return buffered_bytes_; }
  std::size_t pending_count() const { return pending_.size(); }

  // Get statistics.
  const RetransmitStats& stats() const { return stats_; }

  // Check if buffer has capacity for more data.
  bool has_capacity(std::size_t bytes) const {
    return buffered_bytes_ + bytes <= config_.max_buffer_bytes;
  }

 private:
  void update_rtt(std::chrono::milliseconds sample);
  std::chrono::milliseconds calculate_rto() const;

  RetransmitConfig config_;
  std::function<TimePoint()> now_fn_;

  std::map<std::uint64_t, PendingPacket> pending_;
  std::size_t buffered_bytes_{0};

  // RTT estimation (RFC 6298 style)
  std::chrono::milliseconds estimated_rtt_;
  std::chrono::milliseconds rtt_variance_{0};
  std::chrono::milliseconds current_rto_;
  bool rtt_initialized_{false};

  RetransmitStats stats_;
};

}  // namespace veil::mux
