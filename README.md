# Ethernet PHY & MAC Emulation Labs (IEEE 802.3)

A Hardware-in-the-Loop (HIL) emulation suite for IEEE 802.3 Ethernet sublayers using SystemVerilog, Verilator, and Linux TAP interfaces. This repository bridges the gap between RTL simulation and real Linux network stacks, enabling real-time packet processing (e.g., ICMP/ping, ARP) through hardware pipelines.

---

## 🚀 IEEE 802.3 Roadmap & Lab Overview

This project incrementally builds an Ethernet Physical Layer (PHY) architecture from Layer 2 up to next-generation multi-gigabit (+100G to 1.6T) specifications:

| Lab Directory | Sublayer / Protocol | Bus Width | Key Concepts |
| :--- | :--- | :--- | :--- |
| **`lab1-raw-ethernet`** | Layer 2 MAC Payload Stream | 8-bit stream | Direct TAP frame passthrough, non-MII byte piping. |
| **`lab2-xgmii-64bit`** | Reconciliation Sublayer (RS) | 64-bit XGMII | Framing control words (`/S/`, `/T/`, `/I/`), MAC/EtherType parsing FSM. |
| *`lab3-pcs-64b66b`* | PCS Sublayer (10G/25G) | 66-bit blocks | 64b/66b encoding, sync headers (`01`/`10`), self-synchronizing scrambler. |
| *`lab4-mld-deskew`* | Multi-Lane Distribution | 4 / 8 Lanes | PCS lane striping, Alignment Marker (AM) insertion, lane deskew logic. |
| *`lab5-rs-fec`* | Forward Error Correction | 100G/400G | Reed-Solomon RS(528,514) / RS(544,514) codeword generation. |
| *`lab6-wide-bus-1.6t`* | Next-Gen Ultra-High Speed | 512b / 1024b | 256b/257b transcoding, 800GMII / 1.6TMII wide internal architecture. |

---

## 🛠️ Repository Structure

```text
ethernet-phy-labs/
├── README.md
├── .gitignore
├── scripts/
│   └── setup_netns.sh         # Network namespace setup script
├── lab1-raw-ethernet/
│   ├── raw_eth_top.sv         # Direct L2 byte pipeline
│   └── wrapper.cpp            # Verilator TAP interface wrapper
└── lab2-xgmii-64bit/
    ├── xgmii_top.sv           # 64-bit XGMII top module & FSMs
    └── wrapper.cpp            # XGMII-to-TAP framer wrapper
```

---

## 📋 Prerequisites

System requirements for Linux (Ubuntu 22.04 / 24.04 recommended):

```bash
sudo apt update
sudo apt install -y build-essential verilator gtkwave tcpdump iproute2
```

---

## ⚡ Environment Setup

Linux kernels route traffic between virtual TAP interfaces on the same host directly through RAM bypass. To force packets to travel **physically through the SystemVerilog RTL model**, `tap1` is isolated inside a dedicated Network Namespace (`ns_b`).

Run the setup script once per system reboot:

```bash
chmod +x scripts/setup_netns.sh
sudo ./scripts/setup_netns.sh
```

---

## 🏃 Quick Start (Lab 2 Example)

1. **Prepare Network Interfaces:**
   ```bash
   sudo ./scripts/setup_netns.sh
   ```

2. **Compile and Run the RTL Emulator:**
   ```bash
   cd lab2-xgmii-64bit
   verilator --cc --trace --exe --build -j 0 -Wall wrapper.cpp xgmii_top.sv -o emulator
   sudo ./obj_dir/emulator
   ```

3. **Test Traffic (In a new terminal):**
   ```bash
   # Ping Node B (inside ns_b) from Node A
   ping -c 4 -I tap0 10.0.0.2

   # Capture packets on tap0
   sudo tcpdump -i tap0 -n
   ```

4. **Analyze Waveforms:**
   ```bash
   gtkwave waveform.vcd
   ```

---

## 📄 License

Distributed under the MIT License. See `LICENSE` for more information.
