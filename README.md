# 🌐 High-Speed Ethernet PHY & MAC Sublayer Labs

This repository contains a hands-on, hardware-oriented laboratory series designed to explore and implement high-speed **Ethernet Physical Layer (PHY)** and **Data Link Layer** sublayers in **SystemVerilog** and **C++ (Verilator)**. 

The curriculum follows a modular two-phase structure: **Phase 1** focuses on digital controller protocol framing (MAC, PCS, MLD, RS-FEC, and Wide-Bus Transcoding), while **Phase 2** covers physical SerDes interface conditioning in the Physical Medium Attachment (PMA) sublayer, adhering strictly to **IEEE 802.3** standards up through 800G and 1.6T architectures.

---

## 💡 Architectural Evolution: Why PHY Sublayers Expanded

As Ethernet data rates scaled by orders of magnitude (from 1 Gbps up to 1.6 Tbps), physical layer architecture shifted from simple byte-piping to highly modular, multi-sublayer pipelines driven by fundamental physical and silicon constraints:

* **SerDes Rate Limits & Parallelism:** Physical channels could not scale linearly in frequency without extreme signal attenuation. To achieve 40G/100G+, protocols introduced **Multi-Lane Distribution (MLD)** to distribute data streams across lower-speed parallel SerDes lanes.
* **PAM4 Degradation & Forward Error Correction (FEC):** Transitioning from NRZ to PAM4 signaling doubled bandwidth density but drastically reduced noise margins. To maintain an acceptable Bit Error Rate ($10^{-12}$), **Reed-Solomon FEC (RS-FEC)** became mandatory to correct channel burst errors in real time.
* **The Silicon Clock Wall:** FPGA and ASIC logic cannot clock at multi-gigahertz speeds. To handle terabit throughput, modern PHYs use **256b/257b transcoding** to minimize framing overhead and process data over **512-bit and 1024-bit wide internal buses**, keeping internal clock frequencies at manageable levels (~800 MHz – 1 GHz).
* **Digital Controller vs. Mixed-Signal PHY Separation:** Modern ASIC designs strictly divide protocol responsibilities between the digital controller (MAC/PCS) and the mixed-signal physical interface (PMA/SerDes), requiring dedicated PMA logic for gearboxing, symbol mapping, and bit alignment.

---

## 🧰 Main Tools & Technologies

This repository bridges hardware description languages with live Linux networking using the following key tools:

* **Verilator (SystemVerilog Simulator):** Compiles SystemVerilog designs directly into high-speed, cycle-accurate C++ models. **Role:** Runs the hardware simulation without requiring expensive commercial EDA tools.
* **tcpdump (CLI Packet Sniffer):** Lightweight command-line packet capture tool. **Role:** Records live traffic on virtual interfaces directly to standard `.pcap` trace files.
* **Wireshark (Graphical Packet Analyzer):** Post-mortem protocol analyzer. **Role:** Opens saved `.pcap` files to inspect Ethernet headers, payloads, and protocol fields to verify standard compliance.
* **GTKWave (Waveform Viewer):** Displays Value Change Dump (`.vcd`) signal trace files. **Role:** Allows cycle-by-cycle inspection of internal hardware registers, clock signals, and state machine transitions.
* **Linux TAP Interfaces & `iproute2`:** Virtual Layer-2 network interfaces (`tap0`/`tap1`). **Role:** Bridges real host kernel traffic (e.g., `ping`, `arp`) directly into the simulated SystemVerilog pipeline.

---

## 🏗️ IEEE 802.3 Layer Architecture Covered

```text
================================================================================
          PHASE 1: DIGITAL CONTROLLER & PROTOCOL FRAMING (LABS 1–6)
================================================================================
+-------------------------------------------------------------------+
|               Layer 2: Media Access Control (MAC)                 |  <-- Lab 1
+-------------------------------------------------------------------+
                                  | (MAC Service Interface)
+-------------------------------------------------------------------+
|               Reconciliation Sublayer (RS) & XGMII                |  <-- Lab 2
+-------------------------------------------------------------------+
                                  | (64-bit Data / Control Bus)
+-------------------------------------------------------------------+
|      Physical Coding Sublayer (PCS) - 64b/66b & Scrambling       |  <-- Lab 3
+-------------------------------------------------------------------+
                                  | (66-bit Blocks)
+-------------------------------------------------------------------+
|          Multi-Lane Distribution (MLD) & Lane Alignment           |  <-- Lab 4
+-------------------------------------------------------------------+
                                  | (Striped PCS Lanes)
+-------------------------------------------------------------------+
|             Forward Error Correction (RS-FEC Sublayer)            |  <-- Lab 5
+-------------------------------------------------------------------+
                                  | (Transcoded / Wide Bus)
+-------------------------------------------------------------------+
|      Next-Gen Terabit Ultra-Wide Architecture (800G / 1.6T)      |  <-- Lab 6
+-------------------------------------------------------------------+

================================================================================
            PHASE 2: PHYSICAL MEDIUM ATTACHMENT & SERDES (LABS 7–8)
================================================================================
                                  | (Baseband Bitstream / Parallel Bus)
+-------------------------------------------------------------------+
|      Physical Medium Attachment (PMA) - 100G NRZ (25G SerDes)     |  <-- Lab 7
+-------------------------------------------------------------------+
                                  | (PAM4 Multi-Level Symbol Stream)
+-------------------------------------------------------------------+
|      Next-Gen PMA - 200G PAM4 SerDes Architecture (800G / 1.6T)   |  <-- Lab 8
+-------------------------------------------------------------------+
```

