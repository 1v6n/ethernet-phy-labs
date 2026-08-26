# Lab 3 Assignment: PCS 64b/66b Encoding & Scrambling Traffic Generation & Waveform Protocol Analysis

## Overview

In this assignment, you will evaluate a complete, pre-provided SystemVerilog implementation of an **IEEE 802.3 Clause 49/82 Physical Coding Sublayer (PCS)** including a 64b/66b block encoder/decoder and a 58-bit parallel LFSR scrambler/descrambler. The entire hardware stack (`mac_tx.sv`, `mac_rx.sv`, `pcs_tx_64b66b.sv`, `pcs_rx_64b66b.sv`, `scrambler.sv`, `descrambler.sv`, `top.sv`) and software driver (`wrapper.cpp`) are fully implemented and provided.

Your objective is **not** to write RTL code, but to **execute the Hardware-in-the-Loop (HIL) simulation, generate live network traffic, capture packet traces in Wireshark, and perform cycle-accurate waveform analysis** in GTKWave or Surfer to validate 64b/66b framing, control character mapping, and scrambler mechanics.

---

## Architecture & Verification Pipeline

The provided environment interfaces a MAC layer with the PCS layer inside `top.sv`. Ethernet frames from the OS flow through Linux virtual TAP interfaces (`tap0`, `tap1`), translate into XGMII streams at the MAC, and undergo 64b/66b framing and 58-bit LFSR scrambling at the PCS layer.

```text
+-------------------------------------------------------+
|                    top.sv (PROVIDED)                  |
|                                                       |
|  [ TAP / Wireshark ]                                  |
|          | Raw Bytes                                  |
|          v                                            |
|  +------------------+                                 |
|  |      MAC TX      | (Preamble, SFD, CRC32, IPG)     |
|  +------------------+                                 |
|          | XGMII (TXD[63:0], TXC[7:0])                |
|          v                                            |
|  +------------------+                                 |
|  |  pcs_tx_64b66b   | (64b/66b Block Encoder)         |
|  +------------------+                                 |
|          | 66-bit Block (Sync[1:0] + Payload[63:0])    |
|          v                                            |
|  +------------------+                                 |
|  |    scrambler     | (58-bit LFSR: G(x)=x^58+x^39+1) |
|  +------------------+                                 |
|          | Scrambled Link                             |
|          v (Trace Dump to FST/GTKWave)                |
|  +------------------+                                 |
|  |   descrambler    | (58-bit LFSR Recovery)          |
|  +------------------+                                 |
|          | Restored 66-bit Block                      |
|          v                                            |
|  +------------------+                                 |
|  |  pcs_rx_64b66b   | (64b/66b Block Decoder)         |
|  +------------------+                                 |
|          | XGMII                                      |
|          v                                            |
|  +------------------+                                 |
|  |      MAC RX      |                                 |
|  +------------------+                                 |
|          | Raw Bytes                                  |
|          v                                            |
|  [ TAP / Wireshark ]                                  |
+-------------------------------------------------------+
```

---

## Lab Execution Steps

### 1. Setup Virtual Network Interfaces
Configure the Linux virtual TAP interfaces (`tap0` and `tap1`) and network namespace (`ns_b`):
```bash
chmod +x scripts/setup_netns.sh
sudo ./scripts/setup_netns.sh
```

### 2. Compile SystemVerilog Hardware Model with Trace Logging
Build the Verilator C++ executable with waveform tracing flags enabled:
```bash
verilator --cc --trace-fst --exe --build --top-module top \
  -CFLAGS "-DTRACE_ENABLE" wrapper.cpp top.sv -o emulator_trace
```

### 3. Start Wireshark Packet Capture
Launch Wireshark in the background to record network traffic passing through the emulated physical link:
```bash
sudo wireshark -k -i tap0 &
```

### 4. Execute Simulation & Generate Traffic
Run the compiled simulation model with scrambling enabled and send ICMP Echo Request packets across the link:
```bash
# Terminal 1: Run hardware emulator with scrambler active
sudo ./obj_dir/emulator_trace --enable-scrambler

# Terminal 2: Trigger network traffic
ping -c 2 -I tap0 10.0.0.2
```

