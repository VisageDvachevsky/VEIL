#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace veil::mux {

// Configuration for reorder buffer
struct ReorderBufferConfig {
    size_t max_buffered_packets = 256;  // Maximum packets to buffer
    size_t max_buffered_bytes = 1048576;  // Maximum bytes to buffer (1MB)
    uint64_t max_delay_ms = 1000;  // Maximum time to wait for missing packets
};

// Reorder buffer for handling out-of-order packets
class ReorderBuffer {
public:
    using DeliverCallback = std::function<void(uint64_t seq, std::vector<uint8_t> data)>;

    explicit ReorderBuffer(const ReorderBufferConfig& config = {});

    // Set callback for delivering in-order packets
    void set_deliver_callback(DeliverCallback callback);

    // Insert a packet
    // Returns true if packet was buffered, false if dropped (duplicate, etc.)
    bool insert(uint64_t seq, std::vector<uint8_t> data, uint64_t timestamp_ms);

    // Get the next expected sequence number
    [[nodiscard]] uint64_t next_expected() const { return next_expected_; }

    // Check if buffer has packets ready to deliver
    [[nodiscard]] bool has_deliverable() const;

    // Deliver ready packets (calls callback for each)
    // Returns number of packets delivered
    size_t deliver();

    // Force delivery of all buffered packets (even with gaps)
    // Used when timeout expires
    size_t flush(uint64_t current_time_ms);

    // Get buffer statistics
    [[nodiscard]] size_t buffered_count() const { return buffer_.size(); }
    [[nodiscard]] size_t buffered_bytes() const { return buffered_bytes_; }

    // Reset state
    void reset();

private:
    struct BufferedPacket {
        std::vector<uint8_t> data;
        uint64_t timestamp_ms;
    };

    ReorderBufferConfig config_;
    std::map<uint64_t, BufferedPacket> buffer_;
    uint64_t next_expected_{1};  // First expected seq is 1
    size_t buffered_bytes_{0};
    DeliverCallback callback_;
};

}  // namespace veil::mux
