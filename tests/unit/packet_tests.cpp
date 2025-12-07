#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/packet/packet_builder.h"

namespace veil::tests {

TEST(PacketTests, BuildAndParseRoundTrip) {
  packet::PacketBuilder builder;
  builder.set_session_id(42).set_sequence(7).set_flags(0xAA);
  const std::vector<std::uint8_t> payload{'h', 'i'};
  builder.add_frame(packet::FrameType::kData, payload);
  builder.add_padding(8);

  const auto bytes = builder.build();
  auto parsed = packet::PacketParser::parse(bytes);
  ASSERT_TRUE(parsed.has_value());
  const auto pkt = parsed.value();
  EXPECT_EQ(pkt.session_id, 42U);
  EXPECT_EQ(pkt.sequence, 7U);
  EXPECT_EQ(pkt.flags, 0xAA);
  ASSERT_EQ(pkt.frames.size(), 2U);
  EXPECT_EQ(pkt.frames[0].type, packet::FrameType::kData);
  EXPECT_EQ(pkt.frames[0].data, payload);
  EXPECT_EQ(pkt.frames[1].type, packet::FrameType::kPadding);
}

TEST(PacketTests, RejectsInvalidMagic) {
  std::vector<std::uint8_t> bytes{0, 0};
  auto parsed = packet::PacketParser::parse(bytes);
  EXPECT_FALSE(parsed.has_value());
}

}  // namespace veil::tests
