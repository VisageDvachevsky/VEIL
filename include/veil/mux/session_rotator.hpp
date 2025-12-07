#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include "veil/crypto/crypto.hpp"

namespace veil::mux {

// Session rotation configuration
struct SessionRotatorConfig {
    uint64_t packets_per_session = 1000000;  // Rotate after N packets
    uint64_t bytes_per_session = 1ULL << 30; // Rotate after N bytes (1GB default)
    uint64_t seconds_per_session = 3600;     // Rotate after N seconds (1 hour default)
};

// Manages session key rotation
class SessionRotator {
public:
    using SessionId = uint64_t;
    using RotationCallback = std::function<void(SessionId new_session_id)>;

    explicit SessionRotator(const SessionRotatorConfig& config = {});

    // Set callback for when rotation is needed
    void set_rotation_callback(RotationCallback callback);

    // Update counters and check if rotation is needed
    // Call this after each packet
    void on_packet_sent(size_t bytes);
    void on_packet_received(size_t bytes);

    // Check if rotation is due (without updating counters)
    [[nodiscard]] bool should_rotate() const;

    // Trigger rotation
    // Returns the new session ID
    SessionId rotate();

    // Get current session ID
    [[nodiscard]] SessionId current_session_id() const { return current_session_id_; }

    // Get session start time (for time-based rotation)
    [[nodiscard]] uint64_t session_start_time() const { return session_start_time_; }

    // Reset counters (call after successful rotation)
    void reset_counters();

    // Update current time (for testing)
    void set_current_time(uint64_t time);

private:
    SessionRotatorConfig config_;
    SessionId current_session_id_{0};
    uint64_t packets_sent_{0};
    uint64_t packets_received_{0};
    uint64_t bytes_sent_{0};
    uint64_t bytes_received_{0};
    uint64_t session_start_time_{0};
    uint64_t current_time_{0};
    RotationCallback callback_;
};

}  // namespace veil::mux
