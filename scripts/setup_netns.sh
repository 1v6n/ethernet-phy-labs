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
ip netns add ns_b
ip tuntap add dev tap0 mode tap
ip tuntap add dev tap1 mode tap
ip link set tap1 netns ns_b

echo "=== 3. Configurando Direcciones IP y Estado ==="
# Host interface (tap0)
ip addr add 10.0.0.1/24 dev tap0
ip link set tap0 promisc on
ip link set tap0 up

# Namespace ns_b interface (tap1)
ip netns exec ns_b ip link set lo up
ip netns exec ns_b ip addr add 10.0.0.2/24 dev tap1
ip netns exec ns_b ip link set tap1 promisc on
ip netns exec ns_b ip link set tap1 up

echo "=== 4. Verificando estado del entorno ==="
echo "-> Interfaz tap0 (Host):"
ip addr show dev tap0
echo "-> Interfaz tap1 (Namespace ns_b):"
ip netns exec ns_b ip addr show dev tap1

echo "================================================="
echo "Entorno preparado con éxito. Listo para la simulación."
echo "================================================="
