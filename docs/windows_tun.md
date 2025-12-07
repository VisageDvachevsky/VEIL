# Windows TUN Device Implementation

## Overview

This document describes the Windows TUN device implementation using the Wintun driver from WireGuard.

## Wintun Driver

### What is Wintun?

Wintun is a high-performance Layer 3 TUN driver for Windows developed by the WireGuard project. It provides:
- Kernel-mode virtual network adapter
- Ring buffer-based packet I/O (zero-copy in kernel)
- Superior performance compared to TAP-Windows
- Signed driver (no test mode required)
- MIT/GPL dual license

### Integration Approach

**Files:**
- `src/tun/tun_device_windows.cpp` - Windows-specific TUN implementation
- `src/tun/tun_device_linux.cpp` - Linux-specific TUN implementation
- `src/tun/tun_device.cpp` - Platform dispatcher
- `src/tun/tun_device.h` - Common interface (unchanged)

**Dependencies:**
- `wintun.dll` - Runtime library (must be distributed with application)
- `wintun.h` - Header file from Wintun SDK
- Windows SDK (iphlpapi.h for IP configuration)

## API Mapping

### Create/Open Adapter

**Linux:** `open("/dev/net/tun")` + `ioctl(TUNSETIFF)`

**Windows:**
```cpp
WINTUN_ADAPTER_HANDLE adapter = WintunCreateAdapter(
    L"VEIL",           // Adapter name
    L"VEIL Tunnel",    // Tunnel type (arbitrary)
    &guid              // Optional GUID
);
```

### Configure IP Address

**Linux:** `ioctl(SIOCSIFADDR)` + `ioctl(SIOCSIFNETMASK)`

**Windows:**
```cpp
// Get adapter LUID
NET_LUID luid;
WintunGetAdapterLUID(adapter, &luid);

// Add IP address using IP Helper API
MIB_UNICASTIPADDRESS_ROW row = {};
InitializeUnicastIpAddressEntry(&row);
row.InterfaceLuid = luid;
row.Address.si_family = AF_INET;
row.Address.Ipv4.sin_addr.s_addr = inet_addr("10.0.0.2");
row.OnLinkPrefixLength = 24;  // /24 netmask
CreateUnicastIpAddressEntry(&row);
```

### Set MTU

**Linux:** `ioctl(SIOCSIFMTU)`

**Windows:**
```cpp
MIB_IPINTERFACE_ROW row = {};
InitializeIpInterfaceEntry(&row);
row.InterfaceLuid = luid;
row.Family = AF_INET;
GetIpInterfaceEntry(&row);
row.NlMtu = 1400;
SetIpInterfaceEntry(&row);
```

### Read Packets

**Linux:** `read(fd, buffer, size)` with epoll

**Windows:**
```cpp
WINTUN_SESSION_HANDLE session = WintunStartSession(adapter, RING_CAPACITY);

// Get read event for WaitForSingleObject
HANDLE readEvent = WintunGetReadWaitEvent(session);

// Blocking read
WaitForSingleObject(readEvent, timeout_ms);
DWORD packetSize;
BYTE* packet = WintunReceivePacket(session, &packetSize);
if (packet) {
    // Process packet...
    WintunReleaseReceivePacket(session, packet);
}
```

### Write Packets

**Linux:** `write(fd, buffer, size)`

**Windows:**
```cpp
BYTE* packet = WintunAllocateSendPacket(session, packetSize);
if (packet) {
    memcpy(packet, data, packetSize);
    WintunSendPacket(session, packet);
}
```

## Threading Model

### Linux
- Single-threaded with epoll for async I/O
- File descriptor integrates with event loop

### Windows
- Requires dedicated reader thread (no file descriptor for epoll equivalent)
- Use `WintunGetReadWaitEvent()` + `WaitForSingleObject()`
- Or poll `WintunReceivePacket()` in loop with small timeout

