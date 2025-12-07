#pragma once

#include <spdlog/common.h>

#include <string_view>

namespace veil::logging {

enum class LogLevel {
  trace,
  debug,
  info,
  warn,
  error,
  critical,
  off
};

LogLevel parse_log_level(std::string_view value);
spdlog::level::level_enum to_spdlog_level(LogLevel level);
void configure_logging(LogLevel level, bool to_stdout);

}  // namespace veil::logging
