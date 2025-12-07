#include "veil/crypto/x25519.hpp"

#include <sodium.h>

namespace veil::crypto {

X25519KeyPair generate_keypair() {
    X25519KeyPair kp;
    crypto_box_keypair(kp.public_key.data(), kp.secret_key.data());
    return kp;
}

PublicKey derive_public_key(const SecretKey& secret_key) {
    PublicKey pk;
    crypto_scalarmult_base(pk.data(), secret_key.data());
    return pk;
}

std::optional<SharedSecret> key_exchange(const SecretKey& our_secret,
                                          const PublicKey& their_public) {
    SharedSecret shared;

    // crypto_scalarmult returns 0 on success, -1 on failure (weak key)
    if (crypto_scalarmult(shared.data(), our_secret.data(), their_public.data()) != 0) {
        return std::nullopt;
    }

    // Additional check for all-zero output (indicates weak key)
    bool all_zero = true;
    for (auto b : shared) {
        if (b != 0) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        return std::nullopt;
    }

    return shared;
}

}  // namespace veil::crypto
