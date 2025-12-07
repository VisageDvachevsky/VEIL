#pragma once

#include <spdlog/spdlog.h>
#include <string>

namespace veil::utils {

// Log levels
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    CRITICAL,
    OFF
};

// Initialize logging
void init_logging(LogLevel level = LogLevel::INFO, const std::string& pattern = "");

// Set log level
void set_log_level(LogLevel level);

// Get current log level
LogLevel get_log_level();

// Convert log level to string
const char* log_level_to_string(LogLevel level);

// Convert string to log level
LogLevel string_to_log_level(const std::string& str);

}  // namespace veil::utils
