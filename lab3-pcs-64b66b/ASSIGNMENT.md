# Lab 3 Assignment: PCS 64b/66b Encoding & Scrambling Implementation

## Overview

In this assignment, you will implement the SystemVerilog RTL for an **IEEE 802.3 Clause 49/82 Physical Coding Sublayer (PCS)** and perform detailed hardware waveform analysis. Your design sits behind a MAC layer inside `top.sv`, taking XGMII bus transfers, encoding them into 66-bit blocks, scrambling the payloads with a 58-bit LFSR, and decoding them back into valid frames on the receiving node.

The primary objective of this lab is to **generate live network traffic** (captured via Wireshark on virtual TAP interfaces) and **analyze the resulting cycle-accurate waveforms** in GTKWave or Surfer to validate IEEE 802.3 PCS encoding and scrambling mechanics.

---

## Hardware Architecture & Verification Datapath

Raw network frames pass between Linux TAP interfaces (`tap0` and `tap1`) and the SystemVerilog hardware module (`top.sv`). Wireshark monitors packet generation at the OS interface, while Verilator captures internal signal states for GTKWave analysis.

```text
+-------------------------------------------------+
|                    top.sv                       |
|                                                 |
|  [ TAP / Wireshark ]                            |
|          | Raw Bytes                            |
|          v                                      |
|  +---------------+                              |
|  |    MAC TX     | (Preamble, SFD, CRC32, IPG)  |
|  +---------------+                              |
|          | XGMII (TXD[63:0], TXC[7:0])           |
|          v                                      |
|  +---------------+                              |
|  |    PCS TX     | (64b/66b Encoder)            |
|  +---------------+                              |
|          | 66b Block (Sync[1:0] + Payload[63:0]) |
|          v                                      |
|  +---------------+                              |
|  |   Scrambler   | (58-bit LFSR: G(x))          |
|  +---------------+                              |
|          | Scrambled Link                       |
|          v (Trace Dump to FST/GTKWave)          |
|  +---------------+                              |
|  |  Descrambler  |                              |
|  +---------------+                              |
|          |                                      |
|          v                                      |
|  +---------------+                              |
|  |    PCS RX     | (64b/66b Decoder)            |
|  +---------------+                              |
|          | XGMII                                |
|          v                                      |
|  +---------------+                              |
|  |    MAC RX     |                              |
|  +---------------+                              |
|          | Raw Bytes                            |
|          v                                      |
|  [ TAP / Wireshark ]                            |
+-------------------------------------------------+
```

---

## Lab Tasks

### Task 1: 64b/66b Encoder (`pcs_tx_64b66b.sv`)
Implement the 64b/66b block encoder according to IEEE 802.3 Clause 49 rules:
* **Data Blocks:** When `TXC[7:0] == 8'h00`, set `Sync = 2'b01` and pass `TXD[63:0]` directly as payload.
* **Control Blocks:** When `TXC != 8'h00`, set `Sync = 2'b10`. Map XGMII control characters (`/I/`, `/S/`, `/T/`, `/E/`, `/O/`) into their corresponding **Block Type Fields (BTF)** and PCS control codes.
* **Sync Headers:** Ensure Sync Headers `2'b01` and `2'b10` are strictly formatted and never inverted.

### Task 2: Parallel LFSR Scrambler & Descrambler (`scrambler.sv`, `descrambler.sv`)
Implement a 58-bit parallel self-synchronizing additive scrambler and matching descrambler:
* **Polynomial:** $G(x) = x^{58} + x^{39} + 1$.
* **Operation:** Compute 64 XOR equations in parallel for each 64-bit payload word based on current LFSR state.
* **Header Protection:** **NEVER** scramble the 2-bit Sync Header (`Sync[1:0]`).

### Task 3: 64b/66b Decoder (`pcs_rx_64b66b.sv`)
Implement the receiving PCS decoder:
* Inspect incoming 2-bit Sync Headers. Assert `sync_error` if `Sync == 2'b00` or `2'b11`.
* Decode BTF values (`0x1E`, `0x78`, `0x87`, etc.) back into standard 64-bit XGMII receive buses (`RXD[63:0]`, `RXC[7:0]`).

### Task 4: Hardware Integration (`top.sv`)
Connect the complete Node A to Node B datapath:
* Wire `mac_tx` -> `pcs_tx_64b66b` -> `scrambler` -> `descrambler` -> `pcs_rx_64b66b` -> `mac_rx`.

---

## Traffic Generation & Protocol Waveform Analysis