**Proposed Threading:**
```cpp
class WinTunDevice {
 private:
  std::thread reader_thread_;
  std::atomic<bool> running_{false};
  HANDLE read_event_;
  WINTUN_SESSION_HANDLE session_;

  void reader_loop() {
    while (running_) {
      DWORD wait = WaitForSingleObject(read_event_, 100);
      if (wait == WAIT_OBJECT_0) {
        DWORD size;
        BYTE* packet = WintunReceivePacket(session_, &size);
        if (packet) {
          // Forward to event loop via queue or callback
          process_packet(packet, size);
          WintunReleaseReceivePacket(session_, packet);
        }
      }
    }
  }
};
```

## Memory Management

### Ring Buffer Capacity

```cpp
// Ring capacity must be power of 2, between 128 KB and 16 MB
#define RING_CAPACITY (2 * 1024 * 1024)  // 2 MB recommended
```

### Zero-Copy Considerations

- `WintunReceivePacket()` returns pointer directly into ring buffer
- Must call `WintunReleaseReceivePacket()` promptly to avoid blocking
- `WintunAllocateSendPacket()` allocates from ring, returns nullptr if full
- Send queue full is not an error, retry after short delay

## Error Handling

### Common Errors

| Error Code | Meaning | Handling |
|------------|---------|----------|
| `ERROR_BUFFER_OVERFLOW` | Ring buffer full | Retry with backoff |
| `ERROR_NO_MORE_ITEMS` | No packets available | Normal, continue polling |
| `ERROR_HANDLE_EOF` | Session terminated | Restart session |
| `ERROR_INVALID_STATE` | Adapter disabled | Check adapter state |

### Error Mapping

```cpp
std::error_code last_error() {
    DWORD err = GetLastError();
    return std::error_code(err, std::system_category());
}
```

## Adapter Lifecycle

### Installation
1. Application includes `wintun.dll` in same directory
2. On first run, Wintun driver auto-installs from DLL resources
3. Requires admin privileges for installation
4. Subsequent runs don't need admin (if driver already installed)

### Cleanup
```cpp
void cleanup() {
    if (session_) {
        WintunEndSession(session_);
        session_ = nullptr;
    }
    if (adapter_) {
        WintunCloseAdapter(adapter_);
        adapter_ = nullptr;
    }
}
```

### Persistent Adapters

- Adapters persist after process exit (visible in "Network Connections")
- Can reuse existing adapter by name: `WintunOpenAdapter()`
- To remove: `WintunDeleteDriver()` (requires admin)

## Performance Tuning

### Recommended Settings

```cpp
// Ring buffer size (balance memory vs throughput)
const DWORD kRingCapacity = 2 * 1024 * 1024;  // 2 MB

// Read timeout (affects latency vs CPU usage)
const DWORD kReadTimeoutMs = 10;  // 10ms for low latency

// Batch processing (process multiple packets per wake)
const int kMaxBatchSize = 16;
```

### Profiling

- Monitor `WintunAllocateSendPacket()` failures → increase ring size
- High CPU in reader thread → increase read timeout
- High latency → decrease read timeout or batch size

## TODO Items

- [ ] Implement wintun adapter creation/opening
- [ ] Add IP address configuration via IP Helper API
- [ ] Create dedicated reader thread with event-based polling
- [ ] Implement MTU configuration
- [ ] Add adapter state monitoring (up/down)
- [ ] Implement packet queue for thread-safe delivery to event loop
- [ ] Add ring buffer size auto-tuning based on traffic
- [ ] Stress test with 1 Gbps throughput
- [ ] Validate overlapped I/O performance
- [ ] Test adapter cleanup on process crash
- [ ] Investigate optimal buffer size for wintun ring

## References

- [Wintun Official Site](https://www.wintun.net/)
- [Wintun SDK Documentation](https://git.zx2c4.com/wintun/about/)
- [WireGuard Windows Source](https://git.zx2c4.com/wireguard-windows) - Reference implementation
- [IP Helper API Docs](https://docs.microsoft.com/en-us/windows/win32/api/iphlpapi/)
- [Windows Network Programming](https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-start-page-2)

## License Compatibility

Wintun is dual-licensed:
- **GPL v2** - For GPL-compatible projects
- **Prebuilt binaries** - Distributed under permissive license (see wintun.net)

VEIL can use prebuilt `wintun.dll` without GPL requirements. Include attribution in documentation.
