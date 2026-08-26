#!/bin/bash
# ============================================================================
# File: setup_netns.sh
# Description: Network Namespace and TAP Setup Script (IPv6 Disabled)
# ============================================================================

set -e

# Ensure script is run with root privileges
if [ "$EUID" -ne 0 ]; then
  echo "[ERROR] Please run as root (sudo ./scripts/setup_netns.sh)"
  exit 1
fi

echo "[NETNS] Cleaning up previous setup..."
ip netns delete ns_b 2>/dev/null || true
ip link delete tap0 2>/dev/null || true
ip link delete tap1 2>/dev/null || true

echo "[NETNS] Creating namespace ns_b..."
ip netns add ns_b

echo "[NETNS] Creating TAP interfaces tap0 and tap1..."
ip tuntap add dev tap0 mode tap
ip tuntap add dev tap1 mode tap

echo "[NETNS] Moving tap1 into namespace ns_b..."
ip link set tap1 netns ns_b

echo "[NETNS] Disabling IPv6 to suppress ICMPv6 multicast packets..."
# Disable IPv6 on host interface tap0
sysctl -w net.ipv6.conf.tap0.disable_ipv6=1 > /dev/null

# Disable IPv6 inside namespace ns_b
ip netns exec ns_b sysctl -w net.ipv6.conf.all.disable_ipv6=1 > /dev/null
ip netns exec ns_b sysctl -w net.ipv6.conf.default.disable_ipv6=1 > /dev/null
ip netns exec ns_b sysctl -w net.ipv6.conf.tap1.disable_ipv6=1 > /dev/null

echo "[NETNS] Assigning IP addresses..."
ip addr add 10.0.0.1/24 dev tap0
ip netns exec ns_b ip addr add 10.0.0.2/24 dev tap1

echo "[NETNS] Bringing interfaces UP..."
ip link set tap0 up
ip netns exec ns_b ip link set lo up
ip netns exec ns_b ip link set tap1 up

echo "--------------------------------------------------"
echo "[NETNS] Setup Complete:"
echo "  * Host (Host NetNS): tap0 -> 10.0.0.1/24 (IPv6 OFF)"
echo "  * Node B (ns_b NetNS): tap1 -> 10.0.0.2/24 (IPv6 OFF)"
echo "--------------------------------------------------"
