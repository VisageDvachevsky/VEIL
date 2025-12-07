#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "veil/crypto/crypto.hpp"
#include "veil/packet/packet_builder.hpp"
#include "veil/packet/packet_parser.hpp"
#include "veil/mux/replay_window.hpp"
#include "veil/mux/ack_bitmap.hpp"
#include "veil/mux/rate_limiter.hpp"
#include "veil/handshake/handshake.hpp"

namespace veil {
namespace {

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(crypto::init());
    }
};

// Test complete packet flow: build -> encrypt -> decrypt -> parse
TEST_F(IntegrationTest, PacketRoundTrip) {
    crypto::SymmetricKey key;
    crypto::Nonce nonce;
    crypto::random_bytes(key);
    crypto::random_bytes(nonce);

    // Build packet
    packet::PacketBuilder builder;
    builder.set_encryption_key(key, nonce);
    builder.set_session_id(0xDEADBEEF);

    packet::DataFrame data_frame;
    data_frame.sequence_number = 42;
    data_frame.payload = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
    builder.add_frame(data_frame);

    packet::AckFrame ack_frame;
    ack_frame.ack_number = 10;
    ack_frame.bitmap = 0xFF;
    ack_frame.recv_window = 65536;
    builder.add_frame(ack_frame);

    auto packet = builder.build(1);

    // Parse packet
    packet::PacketParser parser;
    parser.set_decryption_key(key, nonce);

    auto parsed = parser.parse(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->session_id, 0xDEADBEEF);
    EXPECT_EQ(parsed->packet_counter, 1u);
    ASSERT_EQ(parsed->frames.size(), 2u);

    // Verify data frame
    auto* df = std::get_if<packet::DataFrame>(&parsed->frames[0]);
    ASSERT_NE(df, nullptr);
    EXPECT_EQ(df->sequence_number, 42u);
    EXPECT_EQ(df->payload, data_frame.payload);

    // Verify ack frame
    auto* af = std::get_if<packet::AckFrame>(&parsed->frames[1]);
    ASSERT_NE(af, nullptr);
    EXPECT_EQ(af->ack_number, 10u);
    EXPECT_EQ(af->bitmap, 0xFFu);
}

// Test replay protection integration
TEST_F(IntegrationTest, ReplayProtection) {
    mux::ReplayWindow window;

    // Simulate receiving packets in order
    for (uint64_t i = 1; i <= 100; ++i) {
        EXPECT_TRUE(window.check_and_update(i)) << "seq " << i;
    }

    // Replayed packet should be rejected
    EXPECT_FALSE(window.check(50));

    // New packet should be accepted
    EXPECT_TRUE(window.check_and_update(101));

    // Very old packet should be rejected
    EXPECT_FALSE(window.check(1));
}

// Test ACK bitmap with selective acknowledgment
TEST_F(IntegrationTest, SelectiveAck) {
    mux::AckBitmap sender_tracker;
    mux::AckBitmap receiver_tracker;

    // Receiver gets packets 1, 2, 4, 5, 7 (missing 3, 6)
    receiver_tracker.mark_received(1);
    receiver_tracker.mark_received(2);
    receiver_tracker.mark_received(4);
    receiver_tracker.mark_received(5);
    receiver_tracker.mark_received(7);

    uint64_t ack_num = receiver_tracker.get_ack_number();
    uint64_t bitmap = receiver_tracker.get_bitmap();

    EXPECT_EQ(ack_num, 2u);  // Highest contiguous
    EXPECT_TRUE(bitmap & (1 << 1));  // Bit for seq 4 (offset 1 from ack+1)
    EXPECT_TRUE(bitmap & (1 << 2));  // Bit for seq 5
    EXPECT_TRUE(bitmap & (1 << 4));  // Bit for seq 7

    // Sender processes ACK
    auto acked = sender_tracker.process_ack(ack_num, bitmap);
    EXPECT_TRUE(std::find(acked.begin(), acked.end(), 1) != acked.end());
    EXPECT_TRUE(std::find(acked.begin(), acked.end(), 2) != acked.end());
    EXPECT_TRUE(std::find(acked.begin(), acked.end(), 4) != acked.end());
    EXPECT_TRUE(std::find(acked.begin(), acked.end(), 5) != acked.end());
    EXPECT_TRUE(std::find(acked.begin(), acked.end(), 7) != acked.end());
    EXPECT_TRUE(std::find(acked.begin(), acked.end(), 3) == acked.end());  // Not acked
    EXPECT_TRUE(std::find(acked.begin(), acked.end(), 6) == acked.end());  // Not acked
}

