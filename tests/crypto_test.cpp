#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "veil/crypto/crypto.hpp"
#include "veil/crypto/x25519.hpp"
#include "veil/crypto/hkdf.hpp"
#include "veil/crypto/chacha20poly1305.hpp"

namespace veil::crypto {
namespace {

class CryptoTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(init());
    }
};

TEST_F(CryptoTest, RandomBytesGeneratesNonZero) {
    std::array<uint8_t, 32> bytes{};
    random_bytes(bytes);

    // Extremely unlikely all 32 bytes are zero
    bool all_zero = true;
    for (auto b : bytes) {
        if (b != 0) {
            all_zero = false;
            break;
        }
    }
    EXPECT_FALSE(all_zero);
}

TEST_F(CryptoTest, RandomBytesGeneratesDifferentValues) {
    std::array<uint8_t, 32> bytes1{}, bytes2{};
    random_bytes(bytes1);
    random_bytes(bytes2);

    EXPECT_NE(bytes1, bytes2);
}

TEST_F(CryptoTest, ConstantTimeCompareEqual) {
    std::array<uint8_t, 32> a, b;
    random_bytes(a);
    b = a;

    EXPECT_TRUE(constant_time_compare(a, b));
}

TEST_F(CryptoTest, ConstantTimeCompareNotEqual) {
    std::array<uint8_t, 32> a, b;
    random_bytes(a);
    random_bytes(b);

    EXPECT_FALSE(constant_time_compare(a, b));
}

TEST_F(CryptoTest, ConstantTimeCompareDifferentSize) {
    std::vector<uint8_t> a = {1, 2, 3};
    std::vector<uint8_t> b = {1, 2, 3, 4};

    EXPECT_FALSE(constant_time_compare(a, b));
}

TEST_F(CryptoTest, X25519KeyPairGeneration) {
    auto kp1 = generate_keypair();
    auto kp2 = generate_keypair();

    // Keys should be different
    EXPECT_NE(kp1.secret_key, kp2.secret_key);
    EXPECT_NE(kp1.public_key, kp2.public_key);
}

TEST_F(CryptoTest, X25519PublicKeyDerivation) {
    auto kp = generate_keypair();
    auto derived_pk = derive_public_key(kp.secret_key);

    EXPECT_EQ(kp.public_key, derived_pk);
}

TEST_F(CryptoTest, X25519KeyExchange) {
    auto alice = generate_keypair();
    auto bob = generate_keypair();

    auto alice_shared = key_exchange(alice.secret_key, bob.public_key);
    auto bob_shared = key_exchange(bob.secret_key, alice.public_key);

    ASSERT_TRUE(alice_shared.has_value());
    ASSERT_TRUE(bob_shared.has_value());
    EXPECT_EQ(*alice_shared, *bob_shared);
}

TEST_F(CryptoTest, X25519WeakKeyRejected) {
    PublicKey weak_key{};  // All zeros is a weak key

    auto kp = generate_keypair();
    auto shared = key_exchange(kp.secret_key, weak_key);

    EXPECT_FALSE(shared.has_value());
}

TEST_F(CryptoTest, HmacSha256) {
    std::vector<uint8_t> key = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> message = {0x48, 0x65, 0x6c, 0x6c, 0x6f};  // "Hello"

    auto hmac1 = hmac_sha256(key, message);
    auto hmac2 = hmac_sha256(key, message);

    EXPECT_EQ(hmac1, hmac2);
}

TEST_F(CryptoTest, HmacSha256DifferentKeys) {
    std::vector<uint8_t> key1 = {0x01, 0x02, 0x03, 0x04};
    std::vector<uint8_t> key2 = {0x05, 0x06, 0x07, 0x08};
    std::vector<uint8_t> message = {0x48, 0x65, 0x6c, 0x6c, 0x6f};

    auto hmac1 = hmac_sha256(key1, message);
    auto hmac2 = hmac_sha256(key2, message);

    EXPECT_NE(hmac1, hmac2);
}

TEST_F(CryptoTest, HkdfExtract) {
    std::vector<uint8_t> salt = {0x00, 0x01, 0x02, 0x03};
    std::vector<uint8_t> ikm = {0x0b, 0x0b, 0x0b, 0x0b};

    auto prk = hkdf_extract(salt, ikm);

    EXPECT_EQ(prk.size(), 32u);
}

TEST_F(CryptoTest, HkdfExpandAndContract) {
    std::vector<uint8_t> salt = {0x00, 0x01, 0x02, 0x03};
    std::vector<uint8_t> ikm = {0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b};
    std::vector<uint8_t> info = {0xf0, 0xf1, 0xf2};

    auto prk = hkdf_extract(salt, ikm);

    std::array<uint8_t, 32> okm32;
    std::array<uint8_t, 64> okm64;

    hkdf_expand(prk, info, okm32);
    hkdf_expand(prk, info, okm64);

    // First 32 bytes should match
    EXPECT_TRUE(std::equal(okm32.begin(), okm32.end(), okm64.begin()));
}

