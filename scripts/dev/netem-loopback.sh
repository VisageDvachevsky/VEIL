#!/bin/bash
# netem-loopback.sh - Configure network emulation on loopback interface
#
# This script uses Linux Traffic Control (tc) with netem to simulate
# network conditions like delay, packet loss, and jitter on loopback.
#
# Usage:
#   ./netem-loopback.sh setup [delay_ms] [loss_percent] [jitter_ms]
#   ./netem-loopback.sh status
#   ./netem-loopback.sh reset
#
# Examples:
#   ./netem-loopback.sh setup 50 1 10    # 50ms delay, 1% loss, 10ms jitter
#   ./netem-loopback.sh setup 100 5 20   # 100ms delay, 5% loss, 20ms jitter
#   ./netem-loopback.sh reset            # Remove all netem rules
#
# Requirements:
#   - Root privileges (sudo)
#   - iproute2 package (tc command)
#
# Note: This affects ALL traffic on loopback, use with caution!

set -e

IFACE="${NETEM_IFACE:-lo}"

usage() {
    echo "Usage: $0 {setup|status|reset} [delay_ms] [loss_percent] [jitter_ms]"
    echo ""
    echo "Commands:"
    echo "  setup   - Configure netem with specified parameters"
    echo "  status  - Show current netem configuration"
    echo "  reset   - Remove all netem rules"
    echo ""
    echo "Parameters for setup:"
    echo "  delay_ms      - Base delay in milliseconds (default: 50)"
    echo "  loss_percent  - Packet loss percentage (default: 1)"
    echo "  jitter_ms     - Delay variation in milliseconds (default: 10)"
    echo ""
    echo "Environment variables:"
    echo "  NETEM_IFACE   - Interface to configure (default: lo)"
    exit 1
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "Error: This script must be run as root (use sudo)" >&2
        exit 1
    fi
}

check_tc() {
    if ! command -v tc &> /dev/null; then
        echo "Error: tc command not found. Install iproute2 package." >&2
        exit 1
    fi
}

setup_netem() {
    local delay_ms="${1:-50}"
    local loss_pct="${2:-1}"
    local jitter_ms="${3:-10}"

    echo "Configuring netem on $IFACE:"
    echo "  Delay:  ${delay_ms}ms ± ${jitter_ms}ms"
    echo "  Loss:   ${loss_pct}%"

    # Remove existing qdisc if present
    tc qdisc del dev "$IFACE" root 2>/dev/null || true

    # Add netem qdisc
    tc qdisc add dev "$IFACE" root netem \
        delay "${delay_ms}ms" "${jitter_ms}ms" distribution normal \
        loss "${loss_pct}%" \
        limit 10000

    echo "Done. Run '$0 status' to verify."
}

show_status() {
    echo "Current qdisc configuration on $IFACE:"
    tc qdisc show dev "$IFACE"
    echo ""
    echo "Detailed netem stats:"
    tc -s qdisc show dev "$IFACE" 2>/dev/null || echo "(no stats available)"
}

reset_netem() {
    echo "Removing netem configuration from $IFACE..."
    tc qdisc del dev "$IFACE" root 2>/dev/null || true
    echo "Done."
}

# Parse command
case "${1:-}" in
    setup)
        check_root
        check_tc
        setup_netem "${2:-}" "${3:-}" "${4:-}"
        ;;
    status)
        check_tc
        show_status
        ;;
    reset)
        check_root
        check_tc
        reset_netem
        ;;
    *)
        usage
        ;;
esac
