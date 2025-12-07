#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "veil/crypto/crypto.hpp"
#include "veil/handshake/handshake.hpp"

namespace veil::handshake {
namespace {

class HandshakeTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(crypto::init());
    }
};

TEST_F(HandshakeTest, InitialState) {
    Handshake hs;
    EXPECT_EQ(hs.state(), HandshakeState::IDLE);
    EXPECT_EQ(hs.last_error(), HandshakeError::NONE);
}

TEST_F(HandshakeTest, InitiateChangesState) {
    Handshake hs;
    std::vector<uint8_t> sent_msg;

    hs.set_send_callback([&sent_msg](std::vector<uint8_t> msg) {
        sent_msg = std::move(msg);
    });

    EXPECT_TRUE(hs.initiate());
    EXPECT_EQ(hs.state(), HandshakeState::INIT_SENT);
    EXPECT_FALSE(sent_msg.empty());
}

TEST_F(HandshakeTest, DoubleInitiateFails) {
    Handshake hs;
    hs.initiate();
    EXPECT_FALSE(hs.initiate());
    EXPECT_EQ(hs.last_error(), HandshakeError::INTERNAL_ERROR);
}

TEST_F(HandshakeTest, FullHandshake) {
    HandshakeConfig config;
    crypto::random_bytes(config.psk);

    Handshake initiator(config);
    Handshake responder(config);

    std::vector<uint8_t> msg_to_responder;
    std::vector<uint8_t> msg_to_initiator;

    // Set current time for both
    uint64_t now = 1234567890;
    initiator.set_current_time(now);
    responder.set_current_time(now);

    // Initiator sends INIT
    initiator.set_send_callback([&msg_to_responder](std::vector<uint8_t> msg) {
        msg_to_responder = std::move(msg);
    });
    EXPECT_TRUE(initiator.initiate());

    // Responder processes INIT, sends RESPONSE
    responder.set_send_callback([&msg_to_initiator](std::vector<uint8_t> msg) {
        msg_to_initiator = std::move(msg);
    });
    EXPECT_FALSE(responder.process_message(msg_to_responder));
    EXPECT_EQ(responder.state(), HandshakeState::RESPONSE_SENT);

    // Initiator processes RESPONSE, sends FINISH
    msg_to_responder.clear();
    initiator.set_send_callback([&msg_to_responder](std::vector<uint8_t> msg) {
        msg_to_responder = std::move(msg);
    });
    EXPECT_TRUE(initiator.process_message(msg_to_initiator));
    EXPECT_EQ(initiator.state(), HandshakeState::COMPLETE);

    // Responder processes FINISH
    EXPECT_TRUE(responder.process_message(msg_to_responder));
    EXPECT_EQ(responder.state(), HandshakeState::COMPLETE);

    // Both should have results
    auto init_result = initiator.result();
    auto resp_result = responder.result();

    ASSERT_TRUE(init_result.has_value());
    ASSERT_TRUE(resp_result.has_value());

    // Session IDs should match
    EXPECT_EQ(init_result->session_id, resp_result->session_id);

    // Keys should be symmetric
    EXPECT_EQ(init_result->session_keys.send_key, resp_result->session_keys.recv_key);
    EXPECT_EQ(init_result->session_keys.recv_key, resp_result->session_keys.send_key);
}

TEST_F(HandshakeTest, TimestampOutOfRange) {
    Handshake initiator;
    Handshake responder;

    initiator.set_current_time(1000);
    responder.set_current_time(2000);  // 1000 seconds difference, > 60 default

    std::vector<uint8_t> msg;
    initiator.set_send_callback([&msg](std::vector<uint8_t> m) {
        msg = std::move(m);
    });
    initiator.initiate();

    EXPECT_FALSE(responder.process_message(msg));
    EXPECT_EQ(responder.last_error(), HandshakeError::TIMESTAMP_OUT_OF_RANGE);
}

TEST_F(HandshakeTest, HmacMismatch) {
    HandshakeConfig config1, config2;
    crypto::random_bytes(config1.psk);
    crypto::random_bytes(config2.psk);  // Different PSK

    Handshake initiator(config1);
    Handshake responder(config2);

    uint64_t now = 1234567890;
    initiator.set_current_time(now);
    responder.set_current_time(now);

    std::vector<uint8_t> msg;
    initiator.set_send_callback([&msg](std::vector<uint8_t> m) {
        msg = std::move(m);
    });
    initiator.initiate();

    EXPECT_FALSE(responder.process_message(msg));
    EXPECT_EQ(responder.last_error(), HandshakeError::HMAC_VERIFICATION_FAILED);
}

TEST_F(HandshakeTest, TruncatedMessage) {
    Handshake initiator;
    Handshake responder;

    uint64_t now = 1234567890;
    initiator.set_current_time(now);
    responder.set_current_time(now);

    std::vector<uint8_t> msg;
    initiator.set_send_callback([&msg](std::vector<uint8_t> m) {
        msg = std::move(m);
    });
    initiator.initiate();

    // Truncate message
    msg.resize(10);

    EXPECT_FALSE(responder.process_message(msg));
    EXPECT_EQ(responder.last_error(), HandshakeError::INVALID_MESSAGE);
}

TEST_F(HandshakeTest, Reset) {
    Handshake hs;
    hs.initiate();

    hs.reset();
    EXPECT_EQ(hs.state(), HandshakeState::IDLE);
    EXPECT_EQ(hs.last_error(), HandshakeError::NONE);

    // Should be able to initiate again
    EXPECT_TRUE(hs.initiate());
}

TEST_F(HandshakeTest, ResultNotAvailableBeforeComplete) {
    Handshake hs;
    EXPECT_FALSE(hs.result().has_value());

    hs.initiate();
    EXPECT_FALSE(hs.result().has_value());
}

}  // namespace
}  // namespace veil::handshake