// Test rate limiting
TEST_F(IntegrationTest, RateLimiting) {
    mux::RateLimiterConfig config;
    config.packets_per_second = 100;
    config.bytes_per_second = 10000;
    config.burst_packets = 5;
    config.burst_bytes = 500;

    mux::RateLimiter limiter(config);

    // Burst should be allowed
    int allowed = 0;
    for (int i = 0; i < 10; ++i) {
        if (limiter.try_consume(50)) {
            ++allowed;
        }
    }
    EXPECT_EQ(allowed, 5);

    // After refill, more should be allowed
    limiter.refill(100);  // 100ms = 10 packets worth

    int allowed_after = 0;
    for (int i = 0; i < 20; ++i) {
        if (limiter.try_consume(50)) {
            ++allowed_after;
        }
    }
    EXPECT_GT(allowed_after, 0);
}

// Test handshake and session key derivation
TEST_F(IntegrationTest, HandshakeToSession) {
    handshake::HandshakeConfig config;
    crypto::random_bytes(config.psk);

    handshake::Handshake client(config);
    handshake::Handshake server(config);

    uint64_t now = 1234567890;
    client.set_current_time(now);
    server.set_current_time(now);

    std::vector<uint8_t> to_server, to_client;

    // Client -> Server: INIT
    client.set_send_callback([&to_server](auto msg) { to_server = std::move(msg); });
    client.initiate();

    // Server -> Client: RESPONSE
    server.set_send_callback([&to_client](auto msg) { to_client = std::move(msg); });
    server.process_message(to_server);

    // Client -> Server: FINISH
    to_server.clear();
    client.set_send_callback([&to_server](auto msg) { to_server = std::move(msg); });
    client.process_message(to_client);

    // Server processes FINISH
    server.process_message(to_server);

    ASSERT_EQ(client.state(), handshake::HandshakeState::COMPLETE);
    ASSERT_EQ(server.state(), handshake::HandshakeState::COMPLETE);

    auto client_result = client.result();
    auto server_result = server.result();

    ASSERT_TRUE(client_result.has_value());
    ASSERT_TRUE(server_result.has_value());

    // Now use the session keys for packet encryption
    packet::PacketBuilder client_builder;
    client_builder.set_encryption_key(client_result->session_keys.send_key,
                                       client_result->session_keys.send_nonce_base);
    client_builder.set_session_id(0x12345678);

    packet::DataFrame frame;
    frame.sequence_number = 1;
    frame.payload = {'T', 'e', 's', 't'};
    client_builder.add_frame(frame);

    auto encrypted = client_builder.build(1);

    // Server decrypts with matching recv key
    packet::PacketParser server_parser;
    server_parser.set_decryption_key(server_result->session_keys.recv_key,
                                      server_result->session_keys.recv_nonce_base);

    auto decrypted = server_parser.parse(encrypted);
    ASSERT_TRUE(decrypted.has_value());
    ASSERT_EQ(decrypted->frames.size(), 1u);

    auto* df = std::get_if<packet::DataFrame>(&decrypted->frames[0]);
    ASSERT_NE(df, nullptr);
    EXPECT_EQ(df->payload, frame.payload);
}

// Stress test: many packets with different frame types
TEST_F(IntegrationTest, ManyPackets) {
    crypto::SymmetricKey key;
    crypto::Nonce nonce;
    crypto::random_bytes(key);
    crypto::random_bytes(nonce);

    for (int i = 0; i < 1000; ++i) {
        packet::PacketBuilder builder;
        builder.set_encryption_key(key, nonce);
        builder.set_session_id(i);

        packet::DataFrame frame;
        frame.sequence_number = i;
        frame.payload.resize(100, static_cast<uint8_t>(i & 0xFF));
        builder.add_frame(frame);

        auto packet = builder.build(i);

        packet::PacketParser parser;
        parser.set_decryption_key(key, nonce);

        auto parsed = parser.parse(packet);
        ASSERT_TRUE(parsed.has_value()) << "Failed at packet " << i;
        ASSERT_EQ(parsed->frames.size(), 1u);

        auto* df = std::get_if<packet::DataFrame>(&parsed->frames[0]);
        ASSERT_NE(df, nullptr);
        EXPECT_EQ(df->sequence_number, static_cast<uint64_t>(i));
    }
}

}  // namespace
}  // namespace veil
