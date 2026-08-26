#!/usr/bin/env bash
set -e

# Privilege check
if [ "$EUID" -ne 0 ]; then
  echo "[ERROR] Please run as root (e.g., sudo ./scripts/setup_netns.sh)."
  exit 1
fi

echo "=== 1. Limpiando configuración previa ==="
ip link del tap0 2>/dev/null || true
ip netns del ns_b 2>/dev/null || true

echo "=== 2. Creando interfaces TAP y Namespace de red ==="
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

echo "=== 3. Configurando Direcciones IP, MACs y Estado ==="
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

echo "=== 4. Deshabilitando Offloading e IPv6/mDNS (Traza Limpia) ==="
# Hardware offload disable
ethtool -K tap0 tx off rx off gso off gro off tso off 2>/dev/null || true
ip netns exec ns_b ethtool -K tap1 tx off rx off gso off gro off tso off 2>/dev/null || true

# Disable IPv6 (eliminates ICMPv6, MLD, Neighbor Solicitations)
sysctl -w net.ipv6.conf.tap0.disable_ipv6=1 >/dev/null 2>&1 || true
ip netns exec ns_b sysctl -w net.ipv6.conf.tap1.disable_ipv6=1 >/dev/null 2>&1 || true

# Pre-populate static ARP cache (eliminates ARP traffic)
ip neigh replace 10.0.0.2 lladdr 02:00:00:00:00:02 dev tap0 nud permanent 2>/dev/null || true
ip netns exec ns_b ip neigh replace 10.0.0.1 lladdr 02:00:00:00:00:01 dev tap1 nud permanent 2>/dev/null || true

echo "=== 5. Verificando estado del entorno ==="
echo "-> Interfaz tap0 (Host):"
ip addr show dev tap0
echo "-> Interfaz tap1 (Namespace ns_b):"
ip netns exec ns_b ip addr show dev tap1

echo "================================================="
echo "Entorno preparado con éxito. Listo para la simulación."
echo "================================================="
