#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace veil::mux {

// Selective ACK bitmap manager
// Tracks which packets have been received for generating SACK bitmaps
class AckBitmap {
public:
    static constexpr size_t BITMAP_SIZE = 64;  // Number of packets in bitmap

    AckBitmap() = default;

    // Mark a sequence number as received
    void mark_received(uint64_t seq);

    // Get the highest contiguous sequence number received
    [[nodiscard]] uint64_t get_ack_number() const { return ack_number_; }

    // Get the SACK bitmap (for packets after ack_number)
    // Bit i = 1 means packet (ack_number + 1 + i) was received
    [[nodiscard]] uint64_t get_bitmap() const;

    // Check if a specific sequence number has been received
    [[nodiscard]] bool is_received(uint64_t seq) const;

    // Process incoming ACK with bitmap
    // Returns list of sequence numbers that are now acknowledged
    std::vector<uint64_t> process_ack(uint64_t ack_number, uint64_t bitmap);

    // Reset state
    void reset();

private:
    uint64_t ack_number_{0};  // Highest contiguous seq received
    uint64_t bitmap_{0};      // SACK bitmap for packets after ack_number_
    bool initialized_{false};
};

}  // namespace veil::mux
