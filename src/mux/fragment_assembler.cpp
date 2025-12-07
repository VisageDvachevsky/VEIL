#include "veil/mux/fragment_assembler.hpp"

#include <algorithm>

namespace veil::mux {

FragmentAssembler::FragmentAssembler(const FragmentAssemblerConfig& config)
    : config_(config) {
}

void FragmentAssembler::set_assemble_callback(AssembleCallback callback) {
    callback_ = std::move(callback);
}

bool FragmentAssembler::add_fragment(uint32_t message_id,
                                      uint16_t fragment_index,
                                      uint16_t total_fragments,
                                      std::span<const uint8_t> payload,
                                      uint64_t timestamp_ms) {
    ++total_fragments_;

    // Validate fragment parameters
    if (total_fragments == 0 || fragment_index >= total_fragments) {
        return false;
    }

    if (total_fragments > config_.max_fragments_per_message) {
        return false;
    }

    // Check if this is a new message
    auto it = pending_.find(message_id);
    if (it == pending_.end()) {
        // Check pending message limit
        if (pending_.size() >= config_.max_pending_messages) {
            return false;
        }

        // Create new pending message
        PendingMessage msg;
        msg.total_fragments = total_fragments;
        msg.first_fragment_time_ms = timestamp_ms;
        it = pending_.emplace(message_id, std::move(msg)).first;
    }

    auto& msg = it->second;

    // Verify total_fragments matches (all fragments of same message must agree)
    if (msg.total_fragments != total_fragments) {
        return false;
    }

    // Check for duplicate fragment
    if (msg.fragments.count(fragment_index) > 0) {
        return false;
    }

    // Check size limit
    if (msg.total_bytes + payload.size() > config_.max_message_size) {
        return false;
    }

    // Store fragment
    msg.fragments[fragment_index] = std::vector<uint8_t>(payload.begin(), payload.end());
    msg.total_bytes += payload.size();

    // Try to assemble
    if (msg.fragments.size() == msg.total_fragments) {
        auto assembled = try_assemble(msg);
        if (assembled && callback_) {
            callback_(message_id, std::move(*assembled));
        }
        pending_.erase(it);
        ++messages_assembled_;
    }

    return true;
}

std::optional<std::vector<uint8_t>> FragmentAssembler::try_assemble(const PendingMessage& msg) {
    // Verify we have all fragments
    if (msg.fragments.size() != msg.total_fragments) {
        return std::nullopt;
    }

    // Concatenate fragments in order
    std::vector<uint8_t> result;
    result.reserve(msg.total_bytes);

    for (uint16_t i = 0; i < msg.total_fragments; ++i) {
        auto it = msg.fragments.find(i);
        if (it == msg.fragments.end()) {
            return std::nullopt;
        }
        result.insert(result.end(), it->second.begin(), it->second.end());
    }

    return result;
}

size_t FragmentAssembler::cleanup_expired(uint64_t current_time_ms) {
    size_t cleaned = 0;

    for (auto it = pending_.begin(); it != pending_.end(); ) {
        if (current_time_ms - it->second.first_fragment_time_ms > config_.fragment_timeout_ms) {
            it = pending_.erase(it);
            ++cleaned;
            ++messages_expired_;
        } else {
            ++it;
        }
    }

    return cleaned;
}

void FragmentAssembler::reset() {
    pending_.clear();
    total_fragments_ = 0;
    messages_assembled_ = 0;
    messages_expired_ = 0;
}

}  // namespace veil::mux
