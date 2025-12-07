#include "tun/routing.h"

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>

#include <system_error>

#include "common/logging/logger.h"

// Link with IP Helper API library
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

// TODO(visage): Validate routing with multiple interfaces
// TODO(visage): Improve logging verbosity mapping for Windows EventLog
// TODO(visage): Implement split-tunneling support
// TODO(visage): Test route manipulation with various network configs
// TODO(visage): Ensure consistent route cleanup on service crash
// TODO(visage): Handle metric override for default route properly

namespace {
std::error_code last_error() {
  return std::error_code(::GetLastError(), std::system_category());
}
}  // namespace

namespace veil::tun {

RouteManager::RouteManager() = default;

RouteManager::~RouteManager() { cleanup(); }

bool RouteManager::add_route(const Route& route, std::error_code& ec) {
  // TODO(visage): Implement using IP Helper API
  // Steps required:
  // 1. Convert route.destination and route.netmask to IP address and prefix length
  // 2. Get interface index from route.interface name using GetAdaptersInfo
  // 3. Create MIB_IPFORWARD_ROW2 structure
  // 4. Set dwForwardDest, dwForwardMask, dwForwardNextHop, dwForwardIfIndex, dwForwardMetric
  // 5. Call CreateIpForwardEntry or CreateIpForwardEntry2
  // 6. Track added route in added_routes_ vector for cleanup

  LOG_ERROR("Windows routing not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::remove_route(const Route& route, std::error_code& ec) {
  // TODO(visage): Implement using IP Helper API
  // Steps required:
  // 1. Same conversion as add_route
  // 2. Call DeleteIpForwardEntry or DeleteIpForwardEntry2
  // 3. Remove from added_routes_ vector

  LOG_ERROR("Windows routing not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::add_default_route(const std::string& interface, const std::string& gateway,
                                      int metric, std::error_code& ec) {
  // TODO(visage): Implement default route addition
  // This is essentially add_route with destination="0.0.0.0" and netmask="0.0.0.0"
  // but with special handling for metrics to ensure priority

  Route route{
      .destination = "0.0.0.0",
      .netmask = "0.0.0.0",
      .gateway = gateway,
      .interface = interface,
      .metric = metric,
  };
  return add_route(route, ec);
}

bool RouteManager::remove_default_route(const std::string& interface, std::error_code& ec) {
  // TODO(visage): Implement default route removal
  // Find and remove all routes with destination 0.0.0.0/0 on the specified interface

  LOG_ERROR("Windows routing not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::set_ip_forwarding(bool enable, std::error_code& ec) {
  // TODO(visage): Implement IP forwarding control
  // Steps required:
  // 1. Open registry key HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters
  // 2. Set IPEnableRouter DWORD to 1 (enable) or 0 (disable)
  // 3. May require service restart or netsh command
  // Alternative: use netsh command
  //   netsh interface ipv4 set global forwarding=enabled
  //   netsh interface ipv4 set global forwarding=disabled

  LOG_ERROR("Windows IP forwarding control not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::is_ip_forwarding_enabled(std::error_code& ec) {
  // TODO(visage): Check IP forwarding state
  // Read IPEnableRouter from registry or use netsh command

  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::configure_nat(const NatConfig& config, std::error_code& ec) {
  // TODO(visage): Implement NAT using Windows ICS or netsh
  // Options:
  // 1. Use Internet Connection Sharing (ICS) APIs
  // 2. Use netsh commands:
  //    netsh routing ip nat install
  //    netsh routing ip nat add interface <internal> private
  //    netsh routing ip nat add interface <external> full
  // 3. Use Windows Routing and Remote Access Service (RRAS) APIs

  // Note: Windows NAT is more complex than Linux iptables
  // May need to create a NAT adapter or use built-in Windows NAT features

  LOG_ERROR("Windows NAT configuration not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::remove_nat(const NatConfig& config, std::error_code& ec) {
  // TODO(visage): Remove NAT configuration
  // Reverse of configure_nat

  LOG_ERROR("Windows NAT removal not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

std::optional<SystemState> RouteManager::get_system_state(std::error_code& ec) {
  // TODO(visage): Implement system state retrieval
  // Steps required:
  // 1. Check IP forwarding state (see is_ip_forwarding_enabled)
  // 2. Get default gateway using GetIpForwardTable
  // 3. Find default interface from routing table (destination 0.0.0.0/0)
  // 4. Get interface name from index using GetAdaptersInfo

  ec = std::make_error_code(std::errc::function_not_supported);
  return std::nullopt;
}

bool RouteManager::save_routes(std::error_code& ec) {
  // TODO(visage): Save current routing table
  // Steps required:
  // 1. Call GetIpForwardTable to get all routes
  // 2. Save to member variable or file for later restoration
  // 3. Consider saving to registry or config file for persistence

  LOG_ERROR("Windows route saving not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::restore_routes(std::error_code& ec) {
  // TODO(visage): Restore saved routing table
  // Steps required:
  // 1. Read saved routes
  // 2. For each route, call CreateIpForwardEntry

  LOG_ERROR("Windows route restoration not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool RouteManager::route_exists(const Route& route, std::error_code& ec) {
  // TODO(visage): Check if route exists
  // Steps required:
  // 1. Call GetIpForwardTable
  // 2. Search for matching route entry

  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

void RouteManager::cleanup() {
  // TODO(visage): Cleanup all routes added by this manager
  // Steps required:
  // 1. Iterate through added_routes_
  // 2. Call remove_route for each
  // 3. Restore original IP forwarding state if changed
  // 4. Remove NAT configuration if configured

  if (!added_routes_.empty()) {
    LOG_INFO("Cleaning up {} routes (not yet implemented)", added_routes_.size());
    added_routes_.clear();
  }

  if (nat_configured_) {
    LOG_INFO("Removing NAT configuration (not yet implemented)");
    nat_configured_ = false;
  }
}

std::optional<std::string> RouteManager::execute_command(const std::string& command,
                                                          std::error_code& ec) {
  // TODO(visage): Implement Windows command execution if needed
  // Note: Prefer using IP Helper API instead of shell commands
  // But this may be useful for netsh commands as a fallback

  LOG_ERROR("Windows command execution not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return std::nullopt;
}

bool RouteManager::execute_command_check(const std::string& command, std::error_code& ec) {
  auto result = execute_command(command, ec);
  return result.has_value() && !ec;
}

std::string RouteManager::build_nat_command(const NatConfig& config, bool add) {
  // TODO(visage): Build Windows NAT command string
  // This is a helper for netsh-based NAT configuration
  return "";
}

}  // namespace veil::tun

#endif  // _WIN32
