#pragma once

#include <span>
#include <vector>
#include "frame.hpp"
#include "veil/crypto/crypto.hpp"
#include "veil/crypto/chacha20poly1305.hpp"

namespace veil::packet {

// Packet structure (wire format):
// [session_id: 8 bytes][packet_counter: 8 bytes][encrypted_payload][tag: 16 bytes]
// The encrypted_payload contains serialized frames

struct PacketHeader {
    static constexpr size_t SIZE = 16;  // session_id(8) + packet_counter(8)
    uint64_t session_id;
    uint64_t packet_counter;
};

class PacketBuilder {
public:
    explicit PacketBuilder(size_t mtu = 1400);

    // Set encryption key and nonce base
    void set_encryption_key(const crypto::SymmetricKey& key, const crypto::Nonce& nonce_base);

    // Set session ID
    void set_session_id(uint64_t session_id);

    // Add frame to current packet
    bool add_frame(const Frame& frame);

    // Check if packet can fit more data
    [[nodiscard]] size_t remaining_capacity() const;

    // Build the encrypted packet
    // Returns empty if no frames added
    [[nodiscard]] std::vector<uint8_t> build(uint64_t packet_counter);

    // Reset builder for new packet
    void reset();

    // Static helpers for frame serialization
    static std::vector<uint8_t> serialize_frame(const Frame& frame);
    static size_t frame_size(const Frame& frame);

private:
    size_t mtu_;
    uint64_t session_id_{0};
    crypto::SymmetricKey key_{};
    crypto::Nonce nonce_base_{};
    bool has_key_{false};
    std::vector<uint8_t> payload_buffer_;
};

}  // namespace veil::packet
