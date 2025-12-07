#include "veil/utils/logging.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <algorithm>
#include <cctype>

namespace veil::utils {

namespace {
LogLevel current_level = LogLevel::INFO;

spdlog::level::level_enum to_spdlog_level(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return spdlog::level::trace;
        case LogLevel::DEBUG: return spdlog::level::debug;
        case LogLevel::INFO: return spdlog::level::info;
        case LogLevel::WARN: return spdlog::level::warn;
        case LogLevel::ERROR: return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
        case LogLevel::OFF: return spdlog::level::off;
    }
    return spdlog::level::info;
}
}  // namespace

void init_logging(LogLevel level, const std::string& pattern) {
    current_level = level;

    auto console = spdlog::stdout_color_mt("veil");
    spdlog::set_default_logger(console);
    spdlog::set_level(to_spdlog_level(level));

    if (!pattern.empty()) {
        spdlog::set_pattern(pattern);
    } else {
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
    }
}

void set_log_level(LogLevel level) {
    current_level = level;
    spdlog::set_level(to_spdlog_level(level));
}

LogLevel get_log_level() {
    return current_level;
}

const char* log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "trace";
        case LogLevel::DEBUG: return "debug";
        case LogLevel::INFO: return "info";
        case LogLevel::WARN: return "warn";
        case LogLevel::ERROR: return "error";
        case LogLevel::CRITICAL: return "critical";
        case LogLevel::OFF: return "off";
    }
    return "info";
}

LogLevel string_to_log_level(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "trace") return LogLevel::TRACE;
    if (lower == "debug") return LogLevel::DEBUG;
    if (lower == "info") return LogLevel::INFO;
    if (lower == "warn" || lower == "warning") return LogLevel::WARN;
    if (lower == "error" || lower == "err") return LogLevel::ERROR;
    if (lower == "critical" || lower == "fatal") return LogLevel::CRITICAL;
    if (lower == "off" || lower == "none") return LogLevel::OFF;

    return LogLevel::INFO;
}

}  // namespace veil::utils
