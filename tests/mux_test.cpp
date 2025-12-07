#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "veil/crypto/crypto.hpp"
#include "veil/mux/replay_window.hpp"
#include "veil/mux/session_rotator.hpp"
#include "veil/mux/rate_limiter.hpp"
#include "veil/mux/ack_bitmap.hpp"
#include "veil/mux/reorder_buffer.hpp"
#include "veil/mux/fragment_assembler.hpp"
#include "veil/mux/retransmission.hpp"

namespace veil::mux {
namespace {

class ReplayWindowTest : public ::testing::Test {
protected:
    ReplayWindow window;
};

TEST_F(ReplayWindowTest, FirstPacketAccepted) {
    EXPECT_TRUE(window.check(1));
    window.update(1);
    EXPECT_EQ(window.highest(), 1u);
}

TEST_F(ReplayWindowTest, DuplicateRejected) {
    window.check_and_update(1);
    EXPECT_FALSE(window.check(1));
}

TEST_F(ReplayWindowTest, InOrderAccepted) {
    for (uint64_t i = 1; i <= 10; ++i) {
        EXPECT_TRUE(window.check_and_update(i));
    }
    EXPECT_EQ(window.highest(), 10u);
}

TEST_F(ReplayWindowTest, OutOfOrderWithinWindow) {
    window.check_and_update(10);

    // Packets 1-9 are within window (64 packets)
    for (uint64_t i = 1; i < 10; ++i) {
        EXPECT_TRUE(window.check_and_update(i)) << "seq " << i;
    }
}

TEST_F(ReplayWindowTest, TooOldRejected) {
    window.check_and_update(100);

    // Packet 1 is outside the 64-packet window
    EXPECT_FALSE(window.check(1));
}

TEST_F(ReplayWindowTest, WindowSliding) {
    for (uint64_t i = 1; i <= 100; ++i) {
        EXPECT_TRUE(window.check_and_update(i));
    }

    // Old packets should be rejected
    EXPECT_FALSE(window.check(1));
    EXPECT_FALSE(window.check(35));

    // Recent packets still rejected as duplicates
    EXPECT_FALSE(window.check(99));
    EXPECT_FALSE(window.check(100));
}

TEST_F(ReplayWindowTest, Reset) {
    window.check_and_update(100);
    window.reset();

    EXPECT_TRUE(window.check(1));
}

class RateLimiterTest : public ::testing::Test {
protected:
    RateLimiterConfig config{
        .packets_per_second = 100,
        .bytes_per_second = 10000,
        .burst_packets = 10,
        .burst_bytes = 1000
    };
    RateLimiter limiter{config};
};

TEST_F(RateLimiterTest, InitialBurstAllowed) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(limiter.try_consume(50)) << "packet " << i;
    }
}

TEST_F(RateLimiterTest, ExcessivePacketsBlocked) {
    for (int i = 0; i < 10; ++i) {
        limiter.try_consume(50);
    }
    EXPECT_FALSE(limiter.try_consume(50));
}

TEST_F(RateLimiterTest, RefillRestoresTokens) {
    for (int i = 0; i < 10; ++i) {
        limiter.try_consume(50);
    }

    // Simulate 100ms passing
    limiter.refill(100);

    // Should have some tokens now
    EXPECT_TRUE(limiter.check(50));
}

TEST_F(RateLimiterTest, LargePacketExceedsByteLimit) {
    EXPECT_TRUE(limiter.check(500));
    EXPECT_FALSE(limiter.check(2000));  // Exceeds burst_bytes
}

TEST_F(RateLimiterTest, StatisticsTracked) {
    limiter.try_consume(100);
    limiter.try_consume(100);

    for (int i = 0; i < 10; ++i) {
        limiter.try_consume(100);
    }

    EXPECT_GT(limiter.packets_dropped(), 0u);
}

class SessionRotatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        crypto::init();
    }
};

