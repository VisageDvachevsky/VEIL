#include "veil/crypto/crypto.hpp"

#include <sodium.h>
#include <stdexcept>

namespace veil::crypto {

bool init() {
    if (sodium_init() < 0) {
        return false;
    }
    return true;
}

void secure_zero(void* ptr, size_t len) {
    sodium_memzero(ptr, len);
}

void random_bytes(std::span<uint8_t> output) {
    randombytes_buf(output.data(), output.size());
}

bool constant_time_compare(std::span<const uint8_t> a, std::span<const uint8_t> b) {
    if (a.size() != b.size()) {
        return false;
    }
    return sodium_memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace veil::crypto
