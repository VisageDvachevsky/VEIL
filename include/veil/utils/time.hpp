#pragma once

#include <cstdint>
#include <chrono>

namespace veil::utils {

// Get current time in milliseconds (monotonic)
inline uint64_t time_ms() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

// Get current time in microseconds (monotonic)
inline uint64_t time_us() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

// Get current Unix timestamp in seconds
inline uint64_t unix_time() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

// Simple timer class
class Timer {
public:
    Timer() : start_(std::chrono::steady_clock::now()) {}

    void reset() {
        start_ = std::chrono::steady_clock::now();
    }

    [[nodiscard]] uint64_t elapsed_ms() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - start_).count();
    }

    [[nodiscard]] uint64_t elapsed_us() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

}  // namespace veil::utils
