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

echo "[NETNS] Creating namespace ns_b & TAP nodes..."
mkdir -p /dev/net
if [ ! -c /dev/net/tun ]; then
  mknod /dev/net/tun c 10 200
  chmod 666 /dev/net/tun
fi
mkdir -p /var/run/netns

ip netns add ns_b
ip tuntap add dev tap0 mode tap
ip tuntap add dev tap1 mode tap
ip link set tap1 netns ns_b

echo "[NETNS] Configuring IP addresses, MACs & link states..."
# Host interface (tap0)
ip link set dev tap0 address 02:00:00:00:00:01 2>/dev/null || true
ip addr add 10.0.0.1/24 dev tap0
ip link set tap0 multicast off
ip link set tap0 promisc on
ip link set tap0 up

# Namespace ns_b interface (tap1)
ip netns exec ns_b ip link set lo up
ip netns exec ns_b ip link set dev tap1 address 02:00:00:00:00:02 2>/dev/null || true
ip netns exec ns_b ip addr add 10.0.0.2/24 dev tap1
ip netns exec ns_b ip link set tap1 multicast off
ip netns exec ns_b ip link set tap1 promisc on
ip netns exec ns_b ip link set tap1 up

echo "[NETNS] Disabling hardware offloading & IPv6 (Clean Trace)..."
ethtool -K tap0 tx off rx off gso off gro off tso off 2>/dev/null || true
ip netns exec ns_b ethtool -K tap1 tx off rx off gso off gro off tso off 2>/dev/null || true

sysctl -w net.ipv6.conf.tap0.disable_ipv6=1 >/dev/null 2>&1 || true
ip netns exec ns_b sysctl -w net.ipv6.conf.all.disable_ipv6=1 >/dev/null 2>&1 || true
ip netns exec ns_b sysctl -w net.ipv6.conf.default.disable_ipv6=1 >/dev/null 2>&1 || true
ip netns exec ns_b sysctl -w net.ipv6.conf.tap1.disable_ipv6=1 >/dev/null 2>&1 || true

# Pre-populate static ARP cache
ip neigh replace 10.0.0.2 lladdr 02:00:00:00:00:02 dev tap0 nud permanent 2>/dev/null || true
ip netns exec ns_b ip neigh replace 10.0.0.1 lladdr 02:00:00:00:00:01 dev tap1 nud permanent 2>/dev/null || true

echo "--------------------------------------------------"
echo "[NETNS] Setup Complete:"
echo "  * Host (Host NetNS): tap0 -> 10.0.0.1/24 (MAC 02:00:00:00:00:01, IPv6 OFF)"
echo "  * Node B (ns_b NetNS): tap1 -> 10.0.0.2/24 (MAC 02:00:00:00:00:02, IPv6 OFF)"
echo "--------------------------------------------------"
