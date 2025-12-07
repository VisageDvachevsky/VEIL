#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace veil::crypto {

// Key sizes
constexpr size_t X25519_KEY_SIZE = 32;
constexpr size_t X25519_PUBLIC_KEY_SIZE = 32;
constexpr size_t X25519_SECRET_KEY_SIZE = 32;
constexpr size_t CHACHA20_KEY_SIZE = 32;
constexpr size_t CHACHA20_NONCE_SIZE = 12;
constexpr size_t POLY1305_TAG_SIZE = 16;
constexpr size_t HMAC_SHA256_SIZE = 32;
constexpr size_t HKDF_SALT_SIZE = 32;

using SecretKey = std::array<uint8_t, X25519_SECRET_KEY_SIZE>;
using PublicKey = std::array<uint8_t, X25519_PUBLIC_KEY_SIZE>;
using SharedSecret = std::array<uint8_t, X25519_KEY_SIZE>;
using SymmetricKey = std::array<uint8_t, CHACHA20_KEY_SIZE>;
using Nonce = std::array<uint8_t, CHACHA20_NONCE_SIZE>;
using AuthTag = std::array<uint8_t, POLY1305_TAG_SIZE>;
using HmacDigest = std::array<uint8_t, HMAC_SHA256_SIZE>;

// Initialize the crypto subsystem
bool init();

// Securely zero memory
void secure_zero(void* ptr, size_t len);

// Generate random bytes
void random_bytes(std::span<uint8_t> output);

// Constant-time comparison
bool constant_time_compare(std::span<const uint8_t> a, std::span<const uint8_t> b);

}  // namespace veil::crypto
