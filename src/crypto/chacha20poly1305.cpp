#include "veil/crypto/chacha20poly1305.hpp"

#include <sodium.h>
#include <cstring>

namespace veil::crypto {

std::vector<uint8_t> encrypt(const SymmetricKey& key,
                              const Nonce& nonce,
                              std::span<const uint8_t> plaintext,
                              std::span<const uint8_t> additional_data) {
    std::vector<uint8_t> ciphertext(plaintext.size() + POLY1305_TAG_SIZE);
    unsigned long long ciphertext_len;

    crypto_aead_chacha20poly1305_ietf_encrypt(
        ciphertext.data(), &ciphertext_len,
        plaintext.data(), plaintext.size(),
        additional_data.data(), additional_data.size(),
        nullptr,  // nsec (unused)
        nonce.data(),
        key.data()
    );

    ciphertext.resize(static_cast<size_t>(ciphertext_len));
    return ciphertext;
}

std::optional<std::vector<uint8_t>> decrypt(const SymmetricKey& key,
                                             const Nonce& nonce,
                                             std::span<const uint8_t> ciphertext_with_tag,
                                             std::span<const uint8_t> additional_data) {
    if (ciphertext_with_tag.size() < POLY1305_TAG_SIZE) {
        return std::nullopt;
    }

    std::vector<uint8_t> plaintext(ciphertext_with_tag.size() - POLY1305_TAG_SIZE);
    unsigned long long plaintext_len;

    if (crypto_aead_chacha20poly1305_ietf_decrypt(
            plaintext.data(), &plaintext_len,
            nullptr,  // nsec (unused)
            ciphertext_with_tag.data(), ciphertext_with_tag.size(),
            additional_data.data(), additional_data.size(),
            nonce.data(),
            key.data()
        ) != 0) {
        return std::nullopt;
    }

    plaintext.resize(static_cast<size_t>(plaintext_len));
    return plaintext;
}

bool encrypt_inplace(const SymmetricKey& key,
                     const Nonce& nonce,
                     std::span<uint8_t> plaintext_out_ciphertext,
                     std::span<uint8_t> tag_out,
                     std::span<const uint8_t> additional_data) {
    if (tag_out.size() < POLY1305_TAG_SIZE) {
        return false;
    }

    unsigned long long tag_len;
    crypto_aead_chacha20poly1305_ietf_encrypt_detached(
        plaintext_out_ciphertext.data(),
        tag_out.data(), &tag_len,
        plaintext_out_ciphertext.data(), plaintext_out_ciphertext.size(),
        additional_data.data(), additional_data.size(),
        nullptr,  // nsec
        nonce.data(),
        key.data()
    );

    return true;
}

bool decrypt_inplace(const SymmetricKey& key,
                     const Nonce& nonce,
                     std::span<uint8_t> ciphertext_out_plaintext,
                     std::span<const uint8_t> tag,
                     std::span<const uint8_t> additional_data) {
    if (tag.size() < POLY1305_TAG_SIZE) {
        return false;
    }

    return crypto_aead_chacha20poly1305_ietf_decrypt_detached(
        ciphertext_out_plaintext.data(),
        nullptr,  // nsec
        ciphertext_out_plaintext.data(), ciphertext_out_plaintext.size(),
        tag.data(),
        additional_data.data(), additional_data.size(),
        nonce.data(),
        key.data()
    ) == 0;
}

void increment_nonce(Nonce& nonce) {
    sodium_increment(nonce.data(), nonce.size());
}

Nonce make_nonce(const Nonce& base, uint64_t counter) {
    Nonce result = base;

    // XOR counter into the last 8 bytes of the nonce
    for (size_t i = 0; i < 8; ++i) {
        result[CHACHA20_NONCE_SIZE - 8 + i] ^= static_cast<uint8_t>(counter >> (i * 8));
    }

    return result;
}

}  // namespace veil::crypto
