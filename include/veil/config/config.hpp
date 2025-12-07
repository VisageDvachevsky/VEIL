#pragma once

#include <optional>
#include <string>
#include <variant>

#include "veil/transport/transport_session.hpp"

namespace veil::config {

// Configuration file format
enum class ConfigFormat {
    AUTO,  // Detect from extension
    INI,
    CLI
};

// Configuration source
struct ConfigSource {
    std::string path;       // File path or empty for CLI
    ConfigFormat format{ConfigFormat::AUTO};
};

// Parse configuration from file
std::optional<transport::TransportSessionConfig> load_config(const std::string& path);

// Parse configuration from CLI arguments
std::optional<transport::TransportSessionConfig> parse_cli(int argc, char* argv[]);

// Save configuration to file
bool save_config(const transport::TransportSessionConfig& config, const std::string& path);

// Merge CLI arguments over config file
transport::TransportSessionConfig merge_config(
    const transport::TransportSessionConfig& base,
    const transport::TransportSessionConfig& overlay);

// Validate configuration
struct ValidationResult {
    bool valid{true};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

ValidationResult validate_config(const transport::TransportSessionConfig& config);

}  // namespace veil::config
