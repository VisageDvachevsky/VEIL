#include "veil/config/config.hpp"

#include <CLI/CLI.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace veil::config {

namespace {

// Simple INI parser
class IniParser {
public:
    struct Entry {
        std::string section;
        std::string key;
        std::string value;
    };

    static std::vector<Entry> parse(std::istream& input) {
        std::vector<Entry> entries;
        std::string current_section;
        std::string line;

        while (std::getline(input, line)) {
            // Trim whitespace
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#' || line[0] == ';') {
                continue;
            }

            // Section header
            if (line[0] == '[' && line.back() == ']') {
                current_section = line.substr(1, line.size() - 2);
                continue;
            }

            // Key=value
            auto eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);

                // Trim
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));

                // Remove quotes
                if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                    value = value.substr(1, value.size() - 2);
                }

                entries.push_back({current_section, key, value});
            }
        }

        return entries;
    }
};

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

}  // namespace

std::optional<transport::TransportSessionConfig> load_config(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    auto entries = IniParser::parse(file);
    transport::TransportSessionConfig config;

    for (const auto& entry : entries) {
        std::string section = to_lower(entry.section);
        std::string key = to_lower(entry.key);

        if (section == "network" || section.empty()) {
            if (key == "local_host" || key == "bind") {
                config.local_address.host = entry.value;
            } else if (key == "local_port") {
                config.local_address.port = static_cast<uint16_t>(std::stoi(entry.value));
            } else if (key == "peer_host" || key == "remote") {
                config.peer_address.host = entry.value;
            } else if (key == "peer_port" || key == "remote_port") {
                config.peer_address.port = static_cast<uint16_t>(std::stoi(entry.value));
            } else if (key == "mtu") {
                config.mtu = std::stoul(entry.value);
            }
        } else if (section == "security") {
            if (key == "psk") {
                // Convert hex string to bytes
                std::string hex = entry.value;
                if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
                    hex = hex.substr(2);
                }
                size_t len = std::min(hex.size() / 2, config.psk.size());
                for (size_t i = 0; i < len; ++i) {
                    config.psk[i] = static_cast<uint8_t>(
                        std::stoul(hex.substr(i * 2, 2), nullptr, 16));
                }
            }
        } else if (section == "rate_limiter") {
            if (key == "packets_per_second") {
                config.rate_limiter.packets_per_second = std::stoull(entry.value);
            } else if (key == "bytes_per_second") {
                config.rate_limiter.bytes_per_second = std::stoull(entry.value);
            } else if (key == "burst_packets") {
                config.rate_limiter.burst_packets = std::stoull(entry.value);
            } else if (key == "burst_bytes") {
                config.rate_limiter.burst_bytes = std::stoull(entry.value);
            }
        } else if (section == "session") {
            if (key == "packets_per_session") {
                config.session_rotator.packets_per_session = std::stoull(entry.value);
            } else if (key == "bytes_per_session") {
                config.session_rotator.bytes_per_session = std::stoull(entry.value);
            } else if (key == "seconds_per_session") {
                config.session_rotator.seconds_per_session = std::stoull(entry.value);
            }
        }
    }

    return config;
}

std::optional<transport::TransportSessionConfig> parse_cli(int argc, char* argv[]) {
    CLI::App app{"VEIL - Encrypted UDP Transport"};

    transport::TransportSessionConfig config;
    std::string psk_hex;

    app.add_option("-b,--bind", config.local_address.host, "Local bind address");
    app.add_option("-p,--port", config.local_address.port, "Local port");
    app.add_option("-r,--remote", config.peer_address.host, "Remote peer address");
    app.add_option("--remote-port", config.peer_address.port, "Remote peer port");
    app.add_option("--mtu", config.mtu, "Maximum transmission unit");
    app.add_option("--psk", psk_hex, "Pre-shared key (hex)");
    app.add_option("--rate-limit-pps", config.rate_limiter.packets_per_second,
                   "Packets per second limit");
    app.add_option("--rate-limit-bps", config.rate_limiter.bytes_per_second,
                   "Bytes per second limit");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return std::nullopt;
    }

    // Parse PSK
    if (!psk_hex.empty()) {
        if (psk_hex.size() >= 2 && psk_hex[0] == '0' && (psk_hex[1] == 'x' || psk_hex[1] == 'X')) {
            psk_hex = psk_hex.substr(2);
        }
        size_t len = std::min(psk_hex.size() / 2, config.psk.size());
        for (size_t i = 0; i < len; ++i) {
            config.psk[i] = static_cast<uint8_t>(
                std::stoul(psk_hex.substr(i * 2, 2), nullptr, 16));
        }
    }

    return config;
}

