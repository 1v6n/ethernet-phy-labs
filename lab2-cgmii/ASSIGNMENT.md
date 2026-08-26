# Lab 2 Assignment: MAC Layer (AXI-Stream <-> XGMII/CGMII) Implementation

## Overview

In this assignment, you will implement the SystemVerilog RTL for an **IEEE 802.3 Ethernet Media Access Control (MAC)** module and perform hardware waveform analysis. Your design will bridge user-side **AXI-Stream** packet interfaces with a 64-bit **XGMII/CGMII** bus interface inside `top.sv`.

The primary goal of this lab is to **generate live network traffic** (captured with Wireshark on virtual TAP interfaces) and **analyze the resulting cycle-accurate waveforms** in GTKWave or Surfer to validate MAC framing, CRC32 generation/check, and Inter-Packet Gap (IPG) rules.

---

## Hardware Architecture & Verification Datapath

Raw network frames pass between Linux TAP interfaces (`tap0` and `tap1`) and the SystemVerilog hardware module (`top.sv`). Wireshark monitors packet generation at the OS interface, while Verilator captures internal signal states for GTKWave analysis.

```text
+-------------------------------------------------+
|                    top.sv                       |
|                                                 |
|  [ TAP / Wireshark ]                            |
|          | AXI-Stream                           |
|          v                                      |
|  +---------------+                              |
|  |    MAC TX     | (Preamble / SFD Insertion)   |
|  |  +---------+  | (CRC32 FCS Generation)       |
|  |  | crc32.sv|  | (12-byte IPG Enforcement)   |
|  +---------------+                              |
|          | XGMII Bus (TXD[63:0], TXC[7:0])      |
|          v (Trace Dump to FST/GTKWave)          |
|  +---------------+                              |
|  |    MAC RX     | (SFD Synchronization)        |
|  |  +---------+  | (CRC32 FCS Validation)       |
|  |  | crc32.sv|  | (Preamble & FCS Stripping)   |
|  +---------------+                              |
|          | AXI-Stream                           |
|          v                                      |
|  [ TAP / Wireshark ]                            |
+-------------------------------------------------+
```

---

## Lab Tasks

### Task 1: Parallel CRC32 Engine (`crc32.sv`)
Implement a parallel 64-bit IEEE 802.3 CRC calculation engine:
* **Polynomial:** $G(x) = x^{32} + x^{26} + x^{23} + x^{22} + x^{16} + x^{12} + x^{11} + x^{10} + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + 1$ (`0x04C11DB7`).
* Calculate CRC in a single clock cycle across up to 8 valid bytes (`tkeep[7:0]`).

### Task 2: MAC Transmit Module (`mac_tx.sv`)
Implement the transmit MAC layer:
* **Preamble/SFD Insertion:** Prepend `0x55555555555555` (7 bytes) and `0xD5` (1 byte SFD) onto the XGMII output (`TXC = 8'h01`, `TXD[7:0] = 0xFB`).
* **Payload Framing:** Pass AXI-Stream payload bytes onto `TXD[63:0]` with `TXC = 8'h00`.
* **FCS Append:** Append the calculated 32-bit CRC immediately after the final payload byte (`tlast`).
* **IPG Insertion:** Drive `TXC = 8'hFF` and `TXD = 0x0707070707070707` (`/I/`) for at least 12 byte clock cycles between consecutive frames.

### Task 3: MAC Receive Module (`mac_rx.sv`)
Implement the receive MAC layer:
* **SFD Alignment:** Monitor `RXD`/`RXC` for the start marker (`0xFB`) and SFD (`0xD5`).
* **Payload Extraction:** Convert incoming XGMII payload transfers to user AXI-Stream (`rx_tdata`, `rx_tkeep`, `rx_tvalid`, `rx_tlast`).
* **FCS Check:** Compute CRC32 on incoming payload bytes and compare against received FCS bytes. Assert `crc_error` on mismatch.

### Task 4: Top-Level Integration (`top.sv`)
Connect Node A `mac_tx` to Node B `mac_rx` via XGMII bus signals.

---

## Traffic Generation & Protocol Waveform Analysis

You are required to generate live traffic across the emulated link, observe the packets in Wireshark, and capture cycle-accurate waveforms in GTKWave to explain and validate the MAC protocol.

