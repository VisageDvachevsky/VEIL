#pragma once

#include <optional>
#include <span>
#include <vector>
#include "frame.hpp"
#include "packet_builder.hpp"
#include "veil/crypto/crypto.hpp"
#include "veil/crypto/chacha20poly1305.hpp"

namespace veil::packet {

// Parsed packet result
struct ParsedPacket {
    uint64_t session_id;
    uint64_t packet_counter;
    std::vector<Frame> frames;
};

// Parse result enum
enum class ParseError {
    SUCCESS,
    PACKET_TOO_SHORT,
    DECRYPTION_FAILED,
    INVALID_FRAME,
    UNKNOWN_FRAME_TYPE
};

class PacketParser {
public:
    PacketParser() = default;

    // Set decryption key and nonce base
    void set_decryption_key(const crypto::SymmetricKey& key, const crypto::Nonce& nonce_base);

    // Parse an encrypted packet
    // Returns parsed packet on success, nullopt on failure
    std::optional<ParsedPacket> parse(std::span<const uint8_t> data, ParseError* error = nullptr);

    // Parse packet header (without decryption)
    static std::optional<PacketHeader> parse_header(std::span<const uint8_t> data);

    // Parse a single frame from decrypted payload
    static std::optional<std::pair<Frame, size_t>> parse_frame(std::span<const uint8_t> data);

private:
    crypto::SymmetricKey key_{};
    crypto::Nonce nonce_base_{};
    bool has_key_{false};
};

}  // namespace veil::packet
