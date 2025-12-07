#include "tun/tun_device.h"

#ifdef _WIN32

#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>

#include <system_error>

#include "common/logging/logger.h"

// TODO(visage): Integrate wintun.dll SDK
// TODO(visage): Implement wintun adapter lifecycle management
// TODO(visage): Implement ring buffer for packet I/O
// TODO(visage): Create dedicated thread for packet reading (no fd for epoll)
// TODO(visage): Validate Windows overlapped I/O performance
// TODO(visage): Investigate optimal buffer size for Wintun ring

namespace {
std::error_code last_error() {
  return std::error_code(::GetLastError(), std::system_category());
}

// Maximum packet size for TUN devices.
constexpr std::size_t kMaxPacketSize = 65535;
}  // namespace

namespace veil::tun {

TunDevice::TunDevice() = default;

TunDevice::~TunDevice() { close(); }

TunDevice::TunDevice(TunDevice&& other) noexcept
    : fd_(other.fd_),
      device_name_(std::move(other.device_name_)),
      stats_(other.stats_),
      packet_info_(other.packet_info_) {
  other.fd_ = -1;
}

TunDevice& TunDevice::operator=(TunDevice&& other) noexcept {
  if (this != &other) {
    close();
    fd_ = other.fd_;
    device_name_ = std::move(other.device_name_);
    stats_ = other.stats_;
    packet_info_ = other.packet_info_;
    other.fd_ = -1;
  }
  return *this;
}

bool TunDevice::open(const TunConfig& config, std::error_code& ec) {
  // TODO(visage): Implement wintun adapter creation
  // Steps required:
  // 1. Load wintun.dll dynamically (WintunOpenAdapter, WintunCreateAdapter, etc.)
  // 2. Create or open existing wintun adapter with name from config.device_name
  // 3. Get adapter LUID for routing operations
  // 4. Start wintun session (WintunStartSession)
  // 5. Allocate ring buffers for send/receive
  // 6. Create dedicated thread for packet reading (Windows doesn't provide fd)
  // 7. Configure IP address using SetAdapterIpAddress (IP Helper API)
  // 8. Set MTU using SetInterfaceMtu
  // 9. Bring adapter up if config.bring_up is true

  LOG_ERROR("Windows TUN device not yet implemented");
  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

void TunDevice::close() {
  if (fd_ >= 0) {
    // TODO(visage): Implement wintun cleanup
    // Steps required:
    // 1. Stop packet reader thread
    // 2. End wintun session (WintunEndSession)
    // 3. Close adapter (WintunCloseAdapter)
    // 4. Unload wintun.dll if dynamically loaded

    fd_ = -1;
    device_name_.clear();
  }
}

std::optional<std::vector<std::uint8_t>> TunDevice::read(std::error_code& ec) {
  // TODO(visage): Implement wintun packet reading
  // Steps required:
  // 1. Call WintunReceivePacket to get packet from ring buffer
  // 2. Copy packet data to std::vector
  // 3. Call WintunReleaseReceivePacket
  // 4. Update stats_.packets_read, stats_.bytes_read
  // 5. Handle WAIT_TIMEOUT for non-blocking behavior

  ec = std::make_error_code(std::errc::function_not_supported);
  stats_.read_errors++;
  return std::nullopt;
}

std::ptrdiff_t TunDevice::read_into(std::span<std::uint8_t> buffer, std::error_code& ec) {
  // TODO(visage): Implement wintun packet reading into provided buffer
  // Similar to read() but copy directly into buffer instead of allocating vector

  ec = std::make_error_code(std::errc::function_not_supported);
  stats_.read_errors++;
  return -1;
}

bool TunDevice::write(std::span<const std::uint8_t> packet, std::error_code& ec) {
  // TODO(visage): Implement wintun packet writing
  // Steps required:
  // 1. Call WintunAllocateSendPacket to get buffer from ring
  // 2. Copy packet data to allocated buffer
  // 3. Call WintunSendPacket
  // 4. Update stats_.packets_written, stats_.bytes_written
  // 5. Handle buffer full condition (may need to retry or queue)

  ec = std::make_error_code(std::errc::function_not_supported);
  stats_.write_errors++;
  return false;
}

bool TunDevice::poll(const ReadHandler& handler, int timeout_ms, std::error_code& ec) {
  // TODO(visage): Implement Windows event-based polling
  // Options:
  // 1. Use WaitForSingleObject on wintun event handle
  // 2. Or use dedicated reader thread and Windows event objects
  // 3. Call handler for each received packet

  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool TunDevice::set_mtu(int mtu, std::error_code& ec) {
  // TODO(visage): Implement MTU setting via IP Helper API
  // Use SetInterfaceMtu or MIB_IPINTERFACE_ROW structure

  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool TunDevice::set_up(bool up, std::error_code& ec) {
  // TODO(visage): Implement interface up/down via IP Helper API
  // Use SetInterfaceOperStatus or similar API

  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool TunDevice::configure_address(const TunConfig& config, std::error_code& ec) {
  // TODO(visage): Implement IP address configuration via IP Helper API
  // Steps required:
  // 1. Get adapter LUID from wintun adapter
  // 2. Use AddIPAddress or MIB_UNICASTIPADDRESS_ROW
  // 3. Parse config.ip_address (support CIDR notation)
  // 4. Set both address and netmask

  ec = std::make_error_code(std::errc::function_not_supported);
  return false;
}

bool TunDevice::configure_mtu(int mtu, std::error_code& ec) {
  return set_mtu(mtu, ec);
}

bool TunDevice::bring_interface_up(std::error_code& ec) {
  return set_up(true, ec);
}

}  // namespace veil::tun

#endif  // _WIN32
