# Windows Routing Implementation

## Overview

This document describes the Windows routing implementation using the IP Helper API instead of shell commands.

## Platform Differences

### Linux Approach
- Uses `ip route` shell commands
- iptables for NAT/masquerading
- `/proc/sys/net/ipv4/ip_forward` for IP forwarding

### Windows Approach
- **IP Helper API** (iphlpapi.h) for all routing operations
- No shell commands (more reliable, faster, no UAC prompts)
- Registry or netsh for IP forwarding and NAT

## IP Helper API Functions

### Core Routing Functions

| Function | Purpose | Linux Equivalent |
|----------|---------|------------------|
| `CreateIpForwardEntry2` | Add route | `ip route add` |
| `DeleteIpForwardEntry2` | Remove route | `ip route del` |
| `GetIpForwardTable2` | List routes | `ip route show` |
| `SetIpForwardEntry2` | Modify route | `ip route replace` |
| `GetBestRoute2` | Find best route | `ip route get` |

### Interface Management

| Function | Purpose |
|----------|---------|
| `GetAdaptersInfo` | Get all network adapters |
| `GetAdaptersAddresses` | Get adapter details (modern API) |
| `GetIfEntry2` | Get interface statistics |
| `SetIpInterfaceEntry` | Configure interface properties |

### IP Address Management

| Function | Purpose |
|----------|---------|
| `CreateUnicastIpAddressEntry` | Add IP address |
| `DeleteUnicastIpAddressEntry` | Remove IP address |
| `GetUnicastIpAddressTable` | List IP addresses |
| `SetUnicastIpAddressEntry` | Modify IP address |

## Implementation

### Add Route

```cpp
bool RouteManager::add_route(const Route& route, std::error_code& ec) {
    // 1. Convert destination/netmask to IP and prefix length
    IN_ADDR dest, mask;
    inet_pton(AF_INET, route.destination.c_str(), &dest);
    inet_pton(AF_INET, route.netmask.c_str(), &mask);

    // Convert netmask to prefix length (e.g., 255.255.255.0 -> /24)
    ULONG prefix = mask_to_prefix_length(mask);

    // 2. Get interface LUID from name
    NET_LUID luid;
    DWORD result = ConvertInterfaceNameToLuidW(
        std::wstring(route.interface.begin(), route.interface.end()).c_str(),
        &luid
    );
    if (result != NO_ERROR) {
        ec = std::error_code(result, std::system_category());
        return false;
    }

    // 3. Create route entry
    MIB_IPFORWARD_ROW2 row = {};
    InitializeIpForwardEntry(&row);
    row.InterfaceLuid = luid;
    row.DestinationPrefix.Prefix.si_family = AF_INET;
    row.DestinationPrefix.Prefix.Ipv4.sin_addr = dest;
    row.DestinationPrefix.PrefixLength = prefix;

    if (!route.gateway.empty()) {
        inet_pton(AF_INET, route.gateway.c_str(),
                  &row.NextHop.Ipv4.sin_addr);
    }

    row.Metric = route.metric;
    row.Protocol = MIB_IPPROTO_NETMGMT;  // Manually configured

    // 4. Add route
    result = CreateIpForwardEntry2(&row);
    if (result != NO_ERROR) {
        ec = std::error_code(result, std::system_category());
        LOG_ERROR("Failed to add route: {}", ec.message());
        return false;
    }

    // 5. Track for cleanup
    added_routes_.push_back(route);
    return true;
}
```

### Remove Route

```cpp
bool RouteManager::remove_route(const Route& route, std::error_code& ec) {
    // Similar to add_route, but call DeleteIpForwardEntry2
    MIB_IPFORWARD_ROW2 row = {};
    // ... populate row same as add_route ...

    DWORD result = DeleteIpForwardEntry2(&row);
    if (result != NO_ERROR) {
        ec = std::error_code(result, std::system_category());
        return false;
    }

    // Remove from tracking
    auto it = std::find_if(added_routes_.begin(), added_routes_.end(),
        [&route](const Route& r) {
            return r.destination == route.destination &&
                   r.interface == route.interface;
        });
    if (it != added_routes_.end()) {
        added_routes_.erase(it);
    }

    return true;
}
```

### Get Routing Table