TEST_F(CryptoTest, SessionKeysDerivation) {
    SharedSecret shared;
    random_bytes(shared);

    std::array<uint8_t, 32> session_id;
    random_bytes(session_id);

    auto initiator_keys = derive_session_keys(shared, session_id, true);
    auto responder_keys = derive_session_keys(shared, session_id, false);

    // Initiator's send key should be responder's recv key
    EXPECT_EQ(initiator_keys.send_key, responder_keys.recv_key);
    EXPECT_EQ(initiator_keys.recv_key, responder_keys.send_key);
    EXPECT_EQ(initiator_keys.send_nonce_base, responder_keys.recv_nonce_base);
    EXPECT_EQ(initiator_keys.recv_nonce_base, responder_keys.send_nonce_base);
}

TEST_F(CryptoTest, ChaCha20Poly1305EncryptDecrypt) {
    SymmetricKey key;
    Nonce nonce;
    random_bytes(key);
    random_bytes(nonce);

    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x21};
    std::vector<uint8_t> aad = {0xad, 0xad};

    auto ciphertext = encrypt(key, nonce, plaintext, aad);
    EXPECT_EQ(ciphertext.size(), plaintext.size() + POLY1305_TAG_SIZE);

    auto decrypted = decrypt(key, nonce, ciphertext, aad);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(*decrypted, plaintext);
}

TEST_F(CryptoTest, ChaCha20Poly1305TamperedCiphertext) {
    SymmetricKey key;
    Nonce nonce;
    random_bytes(key);
    random_bytes(nonce);

    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    auto ciphertext = encrypt(key, nonce, plaintext);

    // Tamper with ciphertext
    ciphertext[0] ^= 0xFF;

    auto decrypted = decrypt(key, nonce, ciphertext);
    EXPECT_FALSE(decrypted.has_value());
}

TEST_F(CryptoTest, ChaCha20Poly1305WrongKey) {
    SymmetricKey key1, key2;
    Nonce nonce;
    random_bytes(key1);
    random_bytes(key2);
    random_bytes(nonce);

    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    auto ciphertext = encrypt(key1, nonce, plaintext);

    auto decrypted = decrypt(key2, nonce, ciphertext);
    EXPECT_FALSE(decrypted.has_value());
}

TEST_F(CryptoTest, ChaCha20Poly1305WrongNonce) {
    SymmetricKey key;
    Nonce nonce1, nonce2;
    random_bytes(key);
    random_bytes(nonce1);
    random_bytes(nonce2);

    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    auto ciphertext = encrypt(key, nonce1, plaintext);

    auto decrypted = decrypt(key, nonce2, ciphertext);
    EXPECT_FALSE(decrypted.has_value());
}

TEST_F(CryptoTest, ChaCha20Poly1305WrongAAD) {
    SymmetricKey key;
    Nonce nonce;
    random_bytes(key);
    random_bytes(nonce);

    std::vector<uint8_t> plaintext = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    std::vector<uint8_t> aad1 = {0x01, 0x02};
    std::vector<uint8_t> aad2 = {0x03, 0x04};

    auto ciphertext = encrypt(key, nonce, plaintext, aad1);
    auto decrypted = decrypt(key, nonce, ciphertext, aad2);

    EXPECT_FALSE(decrypted.has_value());
}

TEST_F(CryptoTest, NonceIncrement) {
    Nonce nonce{};
    increment_nonce(nonce);

    // libsodium's sodium_increment uses little-endian (increments from byte 0)
    EXPECT_EQ(nonce[0], 1);  // First byte should be 1
}

TEST_F(CryptoTest, NonceIncrementOverflow) {
    Nonce nonce{};
    nonce[0] = 0xFF;  // Set first byte to max

    increment_nonce(nonce);

    // Should overflow to next byte (little-endian)
    EXPECT_EQ(nonce[0], 0);
    EXPECT_EQ(nonce[1], 1);
}

TEST_F(CryptoTest, MakeNonceFromBase) {
    Nonce base{};
    random_bytes(base);

    auto n0 = make_nonce(base, 0);
    auto n1 = make_nonce(base, 1);
    auto n256 = make_nonce(base, 256);

    EXPECT_NE(n0, n1);
    EXPECT_NE(n1, n256);

    // Counter 0 XOR base should equal base
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(n0[i], base[i]);
    }
}

TEST_F(CryptoTest, InPlaceEncryptDecrypt) {
    SymmetricKey key;
    Nonce nonce;
    random_bytes(key);
    random_bytes(nonce);

    std::vector<uint8_t> data = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
    std::vector<uint8_t> original = data;
    AuthTag tag;

    EXPECT_TRUE(encrypt_inplace(key, nonce, data, tag));

    // Data should be encrypted (different from original)
    EXPECT_NE(data, original);

    EXPECT_TRUE(decrypt_inplace(key, nonce, data, tag));

    // Data should be restored
    EXPECT_EQ(data, original);
}

}  // namespace
}  // namespace veil::crypto
