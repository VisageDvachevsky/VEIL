#include "veil/mux/ack_bitmap.hpp"

#include <algorithm>

namespace veil::mux {

void AckBitmap::mark_received(uint64_t seq) {
    if (!initialized_) {
        ack_number_ = seq;
        bitmap_ = 0;
        initialized_ = true;
        return;
    }

    if (seq <= ack_number_) {
        // Already acknowledged (contiguously or earlier)
        return;
    }

    // Check if this extends the contiguous range
    if (seq == ack_number_ + 1) {
        // Advance ack_number
        ack_number_ = seq;
        // Shift bitmap down by 1 since we moved ack_number forward
        bitmap_ >>= 1;

        // Consume contiguous bits from bitmap
        while (bitmap_ & 1) {
            bitmap_ >>= 1;
            ++ack_number_;
        }
    } else {
        // Out of order - mark in bitmap
        uint64_t offset = seq - ack_number_ - 1;
        if (offset < BITMAP_SIZE) {
            bitmap_ |= (1ULL << offset);
        }
        // Packets beyond BITMAP_SIZE are dropped (not tracked)
    }
}

uint64_t AckBitmap::get_bitmap() const {
    return bitmap_;
}

bool AckBitmap::is_received(uint64_t seq) const {
    if (!initialized_) {
        return false;
    }

    if (seq <= ack_number_) {
        return true;
    }

    uint64_t offset = seq - ack_number_ - 1;
    if (offset < BITMAP_SIZE) {
        return (bitmap_ & (1ULL << offset)) != 0;
    }

    return false;
}

std::vector<uint64_t> AckBitmap::process_ack(uint64_t ack_num, uint64_t bmap) {
    std::vector<uint64_t> acked;

    // All packets up to and including ack_num are acknowledged
    for (uint64_t seq = 1; seq <= ack_num; ++seq) {
        acked.push_back(seq);
    }

    // Process bitmap for selective acks
    for (size_t i = 0; i < BITMAP_SIZE; ++i) {
        if (bmap & (1ULL << i)) {
            acked.push_back(ack_num + 1 + i);
        }
    }

    return acked;
}

void AckBitmap::reset() {
    ack_number_ = 0;
    bitmap_ = 0;
    initialized_ = false;
}

}  // namespace veil::mux
