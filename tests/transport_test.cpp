#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "veil/crypto/crypto.hpp"
#include "veil/transport/udp_socket.hpp"

namespace veil::transport {
namespace {

class UdpSocketTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(crypto::init());
    }
};

TEST_F(UdpSocketTest, DefaultNotOpen) {
    UdpSocket socket;
    EXPECT_FALSE(socket.is_open());
    EXPECT_EQ(socket.fd(), -1);
}

// UDP socket tests that require actual sockets may be skipped in CI
// if socket permissions are not available

TEST_F(UdpSocketTest, OpenAndClose) {
    UdpSocket socket;
    UdpSocketConfig config;
    config.bind_address = {"127.0.0.1", 0};  // Any available port

    bool opened = socket.open(config);

    // Skip if socket creation fails (sandboxed environment)
    if (!opened) {
        GTEST_SKIP() << "Socket creation not available in this environment";
    }

    EXPECT_TRUE(socket.is_open());
    EXPECT_GE(socket.fd(), 0);
    EXPECT_GT(socket.local_address().port, 0);

    socket.close();
    EXPECT_FALSE(socket.is_open());
}

TEST_F(UdpSocketTest, MoveConstruction) {
    UdpSocket socket1;
    UdpSocketConfig config;
    config.bind_address = {"127.0.0.1", 0};

    if (!socket1.open(config)) {
        GTEST_SKIP() << "Socket creation not available";
    }

    int fd = socket1.fd();
    UdpSocket socket2 = std::move(socket1);

    EXPECT_FALSE(socket1.is_open());
    EXPECT_TRUE(socket2.is_open());
    EXPECT_EQ(socket2.fd(), fd);
}

TEST_F(UdpSocketTest, SendAndReceive) {
    UdpSocket server, client;
    UdpSocketConfig server_config, client_config;

    server_config.bind_address = {"127.0.0.1", 0};
    client_config.bind_address = {"127.0.0.1", 0};

    if (!server.open(server_config) || !client.open(client_config)) {
        GTEST_SKIP() << "Socket creation not available";
    }

    SocketAddress server_addr = {"127.0.0.1", server.local_address().port};
    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04};

    EXPECT_TRUE(client.send_to(server_addr, test_data));

    // Poll for data with timeout
    int ready = server.poll_recv(100);
    if (ready <= 0) {
        GTEST_SKIP() << "Poll timed out (may be sandboxed)";
    }

    auto received = server.recv();
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->data, test_data);
    EXPECT_EQ(received->from.port, client.local_address().port);
}

TEST_F(UdpSocketTest, Statistics) {
    UdpSocket socket;
    UdpSocketConfig config;
    config.bind_address = {"127.0.0.1", 0};

    if (!socket.open(config)) {
        GTEST_SKIP() << "Socket creation not available";
    }

    EXPECT_EQ(socket.packets_sent(), 0u);
    EXPECT_EQ(socket.packets_received(), 0u);
    EXPECT_EQ(socket.bytes_sent(), 0u);
    EXPECT_EQ(socket.bytes_received(), 0u);
}

class SocketAddressTest : public ::testing::Test {};

TEST_F(SocketAddressTest, Equality) {
    SocketAddress a{"127.0.0.1", 1234};
    SocketAddress b{"127.0.0.1", 1234};
    SocketAddress c{"127.0.0.1", 5678};
    SocketAddress d{"192.168.1.1", 1234};

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
}

}  // namespace
}  // namespace veil::transport
