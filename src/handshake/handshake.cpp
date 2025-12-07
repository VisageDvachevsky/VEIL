#include "veil/handshake/handshake.hpp"
#include "veil/crypto/chacha20poly1305.hpp"

#include <chrono>
#include <cstring>

namespace veil::handshake {

namespace {

// Message types
constexpr uint8_t MSG_INIT = 0x01;
constexpr uint8_t MSG_RESPONSE = 0x02;
constexpr uint8_t MSG_FINISH = 0x03;

// Message layout:
// [type: 1][timestamp: 8][payload_len: 2][payload: N][hmac: 32]

constexpr size_t MSG_HEADER_SIZE = 1 + 8 + 2;  // type + timestamp + payload_len
constexpr size_t MSG_HMAC_SIZE = 32;

uint64_t read_u64(const uint8_t* data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

void write_u64(uint8_t* data, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        data[i] = static_cast<uint8_t>(value);
        value >>= 8;
    }
}

uint16_t read_u16(const uint8_t* data) {
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

void write_u16(uint8_t* data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value >> 8);
    data[1] = static_cast<uint8_t>(value);
}

}  // namespace

Handshake::Handshake(const HandshakeConfig& config)
    : config_(config) {
    // Generate ephemeral key pair
    our_keypair_ = crypto::generate_keypair();
}

void Handshake::set_send_callback(SendCallback callback) {
    send_callback_ = std::move(callback);
}

uint64_t Handshake::get_current_time() {
    if (current_time_ != 0) {
        return current_time_;
    }
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

void Handshake::set_current_time(uint64_t time) {
    current_time_ = time;
}

std::array<uint8_t, 32> Handshake::compute_hmac(std::span<const uint8_t> data) {
    // Use PSK as HMAC key, or zeros if no PSK
    return crypto::hmac_sha256(config_.psk, data);
}

bool Handshake::verify_hmac(std::span<const uint8_t> data, std::span<const uint8_t> expected_hmac) {
    auto computed = compute_hmac(data);
    return crypto::constant_time_compare(computed, expected_hmac);
}

std::vector<uint8_t> Handshake::build_message(uint8_t type, std::span<const uint8_t> payload) {
    std::vector<uint8_t> msg;
    msg.reserve(MSG_HEADER_SIZE + payload.size() + MSG_HMAC_SIZE);

    // Type
    msg.push_back(type);

    // Timestamp
    uint64_t ts = get_current_time();
    msg.resize(msg.size() + 8);
    write_u64(msg.data() + 1, ts);

    // Payload length
    msg.resize(msg.size() + 2);
    write_u16(msg.data() + 9, static_cast<uint16_t>(payload.size()));

    // Payload
    msg.insert(msg.end(), payload.begin(), payload.end());

    // HMAC over entire message (without HMAC itself)
    auto hmac = compute_hmac(msg);
    msg.insert(msg.end(), hmac.begin(), hmac.end());

    // Add to transcript
    transcript_.insert(transcript_.end(), msg.begin(), msg.end());

    return msg;
}

bool Handshake::initiate() {
    if (state_ != HandshakeState::IDLE) {
        last_error_ = HandshakeError::INTERNAL_ERROR;
        return false;
    }

    // Build INIT message with our public key
    auto msg = build_message(MSG_INIT, our_keypair_.public_key);

    if (send_callback_) {
        send_callback_(std::move(msg));
    }

    state_ = HandshakeState::INIT_SENT;
    return true;
}

bool Handshake::process_message(std::span<const uint8_t> message) {
    // Minimum message size
    if (message.size() < MSG_HEADER_SIZE + MSG_HMAC_SIZE) {
        last_error_ = HandshakeError::INVALID_MESSAGE;
        return false;
    }

    // Parse header
    uint8_t type = message[0];
    uint64_t timestamp = read_u64(message.data() + 1);
    uint16_t payload_len = read_u16(message.data() + 9);

    // Verify message length
    if (message.size() != MSG_HEADER_SIZE + payload_len + MSG_HMAC_SIZE) {
        last_error_ = HandshakeError::INVALID_MESSAGE;
        return false;
    }

    // Verify timestamp
    uint64_t now = get_current_time();
    uint64_t diff = (timestamp > now) ? (timestamp - now) : (now - timestamp);
    if (diff > config_.timestamp_tolerance_sec) {
        last_error_ = HandshakeError::TIMESTAMP_OUT_OF_RANGE;
        // Silent drop for anti-probing
        return false;
    }

    // Verify HMAC
    auto hmac_offset = MSG_HEADER_SIZE + payload_len;
    auto msg_without_hmac = message.subspan(0, hmac_offset);
    auto received_hmac = message.subspan(hmac_offset, MSG_HMAC_SIZE);

    if (!verify_hmac(msg_without_hmac, received_hmac)) {
        last_error_ = HandshakeError::HMAC_VERIFICATION_FAILED;
        // Silent drop for anti-probing
        return false;
    }

    // Add to transcript
    transcript_.insert(transcript_.end(), message.begin(), message.end());

    // Extract payload
    auto payload = message.subspan(MSG_HEADER_SIZE, payload_len);

    // Handle based on type and state
    switch (type) {
        case MSG_INIT:
            return handle_init(payload);
        case MSG_RESPONSE:
            return handle_response(payload);
        case MSG_FINISH:
            return handle_finish(payload);
        default:
            last_error_ = HandshakeError::INVALID_MESSAGE;
            return false;
    }
}

bool Handshake::handle_init(std::span<const uint8_t> payload) {
    if (state_ != HandshakeState::IDLE) {
        last_error_ = HandshakeError::INVALID_MESSAGE;
        return false;
    }

    // Payload should be peer's public key
    if (payload.size() != crypto::X25519_PUBLIC_KEY_SIZE) {
        last_error_ = HandshakeError::INVALID_MESSAGE;
        return false;
    }

    // Store peer's public key
    std::copy(payload.begin(), payload.end(), peer_public_key_.begin());

    // Perform key exchange
    auto shared = crypto::key_exchange(our_keypair_.secret_key, peer_public_key_);
    if (!shared) {
        last_error_ = HandshakeError::KEY_EXCHANGE_FAILED;
        return false;
    }
    shared_secret_ = shared;

    // Don't derive session ID yet - wait until we have the full transcript

    state_ = HandshakeState::INIT_RECEIVED;

    // Send RESPONSE with our public key
    auto msg = build_message(MSG_RESPONSE, our_keypair_.public_key);
    if (send_callback_) {
        send_callback_(std::move(msg));
    }

    state_ = HandshakeState::RESPONSE_SENT;
    return false;  // Not complete yet
}

bool Handshake::handle_response(std::span<const uint8_t> payload) {
    if (state_ != HandshakeState::INIT_SENT) {
        last_error_ = HandshakeError::INVALID_MESSAGE;
        return false;
    }

    // Payload should be peer's public key
    if (payload.size() != crypto::X25519_PUBLIC_KEY_SIZE) {
        last_error_ = HandshakeError::INVALID_MESSAGE;
        return false;
    }

    // Store peer's public key
    std::copy(payload.begin(), payload.end(), peer_public_key_.begin());

    // Perform key exchange
    auto shared = crypto::key_exchange(our_keypair_.secret_key, peer_public_key_);
    if (!shared) {
        last_error_ = HandshakeError::KEY_EXCHANGE_FAILED;
        return false;
    }
    shared_secret_ = shared;

    // Send FINISH (empty payload, just confirms)
    auto msg = build_message(MSG_FINISH, {});
    if (send_callback_) {
        send_callback_(std::move(msg));
    }

    // Derive session ID from full transcript (INIT + RESPONSE + FINISH)
    derive_session_id();

    state_ = HandshakeState::COMPLETE;
    return true;
}

bool Handshake::handle_finish(std::span<const uint8_t> payload) {
    if (state_ != HandshakeState::RESPONSE_SENT) {
        last_error_ = HandshakeError::INVALID_MESSAGE;
        return false;
    }

    // FINISH can have empty payload or additional confirmation data
    // For now we just accept it

    // Derive session ID from full transcript (INIT + RESPONSE + FINISH)
    derive_session_id();

    state_ = HandshakeState::COMPLETE;
    return true;
}

void Handshake::derive_session_id() {
    // Derive session ID from transcript hash
    auto transcript_hash = crypto::hmac_sha256(config_.psk, transcript_);
    std::copy(transcript_hash.begin(), transcript_hash.end(), session_id_.begin());
}

std::optional<HandshakeResult> Handshake::result() const {
    if (state_ != HandshakeState::COMPLETE || !shared_secret_) {
        return std::nullopt;
    }

    HandshakeResult res;
    res.session_id = session_id_;
    res.is_initiator = (state_ == HandshakeState::COMPLETE);  // Simplified

    // Derive session keys
    // Initiator: we initiated if we went through INIT_SENT -> RESPONSE received
    // For now, determine based on whose public key comes first lexicographically
    bool is_initiator = std::memcmp(our_keypair_.public_key.data(),
                                     peer_public_key_.data(),
                                     crypto::X25519_PUBLIC_KEY_SIZE) < 0;
    res.is_initiator = is_initiator;

    res.session_keys = crypto::derive_session_keys(*shared_secret_, session_id_, is_initiator);

    return res;
}

void Handshake::reset() {
    state_ = HandshakeState::IDLE;
    last_error_ = HandshakeError::NONE;
    our_keypair_ = crypto::generate_keypair();
    peer_public_key_ = {};
    shared_secret_.reset();
    session_id_ = {};
    transcript_.clear();
}

}  // namespace veil::handshake
