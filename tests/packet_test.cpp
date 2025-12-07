#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "veil/crypto/crypto.hpp"
#include "veil/packet/frame.hpp"
#include "veil/packet/packet_builder.hpp"
#include "veil/packet/packet_parser.hpp"

namespace veil::packet {
namespace {

class PacketTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(crypto::init());
        crypto::random_bytes(key_);
        crypto::random_bytes(nonce_);
    }

    crypto::SymmetricKey key_;
    crypto::Nonce nonce_;
};

TEST_F(PacketTest, FrameHeaderSerialization) {
    FrameHeader header;
    header.type = FrameType::DATA;
    header.flags = 0x42;
    header.length = 1234;

    auto bytes = serialize_header(header);
    auto parsed = parse_header(bytes);

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->type, header.type);
    EXPECT_EQ(parsed->flags, header.flags);
    EXPECT_EQ(parsed->length, header.length);
}

TEST_F(PacketTest, FrameHeaderTooShort) {
    std::array<uint8_t, 2> short_data = {0x01, 0x00};
    auto parsed = parse_header(short_data);
    EXPECT_FALSE(parsed.has_value());
}

TEST_F(PacketTest, GetFrameTypeVariant) {
    DataFrame data_frame;
    AckFrame ack_frame;
    ControlFrame control_frame;
    FragmentFrame fragment_frame;
    HandshakeFrame handshake_frame;
    SessionRotateFrame rotate_frame;

    EXPECT_EQ(get_frame_type(Frame{data_frame}), FrameType::DATA);
    EXPECT_EQ(get_frame_type(Frame{ack_frame}), FrameType::ACK);
    EXPECT_EQ(get_frame_type(Frame{control_frame}), FrameType::CONTROL);
    EXPECT_EQ(get_frame_type(Frame{fragment_frame}), FrameType::FRAGMENT);
    EXPECT_EQ(get_frame_type(Frame{handshake_frame}), FrameType::HANDSHAKE);
    EXPECT_EQ(get_frame_type(Frame{rotate_frame}), FrameType::SESSION_ROTATE);
}

TEST_F(PacketTest, PacketBuilderDataFrame) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    DataFrame frame;
    frame.sequence_number = 1;
    frame.payload = {0x48, 0x65, 0x6c, 0x6c, 0x6f};

    EXPECT_TRUE(builder.add_frame(frame));
    auto packet = builder.build(1);

    EXPECT_FALSE(packet.empty());
    EXPECT_GE(packet.size(), PacketHeader::SIZE + crypto::POLY1305_TAG_SIZE);
}

TEST_F(PacketTest, PacketBuilderMultipleFrames) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    DataFrame data1, data2;
    data1.sequence_number = 1;
    data1.payload = {0x01, 0x02, 0x03};
    data2.sequence_number = 2;
    data2.payload = {0x04, 0x05, 0x06};

    EXPECT_TRUE(builder.add_frame(data1));
    EXPECT_TRUE(builder.add_frame(data2));
    auto packet = builder.build(1);

    EXPECT_FALSE(packet.empty());
}

TEST_F(PacketTest, PacketBuilderReset) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    DataFrame frame;
    frame.sequence_number = 1;
    frame.payload = {0x01, 0x02};

    builder.add_frame(frame);
    builder.reset();

    // After reset, remaining capacity should be back to max
    size_t initial_capacity = builder.remaining_capacity();
    builder.add_frame(frame);
    size_t after_add = builder.remaining_capacity();

    EXPECT_GT(initial_capacity, after_add);
}

TEST_F(PacketTest, PacketParserDecrypt) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    DataFrame frame;
    frame.sequence_number = 42;
    frame.payload = {0x48, 0x65, 0x6c, 0x6c, 0x6f};

    builder.add_frame(frame);
    auto packet = builder.build(1);

    PacketParser parser;
    parser.set_decryption_key(key_, nonce_);

    auto parsed = parser.parse(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->session_id, 12345u);
    EXPECT_EQ(parsed->packet_counter, 1u);
    ASSERT_EQ(parsed->frames.size(), 1u);

    auto* data_frame = std::get_if<DataFrame>(&parsed->frames[0]);
    ASSERT_NE(data_frame, nullptr);
    EXPECT_EQ(data_frame->sequence_number, 42u);
    EXPECT_EQ(data_frame->payload, frame.payload);
}

TEST_F(PacketTest, PacketParserWrongKey) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    DataFrame frame;
    frame.sequence_number = 1;
    frame.payload = {0x01, 0x02};

    builder.add_frame(frame);
    auto packet = builder.build(1);

    crypto::SymmetricKey wrong_key;
    crypto::random_bytes(wrong_key);

    PacketParser parser;
    parser.set_decryption_key(wrong_key, nonce_);

    ParseError error;
    auto parsed = parser.parse(packet, &error);

    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(error, ParseError::DECRYPTION_FAILED);
}