---

## 📊 Lab Comparison Matrix

| Lab Directory | Target Technology & Speed | Sublayer / Protocol | Bus Width / Format | IEEE Standard | Key Concepts & Implementation Focus |
| :--- | :---: | :--- | :---: | :---: | :--- |
| **`lab1-raw-ethernet`** | **10M / 100M / 1G** | Layer 2 MAC Stream | 8-bit Stream | IEEE 802.3 | Direct Linux TAP interface passthrough, raw byte-piping, MAC header parsing. |
| **`lab2-xgmii-64bit`** | **10G / 25G** | Reconciliation Sublayer (RS) | 64-bit + 8-bit Ctrl | IEEE 802.3ae / 802.3by | Control characters (`/S/`, `/T/`, `/I/`), 64-bit framing, CRC-32 (FCS) calculation, header FSM. |
| **`lab3-pcs-64b66b`** | **10G / 25G** | Physical Coding Sublayer (PCS) | 66-bit Blocks | IEEE 802.3ae Cl. 49 | 64b/66b framing, 2-bit Sync Headers (`01`/`10`), self-synchronizing polynomial scrambler ($1 + X^{39} + X^{58}$). |
| **`lab4-mld-deskew`** | **40G / 100G** | Multi-Lane Distribution (MLD) | 4 / 8 Parallel Lanes | IEEE 802.3ba Cl. 82 | Block striping across physical lanes, Alignment Marker (AM) insertion, receiver deskew FIFO logic. |
| **`lab5-rs-fec`** | **100G / 400G** | Forward Error Correction (RS-FEC) | Codeword Stream | IEEE 802.3bj / 802.3bs | Reed-Solomon RS(528,514) & RS(544,514) codeword generation, error protection for noisy links. |
| **`lab6-wide-bus-1.6t`**| **800G / 1.6T** | Next-Gen Ultra-High Speed PCS | 512-bit / 1024-bit | IEEE 802.3df / 802.3dj | 256b/257b transcoding, 800GMII / 1.6TMII ultra-wide internal bus routing to relax internal clock speeds. |
| **`lab7-pma-100g-nrz`**| **100G NRZ** | PMA (25G SerDes Lanes) | 16-bit / 32-bit Internal | IEEE 802.3ba Cl. 83 | Digital gearboxing (66:16/32), Bit Slip receiver alignment FSM, PRBS31 generator/checker logic. |
| **`lab8-pma-200g-pam4`**| **800G / 1.6T** | Next-Gen PMA (200G PAM4 SerDes) | PAM4 Symbol Stream | IEEE 802.3dj | PAM4 Gray coding, $1/(1+D)$ precoding, 3-tap FIR pre-emphasis control logic. |

---

## 📂 Laboratory Summaries

### 🔹 Phase 1: Digital Controller & Protocol Framing

#### Lab 1: Raw Ethernet Stream Parsing
* **Focus:** Basic Layer 2 Ethernet frame structures.
* **Details:** Connects RTL simulation directly to host OS Linux TAP interfaces. Receives raw byte streams, parses Destination/Source MAC addresses and EtherType fields, and routes raw frames back out.

#### Lab 2: 64-Bit XGMII Interface & Reconciliation Sublayer (RS)
* **Focus:** 10 Gigabit Media Independent Interface (XGMII).
* **Details:** Converts frame byte streams into 64-bit parallel data (`TXD[63:0]`) accompanied by an 8-bit control mask (`TXC[7:0]`). Implements state tracking for `/S/` (Start), `/T/` (Terminate), and `/I/` (Idle) characters, while calculating IEEE 802.3 32-bit Frame Check Sequence (FCS / CRC-32).

#### Lab 3: PCS 64b/66b Encoding & Scrambling
* **Focus:** Physical Coding Sublayer for single-lane 10G/25G links.
* **Details:** Maps 64-bit XGMII data/control bytes into 66-bit PCS blocks using 2-bit Sync Headers (`01` for Data, `10` for Control). Implements the standard Ethernet $1 + X^{39} + X^{58}$ self-synchronizing scrambler to ensure DC balance and transition density.