### Execution & Capture Workflow

1. **Start Wireshark on TAP Interfaces:**
   ```bash
   sudo wireshark -k -i tap0 &
   ```

2. **Build Emulated Hardware with Trace Logging:**
   ```bash
   verilator --cc --trace-fst --exe --build --top-module top \
     -CFLAGS "-DTRACE_ENABLE" wrapper.cpp top.sv -o emulator_trace
   ```

3. **Execute Simulation & Trigger Traffic:**
   ```bash
   sudo ./obj_dir/emulator_trace
   
   # Send an ICMP Echo Request in another terminal:
   ping -c 1 -I tap0 10.0.0.2
   ```

4. **Load Waveforms into GTKWave:**
   ```bash
   gtkwave dump.fst &
   ```

---

## Required Waveform Explanations & Report Deliverables

Your report must include annotated GTKWave screenshots correlated with your Wireshark traffic capture. For each scenario below, describe the exact signal states and explain how they validate IEEE 802.3 MAC compliance:

### 1. Inter-Packet Gap (IPG) & Idle Validation
* **Signals to capture:** `xgmii_txc[7:0]`, `xgmii_txd[63:0]`, `tx_tvalid`.
* **Required Analysis:** Annotate the waveform during idle state. Prove that when `tx_tvalid == 0`, `xgmii_txc == 8'hFF` and `xgmii_txd == 0x0707070707070707` (`/I/`). Verify that IPG duration between frames is $\ge 12$ byte cycles.

### 2. Preamble & Start-of-Frame Delimiter (`SFD`) Insertion
* **Signals to capture:** `tx_tvalid`, `xgmii_txc[7:0]`, `xgmii_txd[63:0]`.
* **Required Analysis:** Highlight the transition from IPG Idle to frame transmission. Demonstrate that the MAC TX module emits `/S/` (`0xFB`) on lane 0 with `TXC[0] = 1`, followed by 6 bytes of Preamble (`0x55`) and 1 byte of SFD (`0xD5`).

### 3. AXI-Stream Payload to XGMII Translation
* **Signals to capture:** `tx_tdata[63:0]`, `tx_tkeep[7:0]`, `tx_tlast`, `xgmii_txd[63:0]`, `xgmii_txc[7:0]`.
* **Required Analysis:** Correlate the Ethernet packet bytes captured in Wireshark (Destination MAC, Source MAC, EtherType, Payload) with `tx_tdata` and `xgmii_txd`. Show how multi-byte transfers map across XGMII lanes.

### 4. Frame Check Sequence (FCS / CRC32) Append & Verification
* **Signals to capture:** `tx_tlast`, `crc_calculated[31:0]`, `xgmii_txd[63:0]`, `rx_crc_err`.
* **Required Analysis:** Identify the clock cycle where `tx_tlast` asserts. Show where the 32-bit CRC is placed relative to the final payload byte. Demonstrate that `rx_crc_err` stays `0` on `mac_rx`.

---

## Submission Deliverables

Submit a ZIP archive or repository pull request containing:

1. **RTL Source Files:**
   * `crc32.sv`
   * `mac_tx.sv`
   * `mac_rx.sv`
   * `top.sv`
2. **Wireshark Capture File (`.pcapng`):**
   * Traffic capture showing ARP and ICMP Echo Request/Reply frames generated during the test run.
3. **Waveform Analysis Report (`REPORT.md` or PDF):**
   * Detailed explanations and annotated screenshots for all required waveform scenarios listed above.

---

## Evaluation Rubric

| Assessment Criteria | Weight | Description |
| :--- | :--- | :--- |
| **Waveform Analysis & Protocol Validation** | 40% | Clear explanations of GTKWave traces demonstrating deep understanding of Preamble/SFD insertion, XGMII framing, and CRC32 operation. |
| **MAC Transmit & IPG Implementation** | 20% | Correct RTL implementation of Preamble/SFD, FCS append, and 12-byte IPG enforcement. |
| **Parallel CRC32 Engine** | 20% | Accurate parallel 64-bit CRC32 implementation matching IEEE 802.3 specifications. |
| **HIL Simulation & Traffic Capture** | 20% | Successful bi-directional ICMP ping execution across Linux TAP interfaces matched with Wireshark pcap traces. |
