#include "veil/packet/packet_parser.hpp"

#include <cstring>

namespace veil::packet {

namespace {

// Read uint64_t from bytes (big-endian)
uint64_t read_u64(std::span<const uint8_t> data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

// Read uint32_t from bytes (big-endian)
uint32_t read_u32(std::span<const uint8_t> data) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | data[i];
    }
    return value;
}

// Read uint16_t from bytes (big-endian)
uint16_t read_u16(std::span<const uint8_t> data) {
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}

// Helper for parsing frame header
std::optional<FrameHeader> parse_frame_header(std::span<const uint8_t> data) {
    return parse_header(data);
}

}  // namespace

void PacketParser::set_decryption_key(const crypto::SymmetricKey& key,
                                       const crypto::Nonce& nonce_base) {
    key_ = key;
    nonce_base_ = nonce_base;
    has_key_ = true;
}

std::optional<PacketHeader> PacketParser::parse_header(std::span<const uint8_t> data) {
    if (data.size() < PacketHeader::SIZE) {
        return std::nullopt;
    }

    PacketHeader header;
    header.session_id = read_u64(data.subspan(0, 8));
    header.packet_counter = read_u64(data.subspan(8, 8));
    return header;
}

std::optional<std::pair<Frame, size_t>> PacketParser::parse_frame(std::span<const uint8_t> data) {
    auto header_opt = parse_frame_header(data);
    if (!header_opt) {
        return std::nullopt;
    }

    auto frame_header = *header_opt;

    if (data.size() < FrameHeader::SIZE + frame_header.length) {
        return std::nullopt;
    }

    auto payload = data.subspan(FrameHeader::SIZE, frame_header.length);
    size_t total_size = FrameHeader::SIZE + frame_header.length;

    switch (frame_header.type) {
        case FrameType::DATA: {
            if (payload.size() < 8) return std::nullopt;
            DataFrame frame;
            frame.sequence_number = read_u64(payload);
            frame.payload.assign(payload.begin() + 8, payload.end());
            return std::make_pair(Frame{frame}, total_size);
        }

        case FrameType::ACK: {
            if (payload.size() < 20) return std::nullopt;
            AckFrame frame;
            frame.ack_number = read_u64(payload);
            frame.bitmap = read_u64(payload.subspan(8));
            frame.recv_window = read_u32(payload.subspan(16));
            return std::make_pair(Frame{frame}, total_size);
        }

        case FrameType::CONTROL: {
            if (payload.size() < 9) return std::nullopt;
            ControlFrame frame;
            frame.type = static_cast<ControlFrame::Type>(payload[0]);
            frame.timestamp = read_u64(payload.subspan(1));
            frame.data.assign(payload.begin() + 9, payload.end());
            return std::make_pair(Frame{frame}, total_size);
        }

        case FrameType::FRAGMENT: {
            if (payload.size() < 8) return std::nullopt;
            FragmentFrame frame;
            frame.message_id = read_u32(payload);
            frame.fragment_index = read_u16(payload.subspan(4));
            frame.total_fragments = read_u16(payload.subspan(6));
            frame.payload.assign(payload.begin() + 8, payload.end());
            return std::make_pair(Frame{frame}, total_size);
        }

        case FrameType::HANDSHAKE: {
            if (payload.empty()) return std::nullopt;
            HandshakeFrame frame;
            frame.stage = static_cast<HandshakeFrame::Stage>(payload[0]);
            frame.payload.assign(payload.begin() + 1, payload.end());
            return std::make_pair(Frame{frame}, total_size);
        }

        case FrameType::SESSION_ROTATE: {
            if (payload.size() < 40) return std::nullopt;
            SessionRotateFrame frame;
            std::copy(payload.begin(), payload.begin() + 32, frame.new_session_id.begin());
            frame.activation_sequence = read_u64(payload.subspan(32));
            return std::make_pair(Frame{frame}, total_size);
        }

        default:
            return std::nullopt;
    }
}

std::optional<ParsedPacket> PacketParser::parse(std::span<const uint8_t> data, ParseError* error) {
    auto set_error = [error](ParseError e) {
        if (error) *error = e;
    };

    // Minimum packet size: header + tag
    constexpr size_t MIN_SIZE = PacketHeader::SIZE + crypto::POLY1305_TAG_SIZE;
    if (data.size() < MIN_SIZE) {
        set_error(ParseError::PACKET_TOO_SHORT);
        return std::nullopt;
    }

    // Parse header
    auto header_opt = parse_header(data);
    if (!header_opt) {
        set_error(ParseError::PACKET_TOO_SHORT);
        return std::nullopt;
    }

    if (!has_key_) {
        set_error(ParseError::DECRYPTION_FAILED);
        return std::nullopt;
    }

    // Extract encrypted payload
    auto encrypted = data.subspan(PacketHeader::SIZE);

    // Create nonce from base and counter
    auto nonce = crypto::make_nonce(nonce_base_, header_opt->packet_counter);

    // Additional data is the header
    std::span<const uint8_t> aad(data.data(), PacketHeader::SIZE);

    // Decrypt
    auto decrypted = crypto::decrypt(key_, nonce, encrypted, aad);
    if (!decrypted) {
        set_error(ParseError::DECRYPTION_FAILED);
        return std::nullopt;
    }

    // Parse frames from decrypted payload
    ParsedPacket result;
    result.session_id = header_opt->session_id;
    result.packet_counter = header_opt->packet_counter;

    std::span<const uint8_t> payload = *decrypted;
    while (!payload.empty()) {
        auto frame_result = parse_frame(payload);
        if (!frame_result) {
            set_error(ParseError::INVALID_FRAME);
            return std::nullopt;
        }

        result.frames.push_back(std::move(frame_result->first));
        payload = payload.subspan(frame_result->second);
    }

    set_error(ParseError::SUCCESS);
    return result;
}

}  // namespace veil::packet