TEST_F(SessionRotatorTest, InitialSessionId) {
    SessionRotator rotator;
    EXPECT_NE(rotator.current_session_id(), 0u);
}

TEST_F(SessionRotatorTest, ManualRotation) {
    SessionRotator rotator;
    auto initial = rotator.current_session_id();

    rotator.rotate();
    EXPECT_NE(rotator.current_session_id(), initial);
}

TEST_F(SessionRotatorTest, RotationCallback) {
    SessionRotator rotator;
    uint64_t new_id = 0;

    rotator.set_rotation_callback([&new_id](uint64_t id) {
        new_id = id;
    });

    rotator.rotate();
    EXPECT_EQ(new_id, rotator.current_session_id());
}

TEST_F(SessionRotatorTest, PacketCountTrigger) {
    SessionRotatorConfig config;
    config.packets_per_session = 10;
    config.bytes_per_session = 1ULL << 30;
    config.seconds_per_session = 3600;

    SessionRotator rotator(config);

    for (int i = 0; i < 10; ++i) {
        rotator.on_packet_sent(100);
    }

    EXPECT_TRUE(rotator.should_rotate());
}

class AckBitmapTest : public ::testing::Test {
protected:
    AckBitmap bitmap;
};

TEST_F(AckBitmapTest, ContiguousReceive) {
    for (uint64_t i = 1; i <= 10; ++i) {
        bitmap.mark_received(i);
    }

    EXPECT_EQ(bitmap.get_ack_number(), 10u);
    EXPECT_EQ(bitmap.get_bitmap(), 0u);
}

TEST_F(AckBitmapTest, OutOfOrderReceive) {
    bitmap.mark_received(1);
    bitmap.mark_received(3);  // Gap at 2
    bitmap.mark_received(5);  // Gap at 4

    EXPECT_EQ(bitmap.get_ack_number(), 1u);
    uint64_t expected_bitmap = (1ULL << 1) | (1ULL << 3);  // bits for 3 and 5
    EXPECT_EQ(bitmap.get_bitmap(), expected_bitmap);
}

TEST_F(AckBitmapTest, GapFilling) {
    bitmap.mark_received(1);
    bitmap.mark_received(3);
    bitmap.mark_received(2);  // Fill gap

    EXPECT_EQ(bitmap.get_ack_number(), 3u);
    EXPECT_EQ(bitmap.get_bitmap(), 0u);
}

TEST_F(AckBitmapTest, IsReceived) {
    bitmap.mark_received(1);
    bitmap.mark_received(3);

    EXPECT_TRUE(bitmap.is_received(1));
    EXPECT_FALSE(bitmap.is_received(2));
    EXPECT_TRUE(bitmap.is_received(3));
    EXPECT_FALSE(bitmap.is_received(4));
}

class ReorderBufferTest : public ::testing::Test {
protected:
    ReorderBufferConfig config{
        .max_buffered_packets = 16,
        .max_buffered_bytes = 65536,
        .max_delay_ms = 1000
    };
    ReorderBuffer buffer{config};
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> delivered;

    void SetUp() override {
        buffer.set_deliver_callback([this](uint64_t seq, std::vector<uint8_t> data) {
            delivered.emplace_back(seq, std::move(data));
        });
    }
};

TEST_F(ReorderBufferTest, InOrderDelivery) {
    buffer.insert(1, {0x01}, 0);
    buffer.insert(2, {0x02}, 0);
    buffer.insert(3, {0x03}, 0);
    buffer.deliver();

    ASSERT_EQ(delivered.size(), 3u);
    EXPECT_EQ(delivered[0].first, 1u);
    EXPECT_EQ(delivered[1].first, 2u);
    EXPECT_EQ(delivered[2].first, 3u);
}

TEST_F(ReorderBufferTest, OutOfOrderBuffered) {
    buffer.insert(2, {0x02}, 0);
    buffer.insert(3, {0x03}, 0);
    buffer.deliver();

    EXPECT_TRUE(delivered.empty());
    EXPECT_EQ(buffer.buffered_count(), 2u);

    buffer.insert(1, {0x01}, 0);
    buffer.deliver();

    ASSERT_EQ(delivered.size(), 3u);
}

