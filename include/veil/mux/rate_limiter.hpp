#pragma once

#include <cstdint>
#include <chrono>

namespace veil::mux {

// Token bucket rate limiter configuration
struct RateLimiterConfig {
    uint64_t packets_per_second = 10000;     // Max packets per second
    uint64_t bytes_per_second = 100000000;   // Max bytes per second (100 MB/s)
    uint64_t burst_packets = 100;            // Max burst size in packets
    uint64_t burst_bytes = 1000000;          // Max burst size in bytes (1 MB)
};

// Token bucket rate limiter
class RateLimiter {
public:
    explicit RateLimiter(const RateLimiterConfig& config = {});

    // Check if a packet can be sent/received
    // Returns true if allowed, false if rate limited
    [[nodiscard]] bool check(size_t packet_bytes) const;

    // Consume tokens for a packet
    // Should only be called after check() returns true
    void consume(size_t packet_bytes);

    // Check and consume in one call
    bool try_consume(size_t packet_bytes);

    // Update token buckets with elapsed time
    // Call this periodically (e.g., before processing packets)
    void refill(uint64_t elapsed_ms);

    // Update with current time (convenience method)
    void refill_now();

    // Get current token counts
    [[nodiscard]] uint64_t packet_tokens() const { return packet_tokens_; }
    [[nodiscard]] uint64_t byte_tokens() const { return byte_tokens_; }

    // Reset to full buckets
    void reset();

    // Statistics
    [[nodiscard]] uint64_t packets_dropped() const { return packets_dropped_; }
    [[nodiscard]] uint64_t bytes_dropped() const { return bytes_dropped_; }

    // Set current time for testing
    void set_current_time_ms(uint64_t time_ms);

private:
    RateLimiterConfig config_;
    uint64_t packet_tokens_;
    uint64_t byte_tokens_;
    uint64_t last_refill_time_ms_{0};
    uint64_t current_time_ms_{0};

    // Statistics
    uint64_t packets_dropped_{0};
    uint64_t bytes_dropped_{0};
};

}  // namespace veil::mux
