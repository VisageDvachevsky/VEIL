#pragma once

#include <optional>
#include <span>
#include <vector>
#include "crypto.hpp"

namespace veil::crypto {

// ChaCha20-Poly1305 AEAD encryption
// Returns ciphertext + tag (16 bytes appended)
std::vector<uint8_t> encrypt(const SymmetricKey& key,
                              const Nonce& nonce,
                              std::span<const uint8_t> plaintext,
                              std::span<const uint8_t> additional_data = {});

// ChaCha20-Poly1305 AEAD decryption
// Expects ciphertext + tag (16 bytes appended)
// Returns plaintext on success, nullopt on authentication failure
std::optional<std::vector<uint8_t>> decrypt(const SymmetricKey& key,
                                             const Nonce& nonce,
                                             std::span<const uint8_t> ciphertext_with_tag,
                                             std::span<const uint8_t> additional_data = {});

// In-place encryption (ciphertext must have room for 16-byte tag)
bool encrypt_inplace(const SymmetricKey& key,
                     const Nonce& nonce,
                     std::span<uint8_t> plaintext_out_ciphertext,
                     std::span<uint8_t> tag_out,
                     std::span<const uint8_t> additional_data = {});

// In-place decryption
bool decrypt_inplace(const SymmetricKey& key,
                     const Nonce& nonce,
                     std::span<uint8_t> ciphertext_out_plaintext,
                     std::span<const uint8_t> tag,
                     std::span<const uint8_t> additional_data = {});

// Increment nonce (for packet counter)
void increment_nonce(Nonce& nonce);

// Create nonce from base and counter
Nonce make_nonce(const Nonce& base, uint64_t counter);

}  // namespace veil::crypto