#### Lab 4: Multi-Lane Distribution (MLD) & Deskew
* **Focus:** Scaling PCS to 40G (4 lanes) and 100G (4 or 8 lanes).
* **Details:** Implements round-robin striping of 66-bit PCS blocks across multiple physical channels. Periodically injects Alignment Markers (AM) to allow the receiver to identify lane swapping, measure inter-lane skew, and align parallel streams.

#### Lab 5: Reed-Solomon Forward Error Correction (RS-FEC)
* **Focus:** High-speed error mitigation for noisy links (100G/400G).
* **Details:** Implements Clause 91 RS(528,514) and Clause 119 RS(544,514) encoding logic. Demonstrates how parity symbols are attached to data blocks to allow full recovery from burst errors over physical channels.

#### Lab 6: Next-Gen Ultra-High Speed Wide-Bus Architecture (800G / 1.6T)
* **Focus:** Terabit-scale PHY pipelines.
* **Details:** Solves clock frequency constraints by converting traditional 64b/66b blocks into 256b/257b transcoded blocks. Processes data over 512-bit and 1024-bit wide internal buses suitable for 800GMII and 1.6TMII architectures.

---

### 🔹 Phase 2: Physical Medium Attachment (PMA) & SerDes Interface

#### Lab 7: 100G NRZ PMA Sublayer (25G SerDes Lanes)
* **Focus:** Baseband multi-lane serialization and framing synchronization.
* **Details:** Implements digital gearboxing to adapt unaligned 66-bit PCS blocks to 16-bit/32-bit SerDes bus widths. Builds a Bit Slip finite state machine (FSM) for receiver block lock and integrates PRBS31 pattern generators and checkers for BER link testing.

#### Lab 8: Next-Gen 200G PAM4 SerDes PMA Sublayer (IEEE 802.3dj)
* **Focus:** Advanced symbol mapping for ultra-wide terabit optical/copper links.
* **Details:** Maps binary bit pairs $(00, 01, 11, 10)$ to 4-level PAM4 symbols $(-3, -1, +1, +3)$. Implements $1 / (1+D) \pmod 2$ precoding to suppress error propagation and configures 3-tap FIR pre-emphasis logic for transmitter pulse shaping.

---

## 🛠️ Prerequisites & Environment Setup

Ensure your Linux environment (Ubuntu 22.04 LTS / 24.04 LTS recommended) has the required tools installed:

```bash
sudo apt update
sudo apt install -y verilator gtkwave wireshark tcpdump build-essential iproute2 git
```

---

## 🎓 Student Quick-Start Guide

### 1. Clone the Repository
Open a terminal and download the repository:
```bash
git clone [https://github.com/jmfino/ethernet-phy-labs.git](https://github.com/jmfino/ethernet-phy-labs.git)
cd ethernet-phy-labs
```

### 2. Running a Lab Exercise (Multi-Terminal Setup)

Running these hardware simulations requires opening **three separate terminal windows** (Terminals A, B, and C) to concurrently manage simulation execution, packet logging, and waveform inspection.

#### 🖥️ Terminal A: Build & Run the Hardware Emulator
Use Terminal A to compile the SystemVerilog design and launch the Verilator hardware binary:

```bash
# 1. Initialize virtual network interfaces
sudo ./scripts/setup_netns.sh

# 2. Navigate to target lab (e.g., Lab 1)
cd lab1-raw-ethernet

# 3. Build the Verilator emulator
verilator --cc --trace --exe --build -j 0 -Wall wrapper.cpp top.sv -o emulator

# 4. Start the hardware simulation (keep this terminal running!)
sudo ./obj_dir/emulator
```

#### 🖥️ Terminal B: Capture Traffic & Inspect PCAP File
With the emulator active in Terminal A, use Terminal B to record network packets to a file with `tcpdump`, send test frames, and open the resulting capture in Wireshark:

```bash
# 1. Start capturing packets on tap0 into a .pcap file
sudo tcpdump -i tap0 -w capture.pcap

# 2. Send test ping traffic into the simulation
ping -c 3 -I tap0 10.0.0.2

# 3. Stop tcpdump by pressing Ctrl+C in this terminal

# 4. Open the recorded capture file in Wireshark for analysis
wireshark capture.pcap &
```

> **What to look for in Wireshark:** Inspect the recorded frames to verify that MAC destination/source addresses, EtherType values, and payload structures adhere strictly to IEEE 802.3 standard specifications.

#### 🖥️ Terminal C: Inspect RTL Waveforms
After sending traffic, open Terminal C to view cycle-by-cycle digital logic signals inside the hardware design:

```bash
# Navigate to the active lab directory
cd lab1-raw-ethernet

# Open GTKWave to inspect signal traces
gtkwave waveform.vcd
```