You are required to generate live traffic across the emulated link, observe the packets in Wireshark, and capture cycle-accurate waveforms in GTKWave to explain and validate the PCS protocol.

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
   sudo ./obj_dir/emulator_trace --enable-scrambler
   
   # In a separate terminal, send an ICMP Echo Request:
   ping -c 1 -I tap0 10.0.0.2
   ```

4. **Load Waveforms into GTKWave:**
   ```bash
   gtkwave dump.fst &
   ```

---

## Required Waveform Explanations & Report Deliverables

Your report must include annotated GTKWave screenshots correlated with your Wireshark traffic capture. For each scenario below, describe the exact signal states and explain how they validate IEEE 802.3 Clause 49/82 compliance:

### 1. Inter-Packet Gap (IPG) & Control Block Validation
* **Signals to capture:** `xgmii_txc`, `xgmii_txd`, `sync_header[1:0]`, `block_type_field[7:0]`, `pcs_payload[63:0]`.
* **Required Analysis:** Annotate the waveform during idle link state (between packets). Prove that `xgmii_txc == 8'hFF` and `xgmii_txd` contains `/I/` characters (`0x07`). Demonstrate that the PCS encoder generates a Control Sync Header (`2'b10`) with `BTF = 0x1E` and compresses the 8 idle control bytes into 7-bit PCS control codes (`0x00`).

### 2. Packet Start Delimiter (`/S/`) & Preamble Entry
* **Signals to capture:** `xgmii_txc`, `xgmii_txd`, `sync_header[1:0]`, `block_type_field[7:0]`.
* **Required Analysis:** Highlight the exact clock cycle where packet transmission starts. Correlate the Ethernet frame observed in Wireshark with the XGMII Start character (`/S/` = `0xFB` on lane 0). Show that the PCS block encoder outputs `Sync = 2'b10` and `BTF = 0x78` to indicate frame start.

### 3. Payload Data Block Framing
* **Signals to capture:** `xgmii_txc`, `sync_header[1:0]`, `unscrambled_payload[63:0]`, `scrambled_payload[63:0]`.
* **Required Analysis:** Show a sequence of pure Ethernet payload transfers (`TXC == 8'h00`). Validate that the encoder switches to a Data Sync Header (`2'b01`) with no Block Type Field present. Compare the bytes in Wireshark against `unscrambled_payload[63:0]`.

### 4. Scrambler Invariance & Randomization Verification
* **Signals to capture:** `enable_scrambler`, `sync_header[1:0]`, `unscrambled_payload[63:0]`, `scrambled_payload[63:0]`, `descrambled_payload[63:0]`.
* **Required Analysis:**
  * Compare `unscrambled_payload` against `scrambled_payload` to demonstrate bit randomization according to $G(x) = x^{58} + x^{39} + 1$.
  * Prove that `sync_header[1:0]` is **identical** before and after scrambling (verifying sync headers are never passed through the LFSR).
  * Show that `descrambled_payload[63:0]` perfectly matches `unscrambled_payload[63:0]` on the RX side.

### 5. Packet Termination Delimiter (`/T/`)
* **Signals to capture:** `xgmii_txc`, `xgmii_txd`, `sync_header[1:0]`, `block_type_field[7:0]`.
* **Required Analysis:** Identify the final transfer of an Ethernet frame where the Terminate character (`/T/` = `0xFD`) appears. Identify which specific BTF (`0x87`, `0x99`, `0xAA`, `0xB4`, `0xCC`, `0xD2`, `0xE1`, or `0xFF`) was selected based on the number of valid data octets in the final word.

---

## Submission Deliverables

Submit a ZIP archive or repository pull request containing:

1. **RTL Source Files:**
   * `pcs_tx_64b66b.sv`
   * `pcs_rx_64b66b.sv`
   * `scrambler.sv`
   * `descrambler.sv`
   * `top.sv`
2. **Wireshark Capture File (`.pcapng`):**
   * Traffic capture showing ARP and ICMP Echo Request/Reply frames generated during the test run.
3. **Waveform Analysis Report (`REPORT.md` or PDF):**
   * Detailed explanations and annotated screenshots for all 5 required waveform scenarios listed above.
   * Signal trace callouts proving 64b/66b block encoding and scrambler invariance.

---

## Evaluation Rubric

| Assessment Criteria | Weight | Description |
| :--- | :--- | :--- |
| **Waveform Analysis & Protocol Validation** | 40% | Clear, thorough explanations of GTKWave traces demonstrating deep understanding of 64b/66b framing, BTF selection, and LFSR operation. |
| **64b/66b Block Encoding Implementation** | 20% | Correct RTL translation of XGMII control characters and data octets into Clause 49/82 block formats. |
| **Parallel LFSR Scrambler & Descrambler** | 20% | Accurate parallel implementation of $G(x) = x^{58} + x^{39} + 1$ without scrambling Sync Headers. |
| **System Co-Simulation & Traffic Capture** | 20% | Successful bidirectional ICMP traffic execution across Linux TAP interfaces matched with Wireshark trace files. |
