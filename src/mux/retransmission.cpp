#include "veil/mux/retransmission.hpp"

#include <algorithm>
#include <cmath>

namespace veil::mux {

RetransmissionManager::RetransmissionManager(const RetransmissionConfig& config)
    : config_(config),
      rto_ms_(config.initial_rto_ms) {
}

void RetransmissionManager::set_retransmit_callback(RetransmitCallback callback) {
    retransmit_callback_ = std::move(callback);
}

void RetransmissionManager::set_drop_callback(DropCallback callback) {
    drop_callback_ = std::move(callback);
}

bool RetransmissionManager::register_packet(uint64_t seq, std::vector<uint8_t> data, uint64_t send_time_ms) {
    // Check limits
    if (unacked_.size() >= config_.max_unacked_packets) {
        return false;
    }

    if (unacked_bytes_ + data.size() > config_.max_unacked_bytes) {
        return false;
    }

    // Check for duplicate
    if (unacked_.count(seq) > 0) {
        return false;
    }

    // Store packet
    unacked_bytes_ += data.size();
    unacked_[seq] = UnackedPacket{std::move(data), send_time_ms, send_time_ms, 0};

    return true;
}

void RetransmissionManager::ack_packet(uint64_t seq, uint64_t ack_time_ms) {
    auto it = unacked_.find(seq);
    if (it == unacked_.end()) {
        return;
    }

    // Update RTT only for non-retransmitted packets (Karn's algorithm)
    if (it->second.retransmit_count == 0) {
        uint64_t rtt = ack_time_ms - it->second.send_time_ms;
        update_rtt(rtt);
    }

    // Remove from unacked
    unacked_bytes_ -= it->second.data.size();
    unacked_.erase(it);
}

void RetransmissionManager::process_sack(uint64_t ack_number, uint64_t bitmap, uint64_t ack_time_ms) {
    // ACK all packets up to ack_number
    for (auto it = unacked_.begin(); it != unacked_.end(); ) {
        if (it->first <= ack_number) {
            // Update RTT if not retransmitted
            if (it->second.retransmit_count == 0) {
                uint64_t rtt = ack_time_ms - it->second.send_time_ms;
                update_rtt(rtt);
            }
            unacked_bytes_ -= it->second.data.size();
            it = unacked_.erase(it);
        } else {
            ++it;
        }
    }

    // Process SACK bitmap for packets after ack_number
    for (size_t i = 0; i < 64; ++i) {
        if (bitmap & (1ULL << i)) {
            uint64_t seq = ack_number + 1 + i;
            ack_packet(seq, ack_time_ms);
        }
    }
}

std::vector<uint64_t> RetransmissionManager::check_timeouts(uint64_t current_time_ms) {
    std::vector<uint64_t> to_retransmit;

    for (auto& [seq, packet] : unacked_) {
        if (current_time_ms - packet.last_sent_ms >= rto_ms_) {
            to_retransmit.push_back(seq);
        }
    }

    return to_retransmit;
}

size_t RetransmissionManager::retransmit_expired(uint64_t current_time_ms) {
    size_t count = 0;
    std::vector<uint64_t> to_drop;

    for (auto& [seq, packet] : unacked_) {
        if (current_time_ms - packet.last_sent_ms < rto_ms_) {
            continue;
        }

        if (packet.retransmit_count >= config_.max_retries) {
            // Max retries exceeded - drop packet
            to_drop.push_back(seq);
            continue;
        }

        // Retransmit
        if (retransmit_callback_) {
            retransmit_callback_(seq, packet.data);
        }

        packet.last_sent_ms = current_time_ms;
        ++packet.retransmit_count;
        ++total_retransmits_;
        ++count;

        // Exponential backoff (double RTO after retransmit)
        rto_ms_ = std::min(rto_ms_ * 2, config_.max_rto_ms);
    }

    // Drop packets that exceeded max retries
    for (uint64_t seq : to_drop) {
        if (drop_callback_) {
            drop_callback_(seq);
        }
        auto it = unacked_.find(seq);
        if (it != unacked_.end()) {
            unacked_bytes_ -= it->second.data.size();
            unacked_.erase(it);
        }
        ++total_drops_;
    }

    return count;
}

bool RetransmissionManager::can_send(size_t bytes) const {
    if (unacked_.size() >= config_.max_unacked_packets) {
        return false;
    }
    if (unacked_bytes_ + bytes > config_.max_unacked_bytes) {
        return false;
    }
    return true;
}

void RetransmissionManager::update_rtt(uint64_t rtt_sample_ms) {
    if (!rtt_initialized_) {
        srtt_ms_ = rtt_sample_ms;
        rttvar_ms_ = rtt_sample_ms / 2;
        rtt_initialized_ = true;
    } else {
        // RFC 6298 RTT calculation
        int64_t delta = static_cast<int64_t>(rtt_sample_ms) - static_cast<int64_t>(srtt_ms_);
        srtt_ms_ = static_cast<uint64_t>(srtt_ms_ + config_.rtt_alpha * delta);

        uint64_t abs_delta = delta >= 0 ? delta : -delta;
        rttvar_ms_ = static_cast<uint64_t>(
            (1 - config_.rtt_beta) * rttvar_ms_ + config_.rtt_beta * abs_delta);
    }

    // RTO = SRTT + 4 * RTTVAR
    rto_ms_ = srtt_ms_ + 4 * rttvar_ms_;
    rto_ms_ = std::max(rto_ms_, config_.min_rto_ms);
    rto_ms_ = std::min(rto_ms_, config_.max_rto_ms);
}

void RetransmissionManager::reset() {
    unacked_.clear();
    unacked_bytes_ = 0;
    srtt_ms_ = 0;
    rttvar_ms_ = 0;
    rto_ms_ = config_.initial_rto_ms;
    rtt_initialized_ = false;
    total_retransmits_ = 0;
    total_drops_ = 0;
}

}  // namespace veil::mux
