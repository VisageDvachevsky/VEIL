#pragma once

#include <span>
#include <string_view>
#include "crypto.hpp"

namespace veil::crypto {

// HMAC-SHA256
HmacDigest hmac_sha256(std::span<const uint8_t> key, std::span<const uint8_t> message);

// HKDF-SHA256 Extract
// Returns PRK (pseudorandom key)
HmacDigest hkdf_extract(std::span<const uint8_t> salt, std::span<const uint8_t> ikm);

// HKDF-SHA256 Expand
// Derives output key material from PRK
void hkdf_expand(std::span<const uint8_t> prk,
                 std::span<const uint8_t> info,
                 std::span<uint8_t> output);

// Combined HKDF Extract and Expand
void hkdf(std::span<const uint8_t> salt,
          std::span<const uint8_t> ikm,
          std::span<const uint8_t> info,
          std::span<uint8_t> output);

// Derive session keys from shared secret
// Returns (send_key, recv_key) - direction depends on initiator flag
struct SessionKeys {
    SymmetricKey send_key;
    SymmetricKey recv_key;
    Nonce send_nonce_base;
    Nonce recv_nonce_base;
};

SessionKeys derive_session_keys(const SharedSecret& shared_secret,
                                 std::span<const uint8_t> session_id,
                                 bool is_initiator);

}  // namespace veil::crypto
