#include "common/obfuscation/obfuscation_profile.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#include "common/crypto/crypto_engine.h"
#include "common/crypto/random.h"

namespace veil::obfuscation {

namespace {

// Convert hex string to byte array.
bool hex_to_bytes(const std::string& hex, std::array<std::uint8_t, kProfileSeedSize>& out) {
  if (hex.size() != kProfileSeedSize * 2) {
    return false;
  }
  for (std::size_t i = 0; i < kProfileSeedSize; ++i) {
    const auto byte_str = hex.substr(i * 2, 2);
    std::uint8_t byte = 0;
    auto result = std::from_chars(byte_str.data(), byte_str.data() + 2, byte, 16);
    if (result.ec != std::errc{}) {
      return false;
    }
    out[i] = byte;
  }
  return true;
}

// Derive a deterministic value using HMAC of seed + counter.
std::uint64_t derive_value(const std::array<std::uint8_t, kProfileSeedSize>& seed,
                           std::uint64_t counter, const char* context) {
  // Create input: seed || counter || context.
  std::vector<std::uint8_t> input;
  input.reserve(seed.size() + 8 + std::strlen(context));
  input.insert(input.end(), seed.begin(), seed.end());
  for (int i = 7; i >= 0; --i) {
    input.push_back(static_cast<std::uint8_t>((counter >> (8 * i)) & 0xFF));
  }
  input.insert(input.end(), context, context + std::strlen(context));

  // HMAC with seed as key.
  auto hmac = crypto::hmac_sha256(seed, input);

  // Extract first 8 bytes as uint64.
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value = (value << 8) | hmac[static_cast<std::size_t>(i)];
  }
  return value;
}

}  // namespace

std::optional<ObfuscationConfig> parse_obfuscation_config(
    const std::string& enabled, const std::string& max_padding,
    const std::string& profile_seed, const std::string& heartbeat_min,
    const std::string& heartbeat_max, const std::string& timing_jitter) {
  ObfuscationConfig config;

  // Parse enabled.
  config.enabled = (enabled == "true" || enabled == "1" || enabled == "yes");

  // Parse max_padding_size.
  if (!max_padding.empty()) {
    int val = 0;
    auto result = std::from_chars(max_padding.data(), max_padding.data() + max_padding.size(), val);
    if (result.ec == std::errc{} && val >= 0 && val <= 65535) {
      config.max_padding_size = static_cast<std::uint16_t>(val);
    }
  }

  // Parse profile_seed.
  config.profile_seed_hex = profile_seed;

  // Parse heartbeat intervals.
  if (!heartbeat_min.empty()) {
    int val = 0;
    auto result =
        std::from_chars(heartbeat_min.data(), heartbeat_min.data() + heartbeat_min.size(), val);
    if (result.ec == std::errc{} && val >= 0) {
      config.heartbeat_interval_min = std::chrono::seconds(val);
    }
  }
  if (!heartbeat_max.empty()) {
    int val = 0;
    auto result =
        std::from_chars(heartbeat_max.data(), heartbeat_max.data() + heartbeat_max.size(), val);
    if (result.ec == std::errc{} && val >= 0) {
      config.heartbeat_interval_max = std::chrono::seconds(val);
    }
  }

  // Parse timing_jitter.
  config.enable_timing_jitter =
      (timing_jitter == "true" || timing_jitter == "1" || timing_jitter == "yes");

  return config;
}

ObfuscationProfile config_to_profile(const ObfuscationConfig& config) {
  ObfuscationProfile profile;
  profile.enabled = config.enabled;
  profile.max_padding_size = config.max_padding_size;
  profile.heartbeat_min = config.heartbeat_interval_min;
  profile.heartbeat_max = config.heartbeat_interval_max;
  profile.timing_jitter_enabled = config.enable_timing_jitter;

  // Parse or generate profile seed.
  if (config.profile_seed_hex.empty() || config.profile_seed_hex == "auto") {
    profile.profile_seed = generate_profile_seed();
  } else {
    if (!hex_to_bytes(config.profile_seed_hex, profile.profile_seed)) {
      // Invalid hex, generate random seed.
      profile.profile_seed = generate_profile_seed();
    }
  }

  return profile;
}

std::array<std::uint8_t, kProfileSeedSize> generate_profile_seed() {
  std::array<std::uint8_t, kProfileSeedSize> seed{};
  auto random = crypto::random_bytes(kProfileSeedSize);
  std::copy(random.begin(), random.end(), seed.begin());
  return seed;
}

std::uint16_t compute_padding_size(const ObfuscationProfile& profile, std::uint64_t sequence) {
  if (!profile.enabled || profile.max_padding_size == 0) {
    return 0;
  }

  const auto value = derive_value(profile.profile_seed, sequence, "padding");
  const auto range = static_cast<std::uint16_t>(profile.max_padding_size - profile.min_padding_size + 1);
  return static_cast<std::uint16_t>(profile.min_padding_size + static_cast<std::uint16_t>(value % range));
}

std::uint8_t compute_prefix_size(const ObfuscationProfile& profile, std::uint64_t sequence) {
  if (!profile.enabled) {
    return 0;
  }

  const auto value = derive_value(profile.profile_seed, sequence, "prefix");
  const auto range = static_cast<std::uint8_t>(profile.max_prefix_size - profile.min_prefix_size + 1);
  return static_cast<std::uint8_t>(profile.min_prefix_size + static_cast<std::uint8_t>(value % range));
}

std::uint16_t compute_timing_jitter(const ObfuscationProfile& profile, std::uint64_t sequence) {
  if (!profile.enabled || !profile.timing_jitter_enabled || profile.max_timing_jitter_ms == 0) {
    return 0;
  }

  const auto value = derive_value(profile.profile_seed, sequence, "jitter");
  return static_cast<std::uint16_t>(value % (profile.max_timing_jitter_ms + 1));
}

std::chrono::milliseconds compute_heartbeat_interval(const ObfuscationProfile& profile,
                                                      std::uint64_t heartbeat_count) {
  const auto min_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(profile.heartbeat_min).count();
  const auto max_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(profile.heartbeat_max).count();

  if (min_ms >= max_ms) {
    return std::chrono::milliseconds(min_ms);
  }

  const auto value = derive_value(profile.profile_seed, heartbeat_count, "heartbeat");
  const auto range = static_cast<std::uint64_t>(max_ms - min_ms + 1);
  return std::chrono::milliseconds(min_ms + static_cast<long long>(value % range));
}

}  // namespace veil::obfuscation