```cpp
std::optional<SystemState> RouteManager::get_system_state(std::error_code& ec) {
    SystemState state;

    // 1. Get IP forwarding state (see below)
    state.ip_forwarding_enabled = is_ip_forwarding_enabled(ec);

    // 2. Get routing table
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    DWORD result = GetIpForwardTable2(AF_INET, &table);
    if (result != NO_ERROR) {
        ec = std::error_code(result, std::system_category());
        return std::nullopt;
    }

    // 3. Find default route (0.0.0.0/0)
    for (ULONG i = 0; i < table->NumEntries; i++) {
        MIB_IPFORWARD_ROW2& row = table->Table[i];
        if (row.DestinationPrefix.PrefixLength == 0) {
            // This is a default route
            state.default_gateway = inet_ntoa(
                row.NextHop.Ipv4.sin_addr
            );

            // Convert LUID to interface name
            WCHAR ifName[IF_MAX_STRING_SIZE];
            ConvertInterfaceLuidToAlias(&row.InterfaceLuid,
                                       ifName, IF_MAX_STRING_SIZE);
            state.default_interface = /* convert wchar to string */;
            break;
        }
    }

    FreeMibTable(table);
    return state;
}
```

### Default Route Management

```cpp
bool RouteManager::add_default_route(const std::string& interface,
                                      const std::string& gateway,
                                      int metric,
                                      std::error_code& ec) {
    Route default_route{
        .destination = "0.0.0.0",
        .netmask = "0.0.0.0",  // /0
        .gateway = gateway,
        .interface = interface,
        .metric = metric
    };

    return add_route(default_route, ec);
}
```

### Metric-Based Priority

Windows uses route metrics to determine priority:
- **Lower metric = higher priority**
- Default routes typically have metric 0-300
- VPN routes should have **metric 1** to override default (metric 10+)
- Save original default route metric to restore later

```cpp
// Override default route
// 1. Get current default route metric
// 2. Add VPN default route with metric 1
// 3. On disconnect, remove VPN route (original route becomes active again)
```

## IP Forwarding

### Enable/Disable

Windows IP forwarding requires registry modification:

```cpp
bool RouteManager::set_ip_forwarding(bool enable, std::error_code& ec) {
    // Registry key: HKLM\SYSTEM\CurrentControlSet\Services\Tcpip\Parameters
    // Value: IPEnableRouter (DWORD) = 1 (enable) or 0 (disable)

    HKEY key;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
        0, KEY_SET_VALUE, &key);

    if (result != ERROR_SUCCESS) {
        ec = std::error_code(result, std::system_category());
        return false;
    }

    DWORD value = enable ? 1 : 0;
    result = RegSetValueExW(key, L"IPEnableRouter", 0, REG_DWORD,
                           (BYTE*)&value, sizeof(value));
    RegCloseKey(key);

    if (result != ERROR_SUCCESS) {
        ec = std::error_code(result, std::system_category());
        return false;
    }

    // Alternative: use netsh command (requires admin)
    // netsh interface ipv4 set global forwarding=enabled

    return true;
}
```

### Check Current State

```cpp
bool RouteManager::is_ip_forwarding_enabled(std::error_code& ec) {
    HKEY key;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters",
        0, KEY_QUERY_VALUE, &key);

    if (result != ERROR_SUCCESS) {
        ec = std::error_code(result, std::system_category());
        return false;
    }

    DWORD value = 0;
    DWORD size = sizeof(value);
    result = RegQueryValueExW(key, L"IPEnableRouter", nullptr, nullptr,
                             (BYTE*)&value, &size);
    RegCloseKey(key);

    return (result == ERROR_SUCCESS && value == 1);
}
```

## NAT Configuration

### Windows NAT Options

1. **Internet Connection Sharing (ICS)**
   - GUI-based, not recommended for programmatic control
   - COM APIs available but complex

2. **Windows Server RRAS (Routing and Remote Access Service)**
   - Enterprise feature, not available on Windows 10/11 Home
   - Requires service installation

3. **Windows Container NAT**
   - Modern NAT implementation (Windows 10+)
   - PowerShell-based: `New-NetNat`

4. **netsh routing** (Legacy)
   - Deprecated, but still works
   - Requires Windows Routing service

### Recommended: Windows NAT (Modern)

```cpp
bool RouteManager::configure_nat(const NatConfig& config, std::error_code& ec) {
    // Use PowerShell to create NAT
    // Requires Windows 10 build 14393 or later

    std::ostringstream cmd;
    cmd << "powershell -Command \"New-NetNat"
        << " -Name 'VEIL-NAT'"
        << " -InternalIPInterfaceAddressPrefix '"
        << config.internal_interface << "/24'\"";

    return execute_command_check(cmd.str(), ec);
}

bool RouteManager::remove_nat(const NatConfig& config, std::error_code& ec) {
    std::string cmd = "powershell -Command \"Remove-NetNat -Name 'VEIL-NAT' -Confirm:$false\"";
    return execute_command_check(cmd, ec);
}
```