TEST_F(ReorderBufferTest, DuplicateRejected) {
    EXPECT_TRUE(buffer.insert(1, {0x01}, 0));
    EXPECT_FALSE(buffer.insert(1, {0x01}, 0));
}

TEST_F(ReorderBufferTest, TimeoutFlush) {
    buffer.insert(2, {0x02}, 0);
    buffer.insert(3, {0x03}, 0);

    // Flush at timeout (1000ms later)
    buffer.flush(1001);

    ASSERT_EQ(delivered.size(), 2u);
}

class FragmentAssemblerTest : public ::testing::Test {
protected:
    FragmentAssemblerConfig config{
        .max_pending_messages = 8,
        .max_fragments_per_message = 16,
        .max_message_size = 65536,
        .fragment_timeout_ms = 1000
    };
    FragmentAssembler assembler{config};
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> assembled;

    void SetUp() override {
        assembler.set_assemble_callback([this](uint32_t id, std::vector<uint8_t> data) {
            assembled.emplace_back(id, std::move(data));
        });
    }
};

TEST_F(FragmentAssemblerTest, SingleFragmentMessage) {
    std::vector<uint8_t> payload = {0x01, 0x02, 0x03};
    assembler.add_fragment(1, 0, 1, payload, 0);

    ASSERT_EQ(assembled.size(), 1u);
    EXPECT_EQ(assembled[0].first, 1u);
    EXPECT_EQ(assembled[0].second, payload);
}

TEST_F(FragmentAssemblerTest, MultiFragmentInOrder) {
    std::vector<uint8_t> p1 = {0x01, 0x02};
    std::vector<uint8_t> p2 = {0x03, 0x04};
    std::vector<uint8_t> p3 = {0x05, 0x06};
    assembler.add_fragment(1, 0, 3, p1, 0);
    assembler.add_fragment(1, 1, 3, p2, 0);
    assembler.add_fragment(1, 2, 3, p3, 0);

    ASSERT_EQ(assembled.size(), 1u);
    std::vector<uint8_t> expected = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    EXPECT_EQ(assembled[0].second, expected);
}

TEST_F(FragmentAssemblerTest, MultiFragmentOutOfOrder) {
    std::vector<uint8_t> p1 = {0x01, 0x02};
    std::vector<uint8_t> p2 = {0x03, 0x04};
    std::vector<uint8_t> p3 = {0x05, 0x06};
    assembler.add_fragment(1, 2, 3, p3, 0);
    assembler.add_fragment(1, 0, 3, p1, 0);
    assembler.add_fragment(1, 1, 3, p2, 0);

    ASSERT_EQ(assembled.size(), 1u);
    std::vector<uint8_t> expected = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    EXPECT_EQ(assembled[0].second, expected);
}

TEST_F(FragmentAssemblerTest, DuplicateFragmentRejected) {
    std::vector<uint8_t> p = {0x01};
    EXPECT_TRUE(assembler.add_fragment(1, 0, 2, p, 0));
    EXPECT_FALSE(assembler.add_fragment(1, 0, 2, p, 0));
}

TEST_F(FragmentAssemblerTest, TimeoutCleanup) {
    std::vector<uint8_t> p1 = {0x01};
    std::vector<uint8_t> p2 = {0x02};
    assembler.add_fragment(1, 0, 3, p1, 0);
    assembler.add_fragment(1, 1, 3, p2, 0);
    // Missing fragment 2

    // Cleanup expired
    size_t cleaned = assembler.cleanup_expired(2000);
    EXPECT_EQ(cleaned, 1u);
    EXPECT_EQ(assembler.pending_messages(), 0u);
}

