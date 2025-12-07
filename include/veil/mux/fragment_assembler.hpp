#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace veil::mux {

// Configuration for fragment assembler
struct FragmentAssemblerConfig {
    size_t max_pending_messages = 64;      // Max messages being assembled
    size_t max_fragments_per_message = 64; // Max fragments per message
    size_t max_message_size = 65536;       // Max assembled message size
    uint64_t fragment_timeout_ms = 5000;   // Timeout for incomplete messages
};

// Fragment assembler for reconstructing fragmented messages
class FragmentAssembler {
public:
    using AssembleCallback = std::function<void(uint32_t message_id, std::vector<uint8_t> data)>;

    explicit FragmentAssembler(const FragmentAssemblerConfig& config = {});

    // Set callback for when a message is fully assembled
    void set_assemble_callback(AssembleCallback callback);

    // Add a fragment
    // Returns true if fragment was accepted
    bool add_fragment(uint32_t message_id,
                      uint16_t fragment_index,
                      uint16_t total_fragments,
                      std::span<const uint8_t> payload,
                      uint64_t timestamp_ms);

    // Clean up timed-out incomplete messages
    size_t cleanup_expired(uint64_t current_time_ms);

    // Get statistics
    [[nodiscard]] size_t pending_messages() const { return pending_.size(); }
    [[nodiscard]] size_t total_fragments_received() const { return total_fragments_; }
    [[nodiscard]] size_t messages_assembled() const { return messages_assembled_; }
    [[nodiscard]] size_t messages_expired() const { return messages_expired_; }

    // Reset state
    void reset();

private:
    struct PendingMessage {
        uint16_t total_fragments;
        std::map<uint16_t, std::vector<uint8_t>> fragments;
        uint64_t first_fragment_time_ms;
        size_t total_bytes{0};
    };

    FragmentAssemblerConfig config_;
    std::map<uint32_t, PendingMessage> pending_;
    AssembleCallback callback_;

    // Statistics
    size_t total_fragments_{0};
    size_t messages_assembled_{0};
    size_t messages_expired_{0};

    // Try to assemble a complete message
    std::optional<std::vector<uint8_t>> try_assemble(const PendingMessage& msg);
};

}  // namespace veil::mux
