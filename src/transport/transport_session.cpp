#include "veil/transport/transport_session.hpp"

#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>

namespace veil::transport {

TransportSession::TransportSession(const TransportSessionConfig& config)
    : config_(config),
      packet_builder_(config.mtu),
      rate_limiter_(config.rate_limiter),
      reorder_buffer_(config.reorder),
      fragment_assembler_(config.fragment),
      retransmission_(config.retransmission),
      session_rotator_(config.session_rotator) {

    // Set up reorder buffer callback
    reorder_buffer_.set_deliver_callback([this](uint64_t seq, std::vector<uint8_t> data) {
        if (data_callback_) {
            data_callback_(std::move(data));
        }
    });

    // Set up fragment assembler callback
    fragment_assembler_.set_assemble_callback([this](uint32_t msg_id, std::vector<uint8_t> data) {
        ++stats_.messages_assembled;
        if (data_callback_) {
            data_callback_(std::move(data));
        }
    });

    // Set up retransmission callbacks
    retransmission_.set_retransmit_callback([this](uint64_t seq, const std::vector<uint8_t>& data) {
        ++stats_.packets_retransmitted;
        socket_.send_to(config_.peer_address, data);
    });

    retransmission_.set_drop_callback([this](uint64_t seq) {
        spdlog::debug("Packet {} dropped after max retries", seq);
    });

    // Set up session rotator callback
    session_rotator_.set_rotation_callback([this](mux::SessionRotator::SessionId new_id) {
        ++stats_.session_rotations;
        spdlog::info("Session rotated to ID: {:016x}", new_id);
    });
}

TransportSession::~TransportSession() {
    stop();
}

void TransportSession::set_data_callback(DataCallback callback) {
    data_callback_ = std::move(callback);
}

void TransportSession::set_state_callback(StateCallback callback) {
    state_callback_ = std::move(callback);
}

void TransportSession::set_error_callback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

void TransportSession::set_state(SessionState new_state) {
    if (state_ != new_state) {
        state_ = new_state;
        if (state_callback_) {
            state_callback_(new_state);
        }
    }
}

bool TransportSession::start() {
    // Open socket
    UdpSocketConfig socket_config;
    socket_config.bind_address = config_.local_address;

    if (!socket_.open(socket_config)) {
        if (error_callback_) {
            error_callback_("Failed to open UDP socket");
        }
        return false;
    }

    // Set up socket callback
    socket_.set_recv_callback([this](ReceivedPacket pkt) {
        handle_received_packet(std::move(pkt));
    });

    socket_.set_error_callback([this](int code, const std::string& msg) {
        spdlog::error("Socket error {}: {}", code, msg);
        if (error_callback_) {
            error_callback_(msg);
        }
    });

    // Start handshake if we have a peer
    if (!config_.peer_address.host.empty()) {
        handshake::HandshakeConfig hs_config;
        hs_config.psk = config_.psk;
        handshake_ = std::make_unique<handshake::Handshake>(hs_config);

        handshake_->set_send_callback([this](std::vector<uint8_t> msg) {
            // Wrap in handshake frame and send
            packet::HandshakeFrame frame;
            frame.stage = packet::HandshakeFrame::Stage::INIT;
            frame.payload = std::move(msg);
            send_packet_internal(frame);
        });

        set_state(SessionState::HANDSHAKING);
        if (!handshake_->initiate()) {
            ++stats_.handshake_failures;
            if (error_callback_) {
                error_callback_("Failed to initiate handshake");
            }
            return false;
        }
    } else {
        // Server mode - wait for incoming handshake
        handshake::HandshakeConfig hs_config;
        hs_config.psk = config_.psk;
        handshake_ = std::make_unique<handshake::Handshake>(hs_config);

        handshake_->set_send_callback([this](std::vector<uint8_t> msg) {
            packet::HandshakeFrame frame;
            frame.stage = packet::HandshakeFrame::Stage::RESPONSE;
            frame.payload = std::move(msg);
            send_packet_internal(frame);
        });

        set_state(SessionState::HANDSHAKING);
    }

    return true;
}

void TransportSession::stop() {
    set_state(SessionState::CLOSING);
    socket_.close();
    set_state(SessionState::CLOSED);
}

bool TransportSession::send(std::span<const uint8_t> data) {
    if (state_ != SessionState::CONNECTED) {
        return false;
    }

    // Check if fragmentation is needed
    size_t max_payload = config_.mtu - packet::PacketHeader::SIZE -
                         packet::FrameHeader::SIZE - 8 -  // sequence
                         crypto::POLY1305_TAG_SIZE;

    if (data.size() <= max_payload) {
        // Send as single data frame
        packet::DataFrame frame;
        frame.sequence_number = send_sequence_++;
        frame.payload.assign(data.begin(), data.end());
        return send_packet_internal(frame);
    }

    // Fragment the data
    auto fragments = fragment_data(data);
    ++stats_.messages_fragmented;

    bool success = true;
    for (auto& frag : fragments) {
        if (!send_packet_internal(frag)) {
            success = false;
        }
    }
    return success;
}

std::vector<packet::FragmentFrame> TransportSession::fragment_data(std::span<const uint8_t> data) {
    size_t max_fragment = config_.mtu - packet::PacketHeader::SIZE -
                          packet::FrameHeader::SIZE - 8 -  // fragment header
                          crypto::POLY1305_TAG_SIZE;

    uint32_t msg_id = next_message_id_++;
    size_t total_fragments = (data.size() + max_fragment - 1) / max_fragment;

    std::vector<packet::FragmentFrame> fragments;
    fragments.reserve(total_fragments);

    size_t offset = 0;
    for (uint16_t i = 0; i < total_fragments; ++i) {
        size_t chunk_size = std::min(max_fragment, data.size() - offset);

        packet::FragmentFrame frag;
        frag.message_id = msg_id;
        frag.fragment_index = i;
        frag.total_fragments = static_cast<uint16_t>(total_fragments);
        frag.payload.assign(data.begin() + offset, data.begin() + offset + chunk_size);

        fragments.push_back(std::move(frag));
        offset += chunk_size;
    }

    return fragments;
}

bool TransportSession::send_ping() {
    if (state_ != SessionState::CONNECTED) {
        return false;
    }

    packet::ControlFrame frame;
    frame.type = packet::ControlFrame::Type::PING;
    frame.timestamp = get_time_ms();
    return send_packet_internal(frame);
}

bool TransportSession::send_pong(uint64_t echo_timestamp) {
    if (state_ != SessionState::CONNECTED) {
        return false;
    }

    packet::ControlFrame frame;
    frame.type = packet::ControlFrame::Type::PONG;
    frame.timestamp = echo_timestamp;
    return send_packet_internal(frame);
}

bool TransportSession::send_packet_internal(const packet::Frame& frame) {
    // Rate limiting
    size_t frame_size = packet::PacketBuilder::frame_size(frame);
    if (!rate_limiter_.try_consume(frame_size)) {
        ++stats_.packets_dropped_rate_limit;
        return false;
    }

    // Build packet
    packet_builder_.reset();
    packet_builder_.set_session_id(session_rotator_.current_session_id());
    packet_builder_.set_encryption_key(session_keys_.send_key, session_keys_.send_nonce_base);

    if (!packet_builder_.add_frame(frame)) {
        return false;
    }

    auto packet_data = packet_builder_.build(send_sequence_);

    // Register for retransmission (for reliable frames)
    if (std::holds_alternative<packet::DataFrame>(frame)) {
        retransmission_.register_packet(send_sequence_, packet_data, get_time_ms());
    }

    // Send
    bool sent = socket_.send_to(config_.peer_address, packet_data);
    if (sent) {
        ++send_sequence_;
        ++stats_.packets_sent;
        stats_.bytes_sent += packet_data.size();
        session_rotator_.on_packet_sent(packet_data.size());
    }

    return sent;
}

void TransportSession::send_ack() {
    packet::AckFrame frame;
    frame.ack_number = ack_bitmap_.get_ack_number();
    frame.bitmap = ack_bitmap_.get_bitmap();
    frame.recv_window = static_cast<uint32_t>(config_.reorder.max_buffered_packets);
    send_packet_internal(frame);
    last_ack_sent_ = frame.ack_number;
}

bool TransportSession::process(int timeout_ms) {
    // Refill rate limiter
    rate_limiter_.refill_now();

    // Process socket events
    socket_.run_once(timeout_ms);

    // Process timeouts
    process_timeouts();

    // Check for session rotation
    if (session_rotator_.should_rotate()) {
        session_rotator_.rotate();
    }

    return true;
}

void TransportSession::process_timeouts() {
    uint64_t now = get_time_ms();

    // Retransmit expired packets
    retransmission_.retransmit_expired(now);

    // Flush reorder buffer if needed
    reorder_buffer_.flush(now);

    // Clean up expired fragment assemblies
    fragment_assembler_.cleanup_expired(now);
}

void TransportSession::handle_received_packet(ReceivedPacket packet) {
    ++stats_.packets_received;
    stats_.bytes_received += packet.data.size();

    // Parse packet header first
    auto header_opt = packet::PacketParser::parse_header(packet.data);
    if (!header_opt) {
        return;
    }

    // Check session ID
    if (header_opt->session_id != session_rotator_.current_session_id()) {
        // Could be old session during rotation - silent drop
        return;
    }

    // Replay protection
    if (!replay_window_.check_and_update(header_opt->packet_counter)) {
        ++stats_.packets_dropped_replay;
        return;
    }

    // Parse and decrypt
    packet_parser_.set_decryption_key(session_keys_.recv_key, session_keys_.recv_nonce_base);
    packet::ParseError error;
    auto parsed = packet_parser_.parse(packet.data, &error);

    if (!parsed) {
        if (error == packet::ParseError::DECRYPTION_FAILED) {
            ++stats_.decryption_failures;
        }
        return;
    }

    session_rotator_.on_packet_received(packet.data.size());

    // Process frames
    for (auto& frame : parsed->frames) {
        handle_frame(frame);
    }
}

void TransportSession::handle_frame(const packet::Frame& frame) {
    std::visit([this](const auto& f) {
        using T = std::decay_t<decltype(f)>;
        if constexpr (std::is_same_v<T, packet::DataFrame>) {
            handle_data_frame(f);
        } else if constexpr (std::is_same_v<T, packet::AckFrame>) {
            handle_ack_frame(f);
        } else if constexpr (std::is_same_v<T, packet::ControlFrame>) {
            handle_control_frame(f);
        } else if constexpr (std::is_same_v<T, packet::FragmentFrame>) {
            handle_fragment_frame(f);
        } else if constexpr (std::is_same_v<T, packet::HandshakeFrame>) {
            handle_handshake_frame(f);
        }
    }, frame);
}

void TransportSession::handle_data_frame(const packet::DataFrame& frame) {
    // Mark as received for ACK
    ack_bitmap_.mark_received(frame.sequence_number);

    // Add to reorder buffer
    reorder_buffer_.insert(frame.sequence_number, frame.payload, get_time_ms());

    // Try to deliver
    reorder_buffer_.deliver();

    // Send ACK if significant progress
    if (ack_bitmap_.get_ack_number() > last_ack_sent_ + 2) {
        send_ack();
    }
}

void TransportSession::handle_ack_frame(const packet::AckFrame& frame) {
    retransmission_.process_sack(frame.ack_number, frame.bitmap, get_time_ms());
}

void TransportSession::handle_control_frame(const packet::ControlFrame& frame) {
    switch (frame.type) {
        case packet::ControlFrame::Type::PING:
            send_pong(frame.timestamp);
            break;
        case packet::ControlFrame::Type::PONG:
            // RTT measurement handled by retransmission manager
            break;
        case packet::ControlFrame::Type::CLOSE:
            set_state(SessionState::CLOSING);
            break;
        case packet::ControlFrame::Type::RESET:
            set_state(SessionState::DISCONNECTED);
            break;
    }
}

void TransportSession::handle_fragment_frame(const packet::FragmentFrame& frame) {
    fragment_assembler_.add_fragment(
        frame.message_id,
        frame.fragment_index,
        frame.total_fragments,
        frame.payload,
        get_time_ms()
    );
}

void TransportSession::handle_handshake_frame(const packet::HandshakeFrame& frame) {
    if (!handshake_) {
        return;
    }

    bool complete = handshake_->process_message(frame.payload);

    if (complete) {
        auto result = handshake_->result();
        if (result) {
            session_keys_ = result->session_keys;
            // Set up packet builder/parser with session keys
            packet_builder_.set_encryption_key(session_keys_.send_key,
                                                session_keys_.send_nonce_base);
            packet_parser_.set_decryption_key(session_keys_.recv_key,
                                               session_keys_.recv_nonce_base);
            set_state(SessionState::CONNECTED);
            spdlog::info("Handshake complete, session established");
        } else {
            ++stats_.handshake_failures;
            if (error_callback_) {
                error_callback_("Handshake failed");
            }
        }
    } else if (handshake_->state() == handshake::HandshakeState::FAILED) {
        ++stats_.handshake_failures;
        if (error_callback_) {
            error_callback_("Handshake failed");
        }
    }
}

uint64_t TransportSession::rtt_ms() const {
    return retransmission_.get_srtt_ms();
}

uint64_t TransportSession::get_time_ms() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

}  // namespace veil::transport
