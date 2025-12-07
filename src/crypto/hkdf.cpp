#include "veil/crypto/hkdf.hpp"

#include <sodium.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace veil::crypto {

namespace {
constexpr size_t HASH_LEN = 32;  // SHA-256 output length

// Helper to compute HMAC-SHA256 using libsodium's crypto_auth_hmacsha256
void hmac_sha256_impl(const uint8_t* key, size_t key_len,
                       const uint8_t* message, size_t message_len,
                       uint8_t* out) {
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, key, key_len);
    crypto_auth_hmacsha256_update(&state, message, message_len);
    crypto_auth_hmacsha256_final(&state, out);
}
}  // namespace

HmacDigest hmac_sha256(std::span<const uint8_t> key, std::span<const uint8_t> message) {
    HmacDigest digest;
    hmac_sha256_impl(key.data(), key.size(), message.data(), message.size(), digest.data());
    return digest;
}

HmacDigest hkdf_extract(std::span<const uint8_t> salt, std::span<const uint8_t> ikm) {
    HmacDigest prk;

    // If salt is empty, use a string of zeros as the salt
    if (salt.empty()) {
        std::array<uint8_t, HASH_LEN> zero_salt{};
        hmac_sha256_impl(zero_salt.data(), zero_salt.size(),
                         ikm.data(), ikm.size(), prk.data());
    } else {
        hmac_sha256_impl(salt.data(), salt.size(),
                         ikm.data(), ikm.size(), prk.data());
    }

    return prk;
}

void hkdf_expand(std::span<const uint8_t> prk,
                 std::span<const uint8_t> info,
                 std::span<uint8_t> output) {
    if (output.size() > 255 * HASH_LEN) {
        throw std::invalid_argument("HKDF output too long");
    }

    size_t n = (output.size() + HASH_LEN - 1) / HASH_LEN;
    std::array<uint8_t, HASH_LEN> t_prev{};
    size_t t_prev_len = 0;
    size_t output_pos = 0;

    for (size_t i = 1; i <= n; ++i) {
        // T(i) = HMAC(PRK, T(i-1) || info || i)
        crypto_auth_hmacsha256_state state;
        crypto_auth_hmacsha256_init(&state, prk.data(), prk.size());

        if (t_prev_len > 0) {
            crypto_auth_hmacsha256_update(&state, t_prev.data(), t_prev_len);
        }

        if (!info.empty()) {
            crypto_auth_hmacsha256_update(&state, info.data(), info.size());
        }

        uint8_t counter = static_cast<uint8_t>(i);
        crypto_auth_hmacsha256_update(&state, &counter, 1);

        crypto_auth_hmacsha256_final(&state, t_prev.data());
        t_prev_len = HASH_LEN;

        // Copy to output
        size_t to_copy = std::min<size_t>(HASH_LEN, output.size() - output_pos);
        std::memcpy(output.data() + output_pos, t_prev.data(), to_copy);
        output_pos += to_copy;
    }
}

void hkdf(std::span<const uint8_t> salt,
          std::span<const uint8_t> ikm,
          std::span<const uint8_t> info,
          std::span<uint8_t> output) {
    auto prk = hkdf_extract(salt, ikm);
    hkdf_expand(prk, info, output);
}

SessionKeys derive_session_keys(const SharedSecret& shared_secret,
                                 std::span<const uint8_t> session_id,
                                 bool is_initiator) {
    SessionKeys keys;

    // Use shared secret as IKM, session_id as salt
    auto prk = hkdf_extract(session_id, shared_secret);

    // Derive keys with different info strings
    // Info format: "veil_v1_<purpose>_<direction>"
    constexpr std::string_view prefix = "veil_v1_";

    // Derive initiator->responder key (send for initiator, recv for responder)
    {
        std::vector<uint8_t> info;
        info.reserve(prefix.size() + 16);
        info.insert(info.end(), prefix.begin(), prefix.end());
        constexpr std::string_view dir = "key_i2r";
        info.insert(info.end(), dir.begin(), dir.end());

        std::array<uint8_t, CHACHA20_KEY_SIZE> key;
        hkdf_expand(prk, info, key);

        if (is_initiator) {
            keys.send_key = key;
        } else {
            keys.recv_key = key;
        }
    }

    // Derive responder->initiator key
    {
        std::vector<uint8_t> info;
        info.reserve(prefix.size() + 16);
        info.insert(info.end(), prefix.begin(), prefix.end());
        constexpr std::string_view dir = "key_r2i";
        info.insert(info.end(), dir.begin(), dir.end());

        std::array<uint8_t, CHACHA20_KEY_SIZE> key;
        hkdf_expand(prk, info, key);

        if (is_initiator) {
            keys.recv_key = key;
        } else {
            keys.send_key = key;
        }
    }

    // Derive nonce bases
    {
        std::vector<uint8_t> info;
        info.reserve(prefix.size() + 16);
        info.insert(info.end(), prefix.begin(), prefix.end());
        constexpr std::string_view dir = "nonce_i2r";
        info.insert(info.end(), dir.begin(), dir.end());

        std::array<uint8_t, CHACHA20_NONCE_SIZE> nonce;
        hkdf_expand(prk, info, nonce);

        if (is_initiator) {
            keys.send_nonce_base = nonce;
        } else {
            keys.recv_nonce_base = nonce;
        }
    }

    {
        std::vector<uint8_t> info;
        info.reserve(prefix.size() + 16);
        info.insert(info.end(), prefix.begin(), prefix.end());
        constexpr std::string_view dir = "nonce_r2i";
        info.insert(info.end(), dir.begin(), dir.end());

        std::array<uint8_t, CHACHA20_NONCE_SIZE> nonce;
        hkdf_expand(prk, info, nonce);

        if (is_initiator) {
            keys.recv_nonce_base = nonce;
        } else {
            keys.send_nonce_base = nonce;
        }
    }

    return keys;
}

}  // namespace veil::crypto
