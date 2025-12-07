#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "common/obfuscation/obfuscation_profile.h"

namespace veil::obfuscation::tests {

class ObfuscationProfileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a deterministic profile for testing.
    profile_.enabled = true;
    profile_.min_padding_size = 10;
    profile_.max_padding_size = 100;
    profile_.min_prefix_size = 4;
    profile_.max_prefix_size = 12;
    profile_.heartbeat_min = std::chrono::seconds(5);
    profile_.heartbeat_max = std::chrono::seconds(15);
    profile_.timing_jitter_enabled = true;
    profile_.max_timing_jitter_ms = 50;

    // Set deterministic seed.
    for (std::size_t i = 0; i < profile_.profile_seed.size(); ++i) {
      profile_.profile_seed[i] = static_cast<std::uint8_t>(i);
    }
  }

  ObfuscationProfile profile_;
};

TEST_F(ObfuscationProfileTest, GenerateProfileSeedIsRandom) {
  auto seed1 = generate_profile_seed();
  auto seed2 = generate_profile_seed();

  // Seeds should be different (with overwhelming probability).
  EXPECT_NE(seed1, seed2);

  // Seeds should not be all zeros.
  bool all_zero1 = std::all_of(seed1.begin(), seed1.end(), [](uint8_t b) { return b == 0; });
  bool all_zero2 = std::all_of(seed2.begin(), seed2.end(), [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(all_zero1);
  EXPECT_FALSE(all_zero2);
}

TEST_F(ObfuscationProfileTest, ComputePaddingSizeWithinBounds) {
  for (std::uint64_t seq = 0; seq < 1000; ++seq) {
    auto size = compute_padding_size(profile_, seq);
    EXPECT_GE(size, profile_.min_padding_size);
    EXPECT_LE(size, profile_.max_padding_size);
  }
}

TEST_F(ObfuscationProfileTest, ComputePaddingSizeIsDeterministic) {
  for (std::uint64_t seq = 0; seq < 100; ++seq) {
    auto size1 = compute_padding_size(profile_, seq);
    auto size2 = compute_padding_size(profile_, seq);
    EXPECT_EQ(size1, size2);
  }
}

TEST_F(ObfuscationProfileTest, ComputePaddingSizeVariesWithSequence) {
  std::set<std::uint16_t> sizes;
  for (std::uint64_t seq = 0; seq < 1000; ++seq) {
    sizes.insert(compute_padding_size(profile_, seq));
  }
  // Should have good variance (at least 10 different sizes).
  EXPECT_GE(sizes.size(), 10U);
}

TEST_F(ObfuscationProfileTest, ComputePaddingSizeDisabledReturnsZero) {
  profile_.enabled = false;
  EXPECT_EQ(compute_padding_size(profile_, 0), 0U);
  EXPECT_EQ(compute_padding_size(profile_, 100), 0U);
}

TEST_F(ObfuscationProfileTest, ComputePaddingSizeZeroMaxReturnsZero) {
  profile_.max_padding_size = 0;
  EXPECT_EQ(compute_padding_size(profile_, 0), 0U);
}

TEST_F(ObfuscationProfileTest, ComputePrefixSizeWithinBounds) {
  for (std::uint64_t seq = 0; seq < 1000; ++seq) {
    auto size = compute_prefix_size(profile_, seq);
    EXPECT_GE(size, profile_.min_prefix_size);
    EXPECT_LE(size, profile_.max_prefix_size);
  }
}

TEST_F(ObfuscationProfileTest, ComputePrefixSizeIsDeterministic) {
  for (std::uint64_t seq = 0; seq < 100; ++seq) {
    auto size1 = compute_prefix_size(profile_, seq);
    auto size2 = compute_prefix_size(profile_, seq);
    EXPECT_EQ(size1, size2);
  }
}

TEST_F(ObfuscationProfileTest, ComputePrefixSizeDisabledReturnsZero) {
  profile_.enabled = false;
  EXPECT_EQ(compute_prefix_size(profile_, 0), 0U);
}

TEST_F(ObfuscationProfileTest, ComputeTimingJitterWithinBounds) {
  for (std::uint64_t seq = 0; seq < 1000; ++seq) {
    auto jitter = compute_timing_jitter(profile_, seq);
    EXPECT_LE(jitter, profile_.max_timing_jitter_ms);
  }
}

TEST_F(ObfuscationProfileTest, ComputeTimingJitterDisabled) {
  profile_.timing_jitter_enabled = false;
  EXPECT_EQ(compute_timing_jitter(profile_, 0), 0U);
  EXPECT_EQ(compute_timing_jitter(profile_, 100), 0U);
}

TEST_F(ObfuscationProfileTest, ComputeHeartbeatIntervalWithinBounds) {
  auto min_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(profile_.heartbeat_min).count();
  auto max_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(profile_.heartbeat_max).count();

  for (std::uint64_t count = 0; count < 1000; ++count) {
    auto interval = compute_heartbeat_interval(profile_, count);
    EXPECT_GE(interval.count(), min_ms);
    EXPECT_LE(interval.count(), max_ms);
  }
}

TEST_F(ObfuscationProfileTest, ComputeHeartbeatIntervalIsDeterministic) {
  for (std::uint64_t count = 0; count < 100; ++count) {
    auto interval1 = compute_heartbeat_interval(profile_, count);
    auto interval2 = compute_heartbeat_interval(profile_, count);
    EXPECT_EQ(interval1, interval2);
  }
}

TEST_F(ObfuscationProfileTest, ConfigToProfileWithAutoSeed) {
  ObfuscationConfig config;
  config.enabled = true;
  config.max_padding_size = 200;
  config.profile_seed_hex = "auto";
  config.heartbeat_interval_min = std::chrono::seconds(10);
  config.heartbeat_interval_max = std::chrono::seconds(30);
  config.enable_timing_jitter = false;

  auto profile = config_to_profile(config);

  EXPECT_TRUE(profile.enabled);
  EXPECT_EQ(profile.max_padding_size, 200U);
  EXPECT_EQ(profile.heartbeat_min, std::chrono::seconds(10));
  EXPECT_EQ(profile.heartbeat_max, std::chrono::seconds(30));
  EXPECT_FALSE(profile.timing_jitter_enabled);

  // Seed should be generated (not all zeros).
  bool all_zero =
      std::all_of(profile.profile_seed.begin(), profile.profile_seed.end(),
                  [](uint8_t b) { return b == 0; });
  EXPECT_FALSE(all_zero);
}

TEST_F(ObfuscationProfileTest, ConfigToProfileWithHexSeed) {
  ObfuscationConfig config;
  config.enabled = true;
  config.max_padding_size = 100;
  // 32-byte seed in hex (64 hex chars).
  config.profile_seed_hex =
      "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
  config.heartbeat_interval_min = std::chrono::seconds(5);
  config.heartbeat_interval_max = std::chrono::seconds(15);
  config.enable_timing_jitter = true;

  auto profile = config_to_profile(config);

  EXPECT_TRUE(profile.enabled);

  // Check seed was parsed correctly.
  EXPECT_EQ(profile.profile_seed[0], 0x01);
  EXPECT_EQ(profile.profile_seed[1], 0x02);
  EXPECT_EQ(profile.profile_seed[31], 0x20);
}

TEST_F(ObfuscationProfileTest, ParseObfuscationConfig) {
  auto config = parse_obfuscation_config("true", "500", "auto", "10", "30", "true");

  ASSERT_TRUE(config.has_value());
  EXPECT_TRUE(config->enabled);
  EXPECT_EQ(config->max_padding_size, 500U);
  EXPECT_EQ(config->profile_seed_hex, "auto");
  EXPECT_EQ(config->heartbeat_interval_min, std::chrono::seconds(10));
  EXPECT_EQ(config->heartbeat_interval_max, std::chrono::seconds(30));
  EXPECT_TRUE(config->enable_timing_jitter);
}

TEST_F(ObfuscationProfileTest, ParseObfuscationConfigDisabled) {
  auto config = parse_obfuscation_config("false", "100", "auto", "5", "15", "false");

  ASSERT_TRUE(config.has_value());
  EXPECT_FALSE(config->enabled);
  EXPECT_FALSE(config->enable_timing_jitter);
}

TEST_F(ObfuscationProfileTest, DifferentSeedsProduceDifferentResults) {
  ObfuscationProfile profile2 = profile_;
  // Change the seed.
  profile2.profile_seed[0] = 0xFF;

  // Results should differ for same sequence.
  EXPECT_NE(compute_padding_size(profile_, 0), compute_padding_size(profile2, 0));
  EXPECT_NE(compute_prefix_size(profile_, 0), compute_prefix_size(profile2, 0));
}

}  // namespace veil::obfuscation::tests