### Alternative: netsh (Legacy, More Compatible)

```cpp
// Add NAT using netsh (Windows 7+)
// Requires "Routing and Remote Access" service

// 1. Install NAT routing protocol
// netsh routing ip nat install

// 2. Configure internal interface as private
// netsh routing ip nat add interface name="VEIL Adapter" mode=PRIVATE

// 3. Configure external interface for NAT
// netsh routing ip nat add interface name="Ethernet" mode=FULL
```

## Split Tunneling

### Implementation Strategy

Split tunneling routes only specific traffic through VPN:

```cpp
struct SplitTunnelConfig {
    std::vector<std::string> include_networks;  // Route through VPN
    std::vector<std::string> exclude_networks;  // Use default route
    bool default_to_vpn;  // If true, route everything except excludes
};

bool configure_split_tunnel(const SplitTunnelConfig& config) {
    if (config.default_to_vpn) {
        // 1. Add default route via VPN with metric 1
        // 2. Add specific routes for excluded networks via original gateway
        for (const auto& network : config.exclude_networks) {
            Route exclude_route{
                .destination = network,
                .netmask = "255.255.255.0",  // Adjust based on network
                .gateway = original_default_gateway_,
                .interface = original_default_interface_,
                .metric = 1  // Same or lower than VPN default
            };
            add_route(exclude_route, ec);
        }
    } else {
        // 1. Do NOT add default route via VPN
        // 2. Add specific routes for included networks via VPN
        for (const auto& network : config.include_networks) {
            Route include_route{
                .destination = network,
                .netmask = "255.255.255.0",
                .gateway = vpn_gateway_,
                .interface = vpn_interface_,
                .metric = 1
            };
            add_route(include_route, ec);
        }
    }
}
```

## Local Network Bypass

Ensure local network traffic doesn't go through VPN:

```cpp
void add_local_network_bypass() {
    // Common local networks
    std::vector<std::string> local_networks = {
        "10.0.0.0/8",
        "172.16.0.0/12",
        "192.168.0.0/16",
        "169.254.0.0/16",  // Link-local
        "224.0.0.0/4"      // Multicast
    };

    for (const auto& network : local_networks) {
        Route local_route = parse_cidr(network);
        local_route.gateway = "";  // Direct route (no gateway)
        local_route.interface = original_default_interface_;
        local_route.metric = 1;
        add_route(local_route, ec);
    }
}
```

## Error Handling

### Common Error Codes

| Code | Constant | Meaning | Action |
|------|----------|---------|--------|
| 5 | ERROR_ACCESS_DENIED | Insufficient privileges | Restart as admin |
| 87 | ERROR_INVALID_PARAMETER | Invalid route parameters | Validate input |
| 1168 | ERROR_NOT_FOUND | Route doesn't exist | Ignore on delete |
| 5010 | ERROR_OBJECT_ALREADY_EXISTS | Route already exists | Update existing or ignore |

### Privilege Requirements

Most IP Helper API functions require **administrator privileges**. Check before calling:

```cpp
bool is_elevated() {
    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(elevation);
        if (GetTokenInformation(token, TokenElevation, &elevation, size, &size)) {
            elevated = elevation.TokenIsElevated;
        }
        CloseHandle(token);
    }
    return elevated;
}
```

## TODO Items

- [ ] Implement route addition using CreateIpForwardEntry2
- [ ] Implement route removal using DeleteIpForwardEntry2
- [ ] Add route enumeration using GetIpForwardTable2
- [ ] Implement IP forwarding control via registry
- [ ] Add NAT configuration (choose Windows NAT or netsh approach)
- [ ] Implement split tunneling with network lists
- [ ] Add local network bypass for RFC1918 addresses
- [ ] Test with multiple network interfaces
- [ ] Validate metric-based route priority
- [ ] Handle route conflicts gracefully
- [ ] Add route persistence across reboots (optional)
- [ ] Implement rollback on configuration failure

## References

- [IP Helper API Documentation](https://docs.microsoft.com/en-us/windows/win32/api/iphlpapi/)
- [MIB_IPFORWARD_ROW2 Structure](https://docs.microsoft.com/en-us/windows/win32/api/netioapi/ns-netioapi-mib_ipforward_row2)
- [Windows Routing Table Management](https://docs.microsoft.com/en-us/windows/win32/rras/routing-table-manager-version-2-reference)
- [Windows NAT](https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/user-guide/setup-nat-network)
- [netsh routing commands](https://docs.microsoft.com/en-us/windows-server/networking/technologies/netsh/netsh-contexts)
