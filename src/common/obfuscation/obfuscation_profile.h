#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace veil::obfuscation {

// Profile seed size (32 bytes for deterministic obfuscation).
constexpr std::size_t kProfileSeedSize = 32;

// Obfuscation profile configuration.
// Controls padding, prefix, timing jitter, and heartbeat behavior.
struct ObfuscationProfile {
  // Profile seed for deterministic padding/prefix generation.
  // If empty/zeroed, generates random seed on first use.
  std::array<std::uint8_t, kProfileSeedSize> profile_seed{};

  // Whether obfuscation is enabled.
  bool enabled{true};

  // Maximum padding size in bytes (added to each packet).
  std::uint16_t max_padding_size{400};

  // Minimum padding size in bytes.
  std::uint16_t min_padding_size{0};

  // Random prefix size range (4-12 bytes based on profile_seed + seq).
  std::uint8_t min_prefix_size{4};
  std::uint8_t max_prefix_size{12};

  // Heartbeat interval range for idle traffic.
  std::chrono::seconds heartbeat_min{5};
  std::chrono::seconds heartbeat_max{15};

  // Enable timing jitter for packet sends.
  bool timing_jitter_enabled{true};

  // Maximum timing jitter in milliseconds.
  std::uint16_t max_timing_jitter_ms{50};

  // Size variance: target different packet size distributions.
  // 0.0 = constant size, 1.0 = maximum variance.
  float size_variance{0.5f};
};

// Obfuscation metrics for DPI/ML analysis.
struct ObfuscationMetrics {
  // Packet size statistics (sliding window).
  std::uint64_t packets_measured{0};
  double avg_packet_size{0.0};
  double packet_size_variance{0.0};
  double packet_size_stddev{0.0};
  std::uint16_t min_packet_size{0};
  std::uint16_t max_packet_size{0};

  // Inter-packet timing statistics.
  double avg_interval_ms{0.0};
  double interval_variance{0.0};
  double interval_stddev{0.0};

  // Heartbeat statistics.
  std::uint64_t heartbeats_sent{0};
  std::uint64_t heartbeats_received{0};
  double heartbeat_ratio{0.0};  // heartbeats / total packets

  // Padding statistics.
  std::uint64_t total_padding_bytes{0};
  double avg_padding_per_packet{0.0};
};

// Configuration file section for obfuscation.
struct ObfuscationConfig {
  bool enabled{true};
  std::uint16_t max_padding_size{400};
  std::string profile_seed_hex;  // Hex-encoded seed, "auto" for random.
  std::chrono::seconds heartbeat_interval_min{5};
  std::chrono::seconds heartbeat_interval_max{15};
  bool enable_timing_jitter{true};
};

// Parse obfuscation config from key-value pairs.
// Typically called from INI/config file parser.
std::optional<ObfuscationConfig> parse_obfuscation_config(
    const std::string& enabled, const std::string& max_padding,
    const std::string& profile_seed, const std::string& heartbeat_min,
    const std::string& heartbeat_max, const std::string& timing_jitter);

// Convert ObfuscationConfig to runtime ObfuscationProfile.
ObfuscationProfile config_to_profile(const ObfuscationConfig& config);

// Generate a random profile seed.
std::array<std::uint8_t, kProfileSeedSize> generate_profile_seed();

// Compute deterministic padding size based on profile seed and sequence.
std::uint16_t compute_padding_size(const ObfuscationProfile& profile, std::uint64_t sequence);

// Compute deterministic prefix size based on profile seed and sequence.
std::uint8_t compute_prefix_size(const ObfuscationProfile& profile, std::uint64_t sequence);

// Compute timing jitter in milliseconds based on profile seed and sequence.
std::uint16_t compute_timing_jitter(const ObfuscationProfile& profile, std::uint64_t sequence);

// Compute heartbeat interval based on profile seed.
std::chrono::milliseconds compute_heartbeat_interval(const ObfuscationProfile& profile,
                                                      std::uint64_t heartbeat_count);

}  // namespace veil::obfuscation
