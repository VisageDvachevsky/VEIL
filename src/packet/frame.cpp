#include "veil/packet/frame.hpp"

#include <cstring>

namespace veil::packet {

FrameType get_frame_type(const Frame& frame) {
    return std::visit([](const auto& f) -> FrameType {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, DataFrame>) {
            return FrameType::DATA;
        } else if constexpr (std::is_same_v<T, AckFrame>) {
            return FrameType::ACK;
        } else if constexpr (std::is_same_v<T, ControlFrame>) {
            return FrameType::CONTROL;
        } else if constexpr (std::is_same_v<T, FragmentFrame>) {
            return FrameType::FRAGMENT;
        } else if constexpr (std::is_same_v<T, HandshakeFrame>) {
            return FrameType::HANDSHAKE;
        } else if constexpr (std::is_same_v<T, SessionRotateFrame>) {
            return FrameType::SESSION_ROTATE;
        }
    }, frame);
}

std::array<uint8_t, FrameHeader::SIZE> serialize_header(const FrameHeader& header) {
    std::array<uint8_t, FrameHeader::SIZE> data;
    data[0] = static_cast<uint8_t>(header.type);
    data[1] = header.flags;
    // Length in big-endian
    data[2] = static_cast<uint8_t>(header.length >> 8);
    data[3] = static_cast<uint8_t>(header.length & 0xFF);
    return data;
}

std::optional<FrameHeader> parse_header(std::span<const uint8_t> data) {
    if (data.size() < FrameHeader::SIZE) {
        return std::nullopt;
    }

    FrameHeader header;
    header.type = static_cast<FrameType>(data[0]);
    header.flags = data[1];
    header.length = static_cast<uint16_t>((data[2] << 8) | data[3]);

    return header;
}

}  // namespace veil::packet
