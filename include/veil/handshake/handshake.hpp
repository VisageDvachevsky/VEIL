#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "veil/crypto/crypto.hpp"
#include "veil/crypto/x25519.hpp"
#include "veil/crypto/hkdf.hpp"

namespace veil::handshake {

// Pre-shared key for additional authentication
using PSK = std::array<uint8_t, 32>;

// Handshake configuration
struct HandshakeConfig {
    PSK psk{};                         // Pre-shared key (optional, zeros = no PSK)
    uint64_t timestamp_tolerance_sec = 60;  // Max clock skew allowed
    bool require_psk = false;          // Require PSK for authentication
    size_t max_handshake_attempts = 3; // Max handshake attempts before rate limiting
};

// Handshake result
struct HandshakeResult {
    crypto::SessionKeys session_keys;
    std::array<uint8_t, 32> session_id;
    bool is_initiator;
};

// Handshake state
enum class HandshakeState {
    IDLE,
    INIT_SENT,       // Initiator: sent INIT, waiting for RESPONSE
    INIT_RECEIVED,   // Responder: received INIT, preparing RESPONSE
    RESPONSE_SENT,   // Responder: sent RESPONSE, waiting for FINISH
    COMPLETE,        // Handshake completed successfully
    FAILED           // Handshake failed
};

// Handshake failure reason
enum class HandshakeError {
    NONE,
    INVALID_MESSAGE,
    TIMESTAMP_OUT_OF_RANGE,
    HMAC_VERIFICATION_FAILED,
    KEY_EXCHANGE_FAILED,
    PSK_REQUIRED_BUT_MISSING,
    RATE_LIMITED,
    INTERNAL_ERROR
};

// Handshake protocol handler
class Handshake {
public:
    using SendCallback = std::function<void(std::vector<uint8_t> message)>;

    explicit Handshake(const HandshakeConfig& config = {});

    // Set callback for sending handshake messages
    void set_send_callback(SendCallback callback);

    // Initiate handshake (client side)
    bool initiate();

    // Process incoming handshake message
    // Returns true if handshake is complete
    bool process_message(std::span<const uint8_t> message);

    // Get current state
    [[nodiscard]] HandshakeState state() const { return state_; }

    // Get last error
    [[nodiscard]] HandshakeError last_error() const { return last_error_; }

    // Get handshake result (only valid after COMPLETE)
    [[nodiscard]] std::optional<HandshakeResult> result() const;

    // Reset to initial state
    void reset();

    // Get current timestamp (for testing)
    void set_current_time(uint64_t time);

private:
    HandshakeConfig config_;
    HandshakeState state_{HandshakeState::IDLE};
    HandshakeError last_error_{HandshakeError::NONE};
    SendCallback send_callback_;

    // Our ephemeral key pair
    crypto::X25519KeyPair our_keypair_;

    // Peer's ephemeral public key
    crypto::PublicKey peer_public_key_{};

    // Derived shared secret
    std::optional<crypto::SharedSecret> shared_secret_;

    // Session ID (derived during handshake)
    std::array<uint8_t, 32> session_id_{};

    // Transcript hash (for binding)
    std::vector<uint8_t> transcript_;

    // Current time (can be overridden for testing)
    uint64_t current_time_{0};

    // Internal message handling
    bool handle_init(std::span<const uint8_t> payload);
    bool handle_response(std::span<const uint8_t> payload);
    bool handle_finish(std::span<const uint8_t> payload);

    // Create HMAC for anti-probing
    std::array<uint8_t, 32> compute_hmac(std::span<const uint8_t> data);

    // Verify HMAC
    bool verify_hmac(std::span<const uint8_t> data, std::span<const uint8_t> expected_hmac);

    // Derive session ID from transcript
    void derive_session_id();

    // Get current time
    uint64_t get_current_time();

    // Build handshake message with type, timestamp, and HMAC
    std::vector<uint8_t> build_message(uint8_t type, std::span<const uint8_t> payload);
};

}  // namespace veil::handshake
