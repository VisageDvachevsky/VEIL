#include "veil/mux/rate_limiter.hpp"

#include <algorithm>
#include <chrono>

namespace veil::mux {

RateLimiter::RateLimiter(const RateLimiterConfig& config)
    : config_(config),
      packet_tokens_(config.burst_packets),
      byte_tokens_(config.burst_bytes) {
    // Initialize with current time
    auto now = std::chrono::steady_clock::now();
    current_time_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    last_refill_time_ms_ = current_time_ms_;
}

bool RateLimiter::check(size_t packet_bytes) const {
    return packet_tokens_ >= 1 && byte_tokens_ >= packet_bytes;
}

void RateLimiter::consume(size_t packet_bytes) {
    if (packet_tokens_ > 0) {
        --packet_tokens_;
    }
    if (byte_tokens_ >= packet_bytes) {
        byte_tokens_ -= packet_bytes;
    } else {
        byte_tokens_ = 0;
    }
}

bool RateLimiter::try_consume(size_t packet_bytes) {
    if (!check(packet_bytes)) {
        ++packets_dropped_;
        bytes_dropped_ += packet_bytes;
        return false;
    }
    consume(packet_bytes);
    return true;
}

void RateLimiter::refill(uint64_t elapsed_ms) {
    if (elapsed_ms == 0) {
        return;
    }

    // Calculate tokens to add based on rate and elapsed time
    // packets_per_second * elapsed_ms / 1000
    uint64_t packet_add = (config_.packets_per_second * elapsed_ms) / 1000;
    uint64_t byte_add = (config_.bytes_per_second * elapsed_ms) / 1000;

    // Add tokens, capped at burst size
    packet_tokens_ = std::min(packet_tokens_ + packet_add, config_.burst_packets);
    byte_tokens_ = std::min(byte_tokens_ + byte_add, config_.burst_bytes);
}

void RateLimiter::refill_now() {
    auto now = std::chrono::steady_clock::now();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    if (now_ms > last_refill_time_ms_) {
        refill(now_ms - last_refill_time_ms_);
        last_refill_time_ms_ = now_ms;
    }
}

void RateLimiter::reset() {
    packet_tokens_ = config_.burst_packets;
    byte_tokens_ = config_.burst_bytes;
    packets_dropped_ = 0;
    bytes_dropped_ = 0;
}

void RateLimiter::set_current_time_ms(uint64_t time_ms) {
    current_time_ms_ = time_ms;
    if (last_refill_time_ms_ == 0) {
        last_refill_time_ms_ = time_ms;
    } else if (time_ms > last_refill_time_ms_) {
        refill(time_ms - last_refill_time_ms_);
        last_refill_time_ms_ = time_ms;
    }
}

}  // namespace veil::mux