bool save_config(const transport::TransportSessionConfig& config, const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }

    file << "[network]\n";
    file << "local_host = " << config.local_address.host << "\n";
    file << "local_port = " << config.local_address.port << "\n";
    file << "peer_host = " << config.peer_address.host << "\n";
    file << "peer_port = " << config.peer_address.port << "\n";
    file << "mtu = " << config.mtu << "\n";
    file << "\n";

    file << "[security]\n";
    file << "psk = 0x";
    for (auto b : config.psk) {
        file << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    file << "\n\n";

    file << "[rate_limiter]\n";
    file << "packets_per_second = " << config.rate_limiter.packets_per_second << "\n";
    file << "bytes_per_second = " << config.rate_limiter.bytes_per_second << "\n";
    file << "burst_packets = " << config.rate_limiter.burst_packets << "\n";
    file << "burst_bytes = " << config.rate_limiter.burst_bytes << "\n";
    file << "\n";

    file << "[session]\n";
    file << "packets_per_session = " << config.session_rotator.packets_per_session << "\n";
    file << "bytes_per_session = " << config.session_rotator.bytes_per_session << "\n";
    file << "seconds_per_session = " << config.session_rotator.seconds_per_session << "\n";

    return true;
}

transport::TransportSessionConfig merge_config(
    const transport::TransportSessionConfig& base,
    const transport::TransportSessionConfig& overlay) {

    transport::TransportSessionConfig result = base;

    // Override with non-empty/non-zero values from overlay
    if (!overlay.local_address.host.empty()) {
        result.local_address.host = overlay.local_address.host;
    }
    if (overlay.local_address.port != 0) {
        result.local_address.port = overlay.local_address.port;
    }
    if (!overlay.peer_address.host.empty()) {
        result.peer_address.host = overlay.peer_address.host;
    }
    if (overlay.peer_address.port != 0) {
        result.peer_address.port = overlay.peer_address.port;
    }
    if (overlay.mtu != 1400) {  // Check if different from default
        result.mtu = overlay.mtu;
    }

    // Check if PSK is non-zero
    bool psk_set = false;
    for (auto b : overlay.psk) {
        if (b != 0) {
            psk_set = true;
            break;
        }
    }
    if (psk_set) {
        result.psk = overlay.psk;
    }

    return result;
}

ValidationResult validate_config(const transport::TransportSessionConfig& config) {
    ValidationResult result;

    // Check local address
    if (config.local_address.port == 0) {
        result.warnings.push_back("Local port is 0 - will use ephemeral port");
    }

    // Check MTU
    if (config.mtu < 576) {
        result.errors.push_back("MTU too small (minimum 576)");
        result.valid = false;
    }
    if (config.mtu > 65535) {
        result.errors.push_back("MTU too large (maximum 65535)");
        result.valid = false;
    }

    // Check rate limiter
    if (config.rate_limiter.packets_per_second == 0) {
        result.warnings.push_back("Rate limiter packets_per_second is 0 - will block all traffic");
    }

    // Check session rotator
    if (config.session_rotator.packets_per_session == 0 &&
        config.session_rotator.bytes_per_session == 0 &&
        config.session_rotator.seconds_per_session == 0) {
        result.warnings.push_back("Session rotation is disabled - not recommended");
    }

    return result;
}

}  // namespace veil::config
