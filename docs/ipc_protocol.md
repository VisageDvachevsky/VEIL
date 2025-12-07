# IPC Protocol Documentation

## Overview

VEIL uses an IPC (Inter-Process Communication) protocol to enable communication between the GUI client and the core service/daemon. The protocol is transport-agnostic, running over Unix domain sockets on Linux and Named Pipes on Windows.

## Transport Layer

### Linux: Unix Domain Sockets

**Socket Paths:**
- Client daemon: `/tmp/veil-client.sock`
- Server daemon: `/tmp/veil-server.sock`

**Characteristics:**
- File-based addressing
- Automatic cleanup on process exit
- Permission-based access control

### Windows: Named Pipes

**Pipe Names:**
- Client daemon: `\\.\pipe\veil-client`
- Server daemon: `\\.\pipe\veil-server`

**Characteristics:**
- Network-transparent (can support remote connections if needed)
- ACL-based access control
- Survives across network changes

## Message Format

### Frame Structure

All messages are JSON-encoded and delimited by newlines:

```
<JSON message>\n
```

Multiple messages can be sent in sequence:

```
{"type": "command", "id": 1, "command": "connect", ...}\n
{"type": "command", "id": 2, "command": "status", ...}\n
```

### Message Types

```cpp
enum class MessageType : std::uint8_t {
  kCommand = 0,    // Client -> Daemon (request)
  kResponse = 1,   // Daemon -> Client (response to command)
  kEvent = 2       // Daemon -> Client (unsolicited status update)
};
```

## Commands

### GET_STATUS

Request current connection status and statistics.

**Request:**
```json
{
  "type": "command",
  "id": 1,
  "command": "get_status"
}
```

**Response:**
```json
{
  "type": "response",
  "id": 1,
  "status": "success",
  "data": {
    "state": "connected",
    "server_address": "vpn.example.com:51820",
    "local_ip": "10.8.0.2",
    "uptime_seconds": 3600,
    "bytes_sent": 1048576,
    "bytes_received": 2097152,
    "packets_sent": 1024,
    "packets_received": 2048,
    "current_rtt_ms": 45,
    "packet_loss_rate": 0.001
  }
}
```

### CONNECT

Initiate VPN connection to server.

**Request:**
```json
{
  "type": "command",
  "id": 2,
  "command": "connect",
  "params": {
    "server_address": "vpn.example.com:51820",
    "psk": "<base64-encoded-pre-shared-key>",
    "dpi_bypass_mode": "iot_mimic"
  }
}
```

**Response:**
```json
{
  "type": "response",
  "id": 2,
  "status": "success",
  "message": "Connection initiated"
}
```

**Possible Errors:**
```json
{
  "type": "response",
  "id": 2,
  "status": "error",
  "error_code": "ALREADY_CONNECTED",
  "message": "Already connected to vpn.example.com"
}
```

### DISCONNECT

Disconnect from VPN server.

**Request:**
```json
{
  "type": "command",
  "id": 3,
  "command": "disconnect"
}
```

**Response:**
```json
{
  "type": "response",
  "id": 3,
  "status": "success",
  "message": "Disconnected"
}
```

### SET_CONFIG

Update daemon configuration.

**Request:**
```json
{
  "type": "command",
  "id": 4,
  "command": "set_config",
  "params": {
    "log_level": "debug",
    "obfuscation_enabled": true,
    "dpi_bypass_mode": "quic_like",
    "dns_servers": ["1.1.1.1", "8.8.8.8"]
  }
}
```

**Response:**
```json
{
  "type": "response",
  "id": 4,
  "status": "success",
  "message": "Configuration updated"
}
```

### GET_LOGS_RECENT

Retrieve recent log entries.

**Request:**
```json
{
  "type": "command",
  "id": 5,
  "command": "get_logs_recent",
  "params": {
    "max_lines": 100,
    "min_level": "info"
  }
}
```

**Response:**
```json
{
  "type": "response",
  "id": 5,
  "status": "success",
  "data": {
    "logs": [
      {"timestamp": "2025-12-07T22:00:00Z", "level": "info", "message": "Connected to server"},
      {"timestamp": "2025-12-07T22:00:05Z", "level": "debug", "message": "Handshake completed"}
    ]
  }
}
```

### GET_ROUTING_TABLE

Retrieve current routing table (for diagnostics).

**Request:**
```json
{
  "type": "command",
  "id": 6,
  "command": "get_routing_table"
}
```