### 5. Inspect Signal Waveforms
Open the generated FST trace file in GTKWave or Surfer:
```bash
gtkwave dump.fst &
```

---

## Required Waveform Explanations & Report Tasks

Your primary task is to write a comprehensive report (`REPORT.md` or PDF) containing annotated GTKWave screenshots correlated with your Wireshark traffic capture (`.pcapng`). For each scenario below, describe the exact signal states and explain how they prove IEEE 802.3 Clause 49/82 PCS compliance:

### 1. Inter-Packet Gap (IPG) & Control Block Validation
* **Signals to capture:** `xgmii_txc[7:0]`, `xgmii_txd[63:0]`, `sync_header[1:0]`, `block_type_field[7:0]`, `pcs_payload[63:0]`.
* **Required Analysis:** Annotate the waveform during idle state between packets (`xgmii_txc == 8'hFF`, `0x07` idle bytes). Prove that the PCS encoder generates a Control Sync Header (`2'b10`) with `BTF = 0x1E` and compresses the 8 idle control bytes into 7-bit PCS control codes (`0x00`).

### 2. Packet Start Delimiter (`/S/`) & Control Block Framing
* **Signals to capture:** `xgmii_txc[7:0]`, `xgmii_txd[63:0]`, `sync_header[1:0]`, `block_type_field[7:0]`.
* **Required Analysis:** Highlight the exact clock cycle where packet transmission begins on XGMII (`/S/` = `0xFB` on Lane 0). Show that the PCS block encoder outputs a Control Sync Header (`2'b10`) and `BTF = 0x78` to indicate frame start.

### 3. Payload Data Block Framing
* **Signals to capture:** `xgmii_txc[7:0]`, `sync_header[1:0]`, `unscrambled_payload[63:0]`, `scrambled_payload[63:0]`.
* **Required Analysis:** Show a sequence of pure Ethernet payload transfers (`TXC == 8'h00`). Validate that the encoder switches to a Data Sync Header (`2'b01`) with no Block Type Field. Match the raw packet hex bytes in Wireshark with `unscrambled_payload[63:0]` word-by-word.

### 4. Scrambler Invariance & Bit Randomization
* **Signals to capture:** `enable_scrambler`, `sync_header[1:0]`, `unscrambled_payload[63:0]`, `scrambled_payload[63:0]`, `descrambled_payload[63:0]`.
* **Required Analysis:**
  * Compare `unscrambled_payload` against `scrambled_payload` to demonstrate bit randomization according to $G(x) = x^{58} + x^{39} + 1$.
  * Prove that `sync_header[1:0]` is **identical** before and after scrambling, verifying that Sync Headers are never passed through the LFSR.
  * Show that `descrambled_payload[63:0]` on the RX side perfectly matches `unscrambled_payload[63:0]` on the TX side.

### 5. Packet Termination Delimiter (`/T/`) & BTF Selection
* **Signals to capture:** `xgmii_txc[7:0]`, `xgmii_txd[63:0]`, `sync_header[1:0]`, `block_type_field[7:0]`.
* **Required Analysis:** Identify the final transfer word of an Ethernet frame containing the Terminate character (`/T/` = `0xFD`). Identify which specific BTF (`0x87`, `0x99`, `0xAA`, `0xB4`, `0xCC`, `0xD2`, `0xE1`, or `0xFF`) was selected based on the position of `/T/` in the 64-bit XGMII word.

---

## Submission Deliverables

Submit a ZIP archive or repository pull request containing:

1. **Wireshark Capture File (`traffic_capture.pcapng`):**
   * Packet capture containing the ARP and ICMP Echo Request/Reply frames generated during your test execution.
2. **Waveform Trace File (`dump.fst`):**
   * Simulation trace file covering the traffic generation run.
3. **Waveform Analysis Report (`REPORT.md` or PDF):**
   * Detailed written protocol analysis.
   * Clear, annotated GTKWave screenshots corresponding to all 5 required analysis scenarios listed above.
