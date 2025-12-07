#include "veil/mux/reorder_buffer.hpp"

namespace veil::mux {

ReorderBuffer::ReorderBuffer(const ReorderBufferConfig& config)
    : config_(config) {
}

void ReorderBuffer::set_deliver_callback(DeliverCallback callback) {
    callback_ = std::move(callback);
}

bool ReorderBuffer::insert(uint64_t seq, std::vector<uint8_t> data, uint64_t timestamp_ms) {
    // Check for duplicate
    if (seq < next_expected_ || buffer_.count(seq) > 0) {
        return false;
    }

    // Check buffer limits
    if (buffer_.size() >= config_.max_buffered_packets) {
        return false;
    }

    if (buffered_bytes_ + data.size() > config_.max_buffered_bytes) {
        return false;
    }

    // Insert packet
    buffered_bytes_ += data.size();
    buffer_[seq] = BufferedPacket{std::move(data), timestamp_ms};

    return true;
}

bool ReorderBuffer::has_deliverable() const {
    return buffer_.count(next_expected_) > 0;
}

size_t ReorderBuffer::deliver() {
    size_t delivered = 0;

    while (has_deliverable()) {
        auto it = buffer_.find(next_expected_);
        if (it == buffer_.end()) {
            break;
        }

        if (callback_) {
            callback_(next_expected_, std::move(it->second.data));
        }

        buffered_bytes_ -= it->second.data.size();
        buffer_.erase(it);
        ++next_expected_;
        ++delivered;
    }

    return delivered;
}

size_t ReorderBuffer::flush(uint64_t current_time_ms) {
    size_t delivered = 0;

    // First, deliver any in-order packets
    delivered += deliver();

    // Then, check for expired packets and deliver them with gaps
    while (!buffer_.empty()) {
        auto it = buffer_.begin();

        // Check if oldest packet has timed out
        if (current_time_ms - it->second.timestamp_ms < config_.max_delay_ms) {
            break;  // Not timed out yet
        }

        // Deliver the packet (even with a gap)
        if (callback_) {
            callback_(it->first, std::move(it->second.data));
        }

        buffered_bytes_ -= it->second.data.size();
        next_expected_ = it->first + 1;
        buffer_.erase(it);
        ++delivered;

        // Continue delivering any now-contiguous packets
        delivered += deliver();
    }

    return delivered;
}

void ReorderBuffer::reset() {
    buffer_.clear();
    next_expected_ = 1;
    buffered_bytes_ = 0;
}

}  // namespace veil::mux