**Response:**
```json
{
  "type": "response",
  "id": 6,
  "status": "success",
  "data": {
    "routes": [
      {"destination": "0.0.0.0/0", "gateway": "10.8.0.1", "interface": "veil0", "metric": 1},
      {"destination": "10.8.0.0/24", "gateway": "", "interface": "veil0", "metric": 0}
    ]
  }
}
```

### PING

Keep-alive / health check.

**Request:**
```json
{
  "type": "command",
  "id": 7,
  "command": "ping"
}
```

**Response:**
```json
{
  "type": "response",
  "id": 7,
  "status": "success",
  "message": "pong"
}
```

## Events

Events are unsolicited messages sent by the daemon to notify clients of state changes.

### Connection State Changed

```json
{
  "type": "event",
  "event": "connection_state_changed",
  "data": {
    "old_state": "connecting",
    "new_state": "connected",
    "timestamp": "2025-12-07T22:00:00Z"
  }
}
```

### Statistics Update

```json
{
  "type": "event",
  "event": "statistics_update",
  "data": {
    "bytes_sent": 1048576,
    "bytes_received": 2097152,
    "current_rtt_ms": 45,
    "packet_loss_rate": 0.001,
    "timestamp": "2025-12-07T22:00:05Z"
  }
}
```

### Error Occurred

```json
{
  "type": "event",
  "event": "error",
  "data": {
    "error_code": "CONNECTION_TIMEOUT",
    "message": "Failed to connect to server: connection timeout",
    "severity": "error",
    "timestamp": "2025-12-07T22:00:10Z"
  }
}
```

## DPI Bypass Modes

Commands and configuration can specify DPI bypass mode:

```json
{
  "dpi_bypass_mode": "iot_mimic"  // or "quic_like", "random_noise", "trickle"
}
```

**Mode Values:**
- `"iot_mimic"` - IoT sensor traffic simulation (default)
- `"quic_like"` - QUIC/HTTP3 traffic patterns
- `"random_noise"` - Maximum entropy and unpredictability
- `"trickle"` - Low-and-slow stealth mode

## Error Codes

| Code | Meaning |
|------|---------|
| `ALREADY_CONNECTED` | Cannot connect, already connected |
| `NOT_CONNECTED` | Cannot disconnect, not connected |
| `CONNECTION_FAILED` | Failed to establish connection |
| `CONNECTION_TIMEOUT` | Connection attempt timed out |
| `AUTHENTICATION_FAILED` | Invalid PSK or authentication failure |
| `INVALID_PARAMETER` | Invalid command parameter |
| `PERMISSION_DENIED` | Insufficient privileges |
| `TUN_CREATE_FAILED` | Failed to create TUN device |
| `ROUTING_FAILED` | Failed to configure routing |
| `INTERNAL_ERROR` | Unexpected internal error |

## Implementation Notes

### Request IDs

- Clients must assign unique IDs to each command
- IDs are used to match responses to requests
- Recommended: incrementing counter starting from 1

### Timeouts

- Clients should implement timeouts for requests (recommended: 30 seconds)
- Daemon may not respond if command fails catastrophically
- PING command can be used to check if daemon is alive

### Concurrency

- Multiple commands can be in flight simultaneously
- Daemon handles commands asynchronously
- Events can arrive at any time, interspersed with responses

### Backward Compatibility

- New fields may be added to messages in future versions
- Clients should ignore unknown fields
- Version negotiation may be added in future

## Platform-Specific Details

### Windows: Named Pipes Security

Named pipes should have restricted ACLs:
- Allow LOCAL SERVICE (for VEILService)
- Allow current user (for GUI)
- Allow administrators

### Linux: Unix Socket Permissions

Unix sockets should have restricted permissions:
- Owner: Current user or root (for system daemon)
- Permissions: 0600 (read/write owner only)
- Located in /tmp or /run/user/{uid}

## TODO Items

- [ ] Define schema for all message types
- [ ] Add version negotiation protocol
- [ ] Implement message validation on both sides
- [ ] Add compression for large responses (logs, routing table)
- [ ] Define protocol for file transfer (config import/export)
- [ ] Add authentication mechanism for IPC (shared secret or token)
- [ ] Document reconnection behavior
- [ ] Add protocol versioning and capability negotiation

## References

- `src/common/ipc/ipc_protocol.h` - Protocol definitions
- `src/common/ipc/ipc_socket.h` - Transport layer interface
- `src/gui-client/ipc_client_manager.cpp` - Client-side implementation
- JSON-RPC 2.0 - Similar design (for reference)
