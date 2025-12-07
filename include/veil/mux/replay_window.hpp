#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

namespace veil::mux {

// Sliding window for replay attack protection
// Tracks which packet sequence numbers have been seen
class ReplayWindow {
public:
    // Window size: tracks 64 packets before the highest seen
    static constexpr size_t WINDOW_SIZE = 64;

    ReplayWindow() = default;

    // Check if a sequence number is valid (not replayed)
    // Returns true if the packet should be accepted
    [[nodiscard]] bool check(uint64_t seq) const;

    // Update the window with a new sequence number
    // Should only be called after check() returns true
    void update(uint64_t seq);

    // Check and update in one call
    // Returns true if packet was accepted
    bool check_and_update(uint64_t seq);

    // Get the highest seen sequence number
    [[nodiscard]] uint64_t highest() const { return highest_seq_; }

    // Reset the window
    void reset();

private:
    uint64_t highest_seq_{0};
    uint64_t bitmap_{0};  // Bitmap for window: bit i = seq (highest_seq_ - i - 1)
    bool initialized_{false};
};

}  // namespace veil::mux
