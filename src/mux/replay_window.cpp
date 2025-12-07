#include "veil/mux/replay_window.hpp"

namespace veil::mux {

bool ReplayWindow::check(uint64_t seq) const {
    // First packet initializes the window
    if (!initialized_) {
        return true;
    }

    // Sequence number is too old (before the window)
    if (seq + WINDOW_SIZE <= highest_seq_) {
        return false;
    }

    // Sequence number is strictly ahead of the highest - new packet
    if (seq > highest_seq_) {
        return true;
    }

    // Sequence number equals highest - duplicate
    if (seq == highest_seq_) {
        return false;
    }

    // Sequence number is within the window
    // Check if already seen in bitmap
    uint64_t diff = highest_seq_ - seq - 1;
    if (diff < WINDOW_SIZE) {
        return (bitmap_ & (1ULL << diff)) == 0;
    }

    return false;
}

void ReplayWindow::update(uint64_t seq) {
    if (!initialized_) {
        highest_seq_ = seq;
        bitmap_ = 0;
        initialized_ = true;
        return;
    }

    if (seq > highest_seq_) {
        // New highest: shift the bitmap
        uint64_t shift = seq - highest_seq_;
        if (shift >= WINDOW_SIZE) {
            // Completely new window
            bitmap_ = 0;
        } else {
            // Shift and set the bit for the old highest
            bitmap_ = (bitmap_ << shift) | (1ULL << (shift - 1));
        }
        highest_seq_ = seq;
    } else if (seq < highest_seq_) {
        // Mark as seen in bitmap
        uint64_t diff = highest_seq_ - seq - 1;
        if (diff < WINDOW_SIZE) {
            bitmap_ |= (1ULL << diff);
        }
    }
    // seq == highest_seq_: already at highest, no update needed
}

bool ReplayWindow::check_and_update(uint64_t seq) {
    if (!check(seq)) {
        return false;
    }
    update(seq);
    return true;
}

void ReplayWindow::reset() {
    highest_seq_ = 0;
    bitmap_ = 0;
    initialized_ = false;
}

}  // namespace veil::mux
