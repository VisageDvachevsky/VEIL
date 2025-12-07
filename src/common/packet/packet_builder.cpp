#include "common/packet/packet_builder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

#include "common/crypto/random.h"

namespace {
constexpr std::size_t kHeaderSize = 2 + 1 + 1 + 8 + 8 + 1 + 2;
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kMaxPayload = 65535;

void write_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void write_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int i = 7; i >= 0; --i) {
    out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
  }
}

std::uint16_t read_u16(std::span<const std::uint8_t> data, std::size_t offset) {
  return static_cast<std::uint16_t>((data[offset] << 8) | data[offset + 1]);
}

std::uint64_t read_u64(std::span<const std::uint8_t> data, std::size_t offset) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | data[offset + static_cast<std::size_t>(i)];
  }
  return value;
}
}  // namespace

namespace veil::packet {

PacketBuilder& PacketBuilder::set_session_id(std::uint64_t id) {
  packet_.session_id = id;
  return *this;
}

PacketBuilder& PacketBuilder::set_sequence(std::uint64_t seq) {
  packet_.sequence = seq;
  return *this;
}

PacketBuilder& PacketBuilder::set_flags(std::uint8_t flags) {
  packet_.flags = flags;
  return *this;
}

PacketBuilder& PacketBuilder::add_frame(FrameType type, std::span<const std::uint8_t> data) {
  packet_.frames.push_back(Frame{type, std::vector<std::uint8_t>(data.begin(), data.end())});
  return *this;
}

PacketBuilder& PacketBuilder::add_padding(std::size_t bytes) {
  auto padding = crypto::random_bytes(bytes);
  packet_.frames.push_back(Frame{FrameType::kPadding, std::move(padding)});
  return *this;
}

std::vector<std::uint8_t> PacketBuilder::build() const {
  if (packet_.frames.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw std::runtime_error("frame count overflow");
  }
  std::size_t payload_size = 0;
  for (const auto& frame : packet_.frames) {
    payload_size += 1 + 2 + frame.data.size();
  }
  if (payload_size > kMaxPayload) {
    throw std::runtime_error("payload too large");
  }

  std::vector<std::uint8_t> buffer;
  buffer.reserve(kHeaderSize + payload_size);

  const auto magic_bytes = magic();
  buffer.insert(buffer.end(), magic_bytes.begin(), magic_bytes.end());
  buffer.push_back(kVersion);
  buffer.push_back(packet_.flags);
  write_u64(buffer, packet_.session_id);
  write_u64(buffer, packet_.sequence);
  buffer.push_back(static_cast<std::uint8_t>(packet_.frames.size()));
  write_u16(buffer, static_cast<std::uint16_t>(payload_size));

  for (const auto& frame : packet_.frames) {
    buffer.push_back(static_cast<std::uint8_t>(frame.type));
    write_u16(buffer, static_cast<std::uint16_t>(frame.data.size()));
    buffer.insert(buffer.end(), frame.data.begin(), frame.data.end());
  }
  return buffer;
}

std::optional<Packet> PacketParser::parse(std::span<const std::uint8_t> buffer) {
  if (buffer.size() < kHeaderSize) {
    return std::nullopt;
  }

  const auto magic_bytes = PacketBuilder::magic();
  if (!std::equal(magic_bytes.begin(), magic_bytes.end(), buffer.begin())) {
    return std::nullopt;
  }

  const auto version = buffer[2];
  if (version != kVersion) {
    return std::nullopt;
  }

  Packet pkt{};
  pkt.version = version;
  pkt.flags = buffer[3];
  pkt.session_id = read_u64(buffer, 4);
  pkt.sequence = read_u64(buffer, 12);
  const std::uint8_t frame_count = buffer[20];
  const std::uint16_t payload_len = read_u16(buffer, 21);

  if (buffer.size() != kHeaderSize + payload_len) {
    return std::nullopt;
  }

  std::size_t offset = kHeaderSize;
  for (std::uint8_t i = 0; i < frame_count; ++i) {
    if (offset + 3 > buffer.size()) {
      return std::nullopt;
    }
    const auto type = static_cast<FrameType>(buffer[offset]);
    const std::uint16_t len = read_u16(buffer, offset + 1);
    offset += 3;
    if (offset + len > buffer.size()) {
      return std::nullopt;
    }
    std::vector<std::uint8_t> data(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(offset + len));
    pkt.frames.push_back(Frame{type, std::move(data)});
    offset += len;
  }

  if (offset != buffer.size()) {
    return std::nullopt;
  }

  return pkt;
}

}  // namespace veil::packet
