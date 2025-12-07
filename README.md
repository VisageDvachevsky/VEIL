# VEIL

**V**ersatile **E**ncrypted **I**nterconnect **L**ayer - A secure UDP-based transport protocol with authenticated encryption.

## Features

- **Strong Cryptography**: X25519 key exchange, ChaCha20-Poly1305 AEAD, HKDF key derivation
- **Secure Handshake**: PSK-authenticated with HMAC, anti-probing silent drops, timestamp validation
- **Reliable Transport**: Selective ACKs, packet reordering, retransmission with RTT estimation
- **Fragmentation**: Automatic message fragmentation and reassembly with MTU awareness
- **Session Management**: Automatic key rotation, replay protection with sliding window
- **Rate Limiting**: Token bucket rate limiter for DoS protection
- **Portability**: Linux epoll with fallback to poll, sendmmsg/recvmmsg with individual syscall fallback

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 12+)
- CMake 3.20+
- Ninja (recommended) or Make
- libsodium (will be fetched if not found)

## Building

```bash
# Configure and build (debug)
cmake --preset debug
cmake --build --preset debug

# Run tests
ctest --preset debug

# Build release
cmake --preset release
cmake --build --preset release
```

## Running Tests

```bash
# All tests
ctest --preset debug

# Verbose output
ctest --preset debug --output-on-failure

# Specific test
./build/debug/veil_tests --gtest_filter=CryptoTest.*
```

## Demo

Run the demo application to test the protocol:

```bash
# Terminal 1: Start server
./build/debug/veil_demo -m server -p 12345

# Terminal 2: Start client
./build/debug/veil_demo -m client -r 127.0.0.1 --remote-port 12345

# With ping mode (continuous pings)
./build/debug/veil_demo -m client -r 127.0.0.1 --remote-port 12345 --ping
```

## Network Emulation Testing

Use the netem script to simulate network conditions:

```bash
# Setup: 50ms delay, 1% packet loss, 10ms jitter
sudo ./scripts/dev/netem-loopback.sh setup 50 1 10

# Check status
./scripts/dev/netem-loopback.sh status

# Reset
sudo ./scripts/dev/netem-loopback.sh reset
```

Then run the demo to test behavior under adverse conditions.

## Configuration

### INI File Format

```ini
[network]
local_host = 0.0.0.0
local_port = 12345
peer_host = 192.168.1.100
peer_port = 12345
mtu = 1400

[security]
psk = 0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20

[rate_limiter]
packets_per_second = 10000
bytes_per_second = 100000000
burst_packets = 100
burst_bytes = 1000000

[session]
packets_per_session = 1000000
bytes_per_session = 1073741824
seconds_per_session = 3600
```

### CLI Options

```
./veil_demo --help

Options:
  -m, --mode          Mode: client or server
  -b, --bind          Local bind address
  -p, --port          Local port (0 = auto)
  -r, --remote        Remote host
  --remote-port       Remote port
  --psk               Pre-shared key (hex string)
  -l, --log-level     Log level: trace,debug,info,warn,error
  --mtu               Maximum transmission unit
  --ping              Ping mode: send periodic pings
  --ping-interval     Ping interval in ms
```

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Application                             │
├─────────────────────────────────────────────────────────────┤
│                   TransportSession                           │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐│
│  │  Handshake  │ │   Config    │ │        Logging          ││
│  └─────────────┘ └─────────────┘ └─────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                         Mux Layer                            │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────────────┐ │
│  │ ACK Bitmap   │ │ Reorder Buf  │ │ Fragment Assembler   │ │
│  ├──────────────┤ ├──────────────┤ ├──────────────────────┤ │
│  │ Replay Win   │ │ Rate Limiter │ │ Retransmission Mgr   │ │
│  ├──────────────┤ ├──────────────┤ └──────────────────────┘ │
│  │Session Rotate│ │              │                          │
│  └──────────────┘ └──────────────┘                          │
├─────────────────────────────────────────────────────────────┤
│                       Packet Layer                           │
│  ┌──────────────────────┐  ┌──────────────────────────────┐ │
│  │    PacketBuilder     │  │       PacketParser           │ │
│  └──────────────────────┘  └──────────────────────────────┘ │
│  ┌──────────────────────────────────────────────────────────┤
│  │ Frame Types: DATA | ACK | CONTROL | FRAGMENT | HANDSHAKE││
│  └──────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                       Crypto Layer                           │
│  ┌────────────┐ ┌────────────┐ ┌────────────────────────┐  │
│  │   X25519   │ │    HKDF    │ │  ChaCha20-Poly1305     │  │
│  └────────────┘ └────────────┘ └────────────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                     Transport Layer                          │
│  ┌────────────────────────────────────────────────────────┐ │
│  │    UdpSocket (epoll, sendmmsg/recvmmsg)                │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

## Wire Format

### Packet Structure
```
┌────────────────┬────────────────┬───────────────────┬──────────┐
│  Session ID    │ Packet Counter │ Encrypted Payload │   Tag    │
│   (8 bytes)    │   (8 bytes)    │    (variable)     │(16 bytes)│
└────────────────┴────────────────┴───────────────────┴──────────┘
```

### Frame Header
```
┌──────────┬───────────┬────────────┬─────────────────┐
│   Type   │   Flags   │   Length   │     Payload     │
│ (1 byte) │  (1 byte) │ (2 bytes)  │   (variable)    │
└──────────┴───────────┴────────────┴─────────────────┘
```

## Security Considerations

- **Key Exchange**: X25519 provides 128-bit security level
- **Encryption**: ChaCha20-Poly1305 AEAD prevents tampering
- **Replay Protection**: 64-packet sliding window
- **Anti-probing**: Invalid packets are silently dropped
- **Timestamp Validation**: Prevents replay of old handshakes
- **Session Rotation**: Keys are rotated periodically

## License

MIT License - see [LICENSE](LICENSE) file.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Run tests: `ctest --preset debug`
4. Submit a pull request

Please ensure all tests pass and follow the existing code style.
