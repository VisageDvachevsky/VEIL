#include "veil/packet/packet_builder.hpp"

#include <cstring>
#include <stdexcept>

namespace veil::packet {

namespace {

// Serialize uint64_t to bytes (big-endian)
void write_u64(std::vector<uint8_t>& buffer, uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        buffer.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

// Serialize uint32_t to bytes (big-endian)
void write_u32(std::vector<uint8_t>& buffer, uint32_t value) {
    for (int i = 3; i >= 0; --i) {
        buffer.push_back(static_cast<uint8_t>(value >> (i * 8)));
    }
}

// Serialize uint16_t to bytes (big-endian)
void write_u16(std::vector<uint8_t>& buffer, uint16_t value) {
    buffer.push_back(static_cast<uint8_t>(value >> 8));
    buffer.push_back(static_cast<uint8_t>(value & 0xFF));
}

}  // namespace

PacketBuilder::PacketBuilder(size_t mtu) : mtu_(mtu) {
    // Reserve space for typical packet
    payload_buffer_.reserve(mtu - PacketHeader::SIZE - crypto::POLY1305_TAG_SIZE);
}

void PacketBuilder::set_encryption_key(const crypto::SymmetricKey& key,
                                        const crypto::Nonce& nonce_base) {
    key_ = key;
    nonce_base_ = nonce_base;
    has_key_ = true;
}

void PacketBuilder::set_session_id(uint64_t session_id) {
    session_id_ = session_id;
}

std::vector<uint8_t> PacketBuilder::serialize_frame(const Frame& frame) {
    std::vector<uint8_t> result;

    std::visit([&result](const auto& f) {
        using T = std::decay_t<decltype(f)>;

        std::vector<uint8_t> payload;

        if constexpr (std::is_same_v<T, DataFrame>) {
            write_u64(payload, f.sequence_number);
            payload.insert(payload.end(), f.payload.begin(), f.payload.end());
        } else if constexpr (std::is_same_v<T, AckFrame>) {
            write_u64(payload, f.ack_number);
            write_u64(payload, f.bitmap);
            write_u32(payload, f.recv_window);
        } else if constexpr (std::is_same_v<T, ControlFrame>) {
            payload.push_back(static_cast<uint8_t>(f.type));
            write_u64(payload, f.timestamp);
            payload.insert(payload.end(), f.data.begin(), f.data.end());
        } else if constexpr (std::is_same_v<T, FragmentFrame>) {
            write_u32(payload, f.message_id);
            write_u16(payload, f.fragment_index);
            write_u16(payload, f.total_fragments);
            payload.insert(payload.end(), f.payload.begin(), f.payload.end());
        } else if constexpr (std::is_same_v<T, HandshakeFrame>) {
            payload.push_back(static_cast<uint8_t>(f.stage));
            payload.insert(payload.end(), f.payload.begin(), f.payload.end());
        } else if constexpr (std::is_same_v<T, SessionRotateFrame>) {
            payload.insert(payload.end(), f.new_session_id.begin(), f.new_session_id.end());
            write_u64(payload, f.activation_sequence);
        }

        // Write frame header
        FrameHeader header;
        header.type = get_frame_type(Frame{f});
        header.flags = 0;
        header.length = static_cast<uint16_t>(payload.size());

        auto header_bytes = serialize_header(header);
        result.insert(result.end(), header_bytes.begin(), header_bytes.end());
        result.insert(result.end(), payload.begin(), payload.end());
    }, frame);

    return result;
}

size_t PacketBuilder::frame_size(const Frame& frame) {
    size_t payload_size = std::visit([](const auto& f) -> size_t {
        using T = std::decay_t<decltype(f)>;

        if constexpr (std::is_same_v<T, DataFrame>) {
            return 8 + f.payload.size();  // sequence(8) + payload
        } else if constexpr (std::is_same_v<T, AckFrame>) {
            return 20;  // ack_number(8) + bitmap(8) + recv_window(4)
        } else if constexpr (std::is_same_v<T, ControlFrame>) {
            return 9 + f.data.size();  // type(1) + timestamp(8) + data
        } else if constexpr (std::is_same_v<T, FragmentFrame>) {
            return 8 + f.payload.size();  // msg_id(4) + index(2) + total(2) + payload
        } else if constexpr (std::is_same_v<T, HandshakeFrame>) {
            return 1 + f.payload.size();  // stage(1) + payload
        } else if constexpr (std::is_same_v<T, SessionRotateFrame>) {
            return 40;  // session_id(32) + activation_seq(8)
        }
        return 0;
    }, frame);

    return FrameHeader::SIZE + payload_size;
}

bool PacketBuilder::add_frame(const Frame& frame) {
    auto serialized = serialize_frame(frame);

    if (payload_buffer_.size() + serialized.size() > remaining_capacity()) {
        return false;
    }

    payload_buffer_.insert(payload_buffer_.end(), serialized.begin(), serialized.end());
    return true;
}

size_t PacketBuilder::remaining_capacity() const {
    size_t overhead = PacketHeader::SIZE + crypto::POLY1305_TAG_SIZE;
    if (mtu_ <= overhead + payload_buffer_.size()) {
        return 0;
    }
    return mtu_ - overhead - payload_buffer_.size();
}

std::vector<uint8_t> PacketBuilder::build(uint64_t packet_counter) {
    if (payload_buffer_.empty()) {
        return {};
    }

    if (!has_key_) {
        throw std::runtime_error("Encryption key not set");
    }

    std::vector<uint8_t> packet;
    packet.reserve(PacketHeader::SIZE + payload_buffer_.size() + crypto::POLY1305_TAG_SIZE);

    // Write packet header
    write_u64(packet, session_id_);
    write_u64(packet, packet_counter);

    // Create nonce from base and counter
    auto nonce = crypto::make_nonce(nonce_base_, packet_counter);

    // Create additional data (the header)
    std::span<const uint8_t> aad(packet.data(), PacketHeader::SIZE);

    // Encrypt payload
    auto encrypted = crypto::encrypt(key_, nonce, payload_buffer_, aad);
    packet.insert(packet.end(), encrypted.begin(), encrypted.end());

    return packet;
}

void PacketBuilder::reset() {
    payload_buffer_.clear();
}

}  // namespace veil::packet
