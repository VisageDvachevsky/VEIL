#include "veil/mux/session_rotator.hpp"

#include <chrono>

namespace veil::mux {

SessionRotator::SessionRotator(const SessionRotatorConfig& config)
    : config_(config) {
    // Initialize with current time
    auto now = std::chrono::steady_clock::now();
    current_time_ = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    session_start_time_ = current_time_;

    // Generate initial session ID
    current_session_id_ = rotate();
}

void SessionRotator::set_rotation_callback(RotationCallback callback) {
    callback_ = std::move(callback);
}

void SessionRotator::on_packet_sent(size_t bytes) {
    ++packets_sent_;
    bytes_sent_ += bytes;
}

void SessionRotator::on_packet_received(size_t bytes) {
    ++packets_received_;
    bytes_received_ += bytes;
}

bool SessionRotator::should_rotate() const {
    // Check packet count
    uint64_t total_packets = packets_sent_ + packets_received_;
    if (total_packets >= config_.packets_per_session) {
        return true;
    }

    // Check byte count
    uint64_t total_bytes = bytes_sent_ + bytes_received_;
    if (total_bytes >= config_.bytes_per_session) {
        return true;
    }

    // Check time
    if (current_time_ - session_start_time_ >= config_.seconds_per_session) {
        return true;
    }

    return false;
}

SessionRotator::SessionId SessionRotator::rotate() {
    // Generate new session ID using random bytes
    std::array<uint8_t, 8> random_bytes;
    crypto::random_bytes(random_bytes);

    SessionId new_id = 0;
    for (int i = 0; i < 8; ++i) {
        new_id = (new_id << 8) | random_bytes[i];
    }

    current_session_id_ = new_id;
    reset_counters();

    if (callback_) {
        callback_(new_id);
    }

    return new_id;
}

void SessionRotator::reset_counters() {
    packets_sent_ = 0;
    packets_received_ = 0;
    bytes_sent_ = 0;
    bytes_received_ = 0;
    session_start_time_ = current_time_;
}

void SessionRotator::set_current_time(uint64_t time) {
    current_time_ = time;
}

}  // namespace veil::mux
