#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

#include <CLI/CLI.hpp>
#include <spdlog/spdlog.h>

#include "veil/crypto/crypto.hpp"
#include "veil/transport/transport_session.hpp"
#include "veil/config/config.hpp"
#include "veil/utils/logging.hpp"

using namespace veil;

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    spdlog::info("Received signal {}, shutting down...", sig);
    g_running = false;
}

int main(int argc, char* argv[]) {
    CLI::App app{"VEIL Demo - Encrypted UDP Transport"};

    std::string mode = "client";
    std::string local_host = "127.0.0.1";
    uint16_t local_port = 0;
    std::string remote_host = "127.0.0.1";
    uint16_t remote_port = 12345;
    std::string psk_hex;
    std::string log_level = "info";
    size_t mtu = 1400;
    bool ping_mode = false;
    int ping_interval = 1000;

    app.add_option("-m,--mode", mode, "Mode: client or server")
        ->check(CLI::IsMember({"client", "server"}));
    app.add_option("-b,--bind", local_host, "Local bind address");
    app.add_option("-p,--port", local_port, "Local port (0 = auto)");
    app.add_option("-r,--remote", remote_host, "Remote host");
    app.add_option("--remote-port", remote_port, "Remote port");
    app.add_option("--psk", psk_hex, "Pre-shared key (hex string)");
    app.add_option("-l,--log-level", log_level, "Log level: trace,debug,info,warn,error");
    app.add_option("--mtu", mtu, "Maximum transmission unit");
    app.add_flag("--ping", ping_mode, "Ping mode: send periodic pings");
    app.add_option("--ping-interval", ping_interval, "Ping interval in ms");

    CLI11_PARSE(app, argc, argv);

    // Initialize logging
    utils::init_logging(utils::string_to_log_level(log_level));

    // Initialize crypto
    if (!crypto::init()) {
        spdlog::error("Failed to initialize crypto subsystem");
        return 1;
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Configure session
    transport::TransportSessionConfig config;
    config.local_address = {local_host, local_port};
    config.mtu = mtu;

    // Parse PSK
    if (!psk_hex.empty()) {
        if (psk_hex.size() >= 2 && psk_hex[0] == '0' &&
            (psk_hex[1] == 'x' || psk_hex[1] == 'X')) {
            psk_hex = psk_hex.substr(2);
        }
        size_t len = std::min(psk_hex.size() / 2, config.psk.size());
        for (size_t i = 0; i < len; ++i) {
            config.psk[i] = static_cast<uint8_t>(
                std::stoul(psk_hex.substr(i * 2, 2), nullptr, 16));
        }
    } else {
        // Generate random PSK for demo
        crypto::random_bytes(config.psk);
        spdlog::info("Generated random PSK (share with peer for testing)");
    }

    if (mode == "server") {
        // Server mode: listen for incoming connections
        spdlog::info("Starting server on {}:{}", local_host,
                     local_port == 0 ? remote_port : local_port);

        if (local_port == 0) {
            config.local_address.port = remote_port;
        }

        transport::TransportSession session(config);

        session.set_data_callback([](std::vector<uint8_t> data) {
            spdlog::info("Received {} bytes: {}",
                        data.size(),
                        std::string(data.begin(), data.end()));
        });

        session.set_state_callback([](transport::SessionState state) {
            const char* state_str = "unknown";
            switch (state) {
                case transport::SessionState::DISCONNECTED: state_str = "disconnected"; break;
                case transport::SessionState::HANDSHAKING: state_str = "handshaking"; break;
                case transport::SessionState::CONNECTED: state_str = "connected"; break;
                case transport::SessionState::CLOSING: state_str = "closing"; break;
                case transport::SessionState::CLOSED: state_str = "closed"; break;
            }
            spdlog::info("Session state: {}", state_str);
        });

        session.set_error_callback([](const std::string& error) {
            spdlog::error("Session error: {}", error);
        });

        if (!session.start()) {
            spdlog::error("Failed to start session");
            return 1;
        }

        spdlog::info("Server running. Press Ctrl+C to stop.");

        while (g_running) {
            session.process(100);
        }

        session.stop();

    } else {
        // Client mode: connect to server
        spdlog::info("Connecting to {}:{}", remote_host, remote_port);

        config.peer_address = {remote_host, remote_port};

        transport::TransportSession session(config);

        session.set_data_callback([](std::vector<uint8_t> data) {
            spdlog::info("Received {} bytes: {}",
                        data.size(),
                        std::string(data.begin(), data.end()));
        });

        session.set_state_callback([&session, ping_mode](transport::SessionState state) {
            const char* state_str = "unknown";
            switch (state) {
                case transport::SessionState::DISCONNECTED: state_str = "disconnected"; break;
                case transport::SessionState::HANDSHAKING: state_str = "handshaking"; break;
                case transport::SessionState::CONNECTED: state_str = "connected"; break;
                case transport::SessionState::CLOSING: state_str = "closing"; break;
                case transport::SessionState::CLOSED: state_str = "closed"; break;
            }
            spdlog::info("Session state: {}", state_str);

            if (state == transport::SessionState::CONNECTED && !ping_mode) {
                // Send a test message
                std::string msg = "Hello from VEIL client!";
                session.send({reinterpret_cast<const uint8_t*>(msg.data()), msg.size()});
            }
        });

        session.set_error_callback([](const std::string& error) {
            spdlog::error("Session error: {}", error);
        });

        if (!session.start()) {
            spdlog::error("Failed to start session");
            return 1;
        }

        auto last_ping = std::chrono::steady_clock::now();

        while (g_running) {
            session.process(100);

            if (ping_mode && session.is_connected()) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_ping).count();

                if (elapsed >= ping_interval) {
                    session.send_ping();
                    spdlog::debug("Sent ping (RTT: {}ms)", session.rtt_ms());
                    last_ping = now;
                }
            }
        }

        session.stop();
    }

    spdlog::info("Demo finished");
    return 0;
}