TEST_F(PacketTest, PacketParserTamperedPacket) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    DataFrame frame;
    frame.sequence_number = 1;
    frame.payload = {0x01, 0x02, 0x03};

    builder.add_frame(frame);
    auto packet = builder.build(1);

    // Tamper with encrypted payload
    packet[PacketHeader::SIZE + 5] ^= 0xFF;

    PacketParser parser;
    parser.set_decryption_key(key_, nonce_);

    auto parsed = parser.parse(packet);
    EXPECT_FALSE(parsed.has_value());
}

TEST_F(PacketTest, PacketParserTooShort) {
    std::vector<uint8_t> short_packet = {0x01, 0x02, 0x03};

    PacketParser parser;
    parser.set_decryption_key(key_, nonce_);

    ParseError error;
    auto parsed = parser.parse(short_packet, &error);

    EXPECT_FALSE(parsed.has_value());
    EXPECT_EQ(error, ParseError::PACKET_TOO_SHORT);
}

TEST_F(PacketTest, PacketHeaderParsing) {
    std::vector<uint8_t> header(16);
    // Session ID: 0x0102030405060708
    header[0] = 0x01; header[1] = 0x02; header[2] = 0x03; header[3] = 0x04;
    header[4] = 0x05; header[5] = 0x06; header[6] = 0x07; header[7] = 0x08;
    // Packet counter: 0x1112131415161718
    header[8] = 0x11; header[9] = 0x12; header[10] = 0x13; header[11] = 0x14;
    header[12] = 0x15; header[13] = 0x16; header[14] = 0x17; header[15] = 0x18;

    auto parsed = PacketParser::parse_header(header);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->session_id, 0x0102030405060708ULL);
    EXPECT_EQ(parsed->packet_counter, 0x1112131415161718ULL);
}

TEST_F(PacketTest, AckFrameRoundTrip) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    AckFrame frame;
    frame.ack_number = 100;
    frame.bitmap = 0xFF00FF00;
    frame.recv_window = 65536;

    builder.add_frame(frame);
    auto packet = builder.build(1);

    PacketParser parser;
    parser.set_decryption_key(key_, nonce_);

    auto parsed = parser.parse(packet);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->frames.size(), 1u);

    auto* ack = std::get_if<AckFrame>(&parsed->frames[0]);
    ASSERT_NE(ack, nullptr);
    EXPECT_EQ(ack->ack_number, frame.ack_number);
    EXPECT_EQ(ack->bitmap, frame.bitmap);
    EXPECT_EQ(ack->recv_window, frame.recv_window);
}

TEST_F(PacketTest, ControlFrameRoundTrip) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    ControlFrame frame;
    frame.type = ControlFrame::Type::PING;
    frame.timestamp = 1234567890;
    frame.data = {0x01, 0x02, 0x03};

    builder.add_frame(frame);
    auto packet = builder.build(1);

    PacketParser parser;
    parser.set_decryption_key(key_, nonce_);

    auto parsed = parser.parse(packet);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->frames.size(), 1u);

    auto* ctrl = std::get_if<ControlFrame>(&parsed->frames[0]);
    ASSERT_NE(ctrl, nullptr);
    EXPECT_EQ(ctrl->type, frame.type);
    EXPECT_EQ(ctrl->timestamp, frame.timestamp);
    EXPECT_EQ(ctrl->data, frame.data);
}

TEST_F(PacketTest, FragmentFrameRoundTrip) {
    PacketBuilder builder;
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    FragmentFrame frame;
    frame.message_id = 42;
    frame.fragment_index = 3;
    frame.total_fragments = 10;
    frame.payload = {0x01, 0x02, 0x03, 0x04};

    builder.add_frame(frame);
    auto packet = builder.build(1);

    PacketParser parser;
    parser.set_decryption_key(key_, nonce_);

    auto parsed = parser.parse(packet);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->frames.size(), 1u);

    auto* frag = std::get_if<FragmentFrame>(&parsed->frames[0]);
    ASSERT_NE(frag, nullptr);
    EXPECT_EQ(frag->message_id, frame.message_id);
    EXPECT_EQ(frag->fragment_index, frame.fragment_index);
    EXPECT_EQ(frag->total_fragments, frame.total_fragments);
    EXPECT_EQ(frag->payload, frame.payload);
}

TEST_F(PacketTest, FrameSizeCalculation) {
    DataFrame data;
    data.sequence_number = 1;
    data.payload = {0x01, 0x02, 0x03, 0x04, 0x05};

    size_t expected = FrameHeader::SIZE + 8 + 5;  // header + seq + payload
    EXPECT_EQ(PacketBuilder::frame_size(data), expected);
}

TEST_F(PacketTest, MtuLimitRespected) {
    constexpr size_t MTU = 500;
    PacketBuilder builder(MTU);
    builder.set_encryption_key(key_, nonce_);
    builder.set_session_id(12345);

    // Add a small frame
    DataFrame frame;
    frame.sequence_number = 1;
    frame.payload.resize(100, 0x42);

    EXPECT_TRUE(builder.add_frame(frame));

    // Try to add a frame that would exceed MTU
    DataFrame large_frame;
    large_frame.sequence_number = 2;
    large_frame.payload.resize(MTU, 0x43);

    EXPECT_FALSE(builder.add_frame(large_frame));
}

}  // namespace
}  // namespace veil::packet
