#pragma once

#include <optional>
#include "crypto.hpp"

namespace veil::crypto {

// X25519 key pair
struct X25519KeyPair {
    SecretKey secret_key;
    PublicKey public_key;
};

// Generate a new X25519 key pair
X25519KeyPair generate_keypair();

// Derive public key from secret key
PublicKey derive_public_key(const SecretKey& secret_key);

// Perform X25519 key exchange
// Returns shared secret on success, nullopt on failure (e.g., weak public key)
std::optional<SharedSecret> key_exchange(const SecretKey& our_secret,
                                          const PublicKey& their_public);

}  // namespace veil::crypto