TEST_F(FragmentAssemblerTest, MismatchedTotalRejected) {
    std::vector<uint8_t> p1 = {0x01};
    std::vector<uint8_t> p2 = {0x02};
    assembler.add_fragment(1, 0, 3, p1, 0);
    // Try to add fragment with different total
    EXPECT_FALSE(assembler.add_fragment(1, 1, 4, p2, 0));
}

class RetransmissionTest : public ::testing::Test {
protected:
    RetransmissionConfig config{
        .initial_rto_ms = 100,
        .min_rto_ms = 50,
        .max_rto_ms = 1000,
        .max_retries = 3,
        .max_unacked_packets = 16,
        .max_unacked_bytes = 65536,
        .rtt_alpha = 0.125,
        .rtt_beta = 0.25
    };
    RetransmissionManager rtx{config};
    std::vector<uint64_t> retransmitted;
    std::vector<uint64_t> dropped;

    void SetUp() override {
        rtx.set_retransmit_callback([this](uint64_t seq, const auto&) {
            retransmitted.push_back(seq);
        });
        rtx.set_drop_callback([this](uint64_t seq) {
            dropped.push_back(seq);
        });
    }
};

TEST_F(RetransmissionTest, RegisterPacket) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    EXPECT_TRUE(rtx.register_packet(1, data, 0));
    EXPECT_EQ(rtx.unacked_count(), 1u);
    EXPECT_EQ(rtx.unacked_bytes(), 3u);
}

TEST_F(RetransmissionTest, AckRemovesPacket) {
    rtx.register_packet(1, {0x01}, 0);
    rtx.ack_packet(1, 50);

    EXPECT_EQ(rtx.unacked_count(), 0u);
}

TEST_F(RetransmissionTest, TimeoutRetransmits) {
    rtx.register_packet(1, {0x01}, 0);

    rtx.retransmit_expired(150);  // After initial RTO

    ASSERT_EQ(retransmitted.size(), 1u);
    EXPECT_EQ(retransmitted[0], 1u);
}

TEST_F(RetransmissionTest, MaxRetriesDrop) {
    rtx.register_packet(1, {0x01}, 0);

    // With initial_rto=100, after each retransmit RTO doubles
    // Need to trigger enough timeouts to exceed max_retries=3
    // Time points that trigger retransmits:
    // - t=100: first timeout, retransmit_count=1, RTO->200
    // - t=300: second timeout (100+200), retransmit_count=2, RTO->400
    // - t=700: third timeout (300+400), retransmit_count=3, RTO->800
    // - t=1500: fourth timeout (700+800), drop (count >= max_retries)
    rtx.retransmit_expired(100);   // First retransmit
    rtx.retransmit_expired(300);   // Second retransmit
    rtx.retransmit_expired(700);   // Third retransmit
    rtx.retransmit_expired(1500);  // Should drop

    EXPECT_FALSE(dropped.empty());
}

TEST_F(RetransmissionTest, SackProcessing) {
    rtx.register_packet(1, {0x01}, 0);
    rtx.register_packet(2, {0x02}, 0);
    rtx.register_packet(3, {0x03}, 0);
    rtx.register_packet(4, {0x04}, 0);

    // ACK 2, SACK bit for 4 (offset 1 from ack_number)
    rtx.process_sack(2, 0x02, 50);

    EXPECT_EQ(rtx.unacked_count(), 1u);  // Only 3 remains
}

TEST_F(RetransmissionTest, RttEstimation) {
    rtx.register_packet(1, {0x01}, 0);
    rtx.ack_packet(1, 50);

    // RTT should be around 50ms
    EXPECT_GT(rtx.get_srtt_ms(), 0u);
    EXPECT_LT(rtx.get_rto_ms(), config.max_rto_ms);
}

TEST_F(RetransmissionTest, CanSendLimit) {
    for (int i = 1; i <= 16; ++i) {
        EXPECT_TRUE(rtx.register_packet(i, {0x01}, 0));
    }

    // Should be at limit
    EXPECT_FALSE(rtx.can_send(1));
}

}  // namespace
}  // namespace veil::mux
