#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace veil::packet {

// Frame types
enum class FrameType : uint8_t {
    DATA = 0x01,          // Application data
    ACK = 0x02,           // Acknowledgment
    CONTROL = 0x03,       // Control/ping
    FRAGMENT = 0x04,      // Fragmented data
    HANDSHAKE = 0x10,     // Handshake message
    SESSION_ROTATE = 0x20 // Session rotation signal
};

// Data frame
struct DataFrame {
    uint64_t sequence_number;
    std::vector<uint8_t> payload;
};

// ACK frame with selective acknowledgment bitmap
struct AckFrame {
    uint64_t ack_number;     // Highest contiguous sequence acknowledged
    uint64_t bitmap;         // Bitmap for selective ACK (next 64 packets after ack_number)
    uint32_t recv_window;    // Receive window size
};

// Control frame
struct ControlFrame {
    enum class Type : uint8_t {
        PING = 0x01,
        PONG = 0x02,
        CLOSE = 0x03,
        RESET = 0x04
    };
    Type type;
    uint64_t timestamp;
    std::vector<uint8_t> data;  // Optional data
};

// Fragment frame
struct FragmentFrame {
    uint32_t message_id;    // ID of the original message
    uint16_t fragment_index;
    uint16_t total_fragments;
    std::vector<uint8_t> payload;
};

// Handshake frame
struct HandshakeFrame {
    enum class Stage : uint8_t {
        INIT = 0x01,      // Initiator sends ephemeral public key
        RESPONSE = 0x02,  // Responder sends ephemeral public key + encrypted payload
        FINISH = 0x03     // Initiator confirms
    };
    Stage stage;
    std::vector<uint8_t> payload;
};

// Session rotation frame
struct SessionRotateFrame {
    std::array<uint8_t, 32> new_session_id;
    uint64_t activation_sequence;  // Sequence number when new session activates
};

// Generic frame
using Frame = std::variant<
    DataFrame,
    AckFrame,
    ControlFrame,
    FragmentFrame,
    HandshakeFrame,
    SessionRotateFrame
>;

// Frame header (wire format)
struct FrameHeader {
    static constexpr size_t SIZE = 4;  // type(1) + flags(1) + length(2)
    FrameType type;
    uint8_t flags;
    uint16_t length;  // Payload length (not including header)
};

// Get frame type from variant
FrameType get_frame_type(const Frame& frame);

// Serialize frame header
std::array<uint8_t, FrameHeader::SIZE> serialize_header(const FrameHeader& header);

// Parse frame header
std::optional<FrameHeader> parse_header(std::span<const uint8_t> data);

}  // namespace veil::packet
