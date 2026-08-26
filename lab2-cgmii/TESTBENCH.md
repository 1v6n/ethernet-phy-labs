# Lab 2 Testbench & Verification Guide

This document describes the testbench architecture, hardware hierarchy, and verification workflows for the **IEEE 802.3 MAC Layer (AXI-Stream <-> XGMII)** implementation.

---

## 1. Testbench Architecture Overview

The verification environment uses a **Hardware-in-the-Loop (HIL) Co-Simulation Model**. Real Ethernet frames from the host Linux network stack flow through C++ TAP wrappers into SystemVerilog AXI-Stream interfaces.

All MAC framing operations—Preamble/SFD insertion, CRC32 generation, IPG enforcement, and frame checks—are executed entirely inside `top.sv`.

```text
+-------------------------------------------------------+
|                   HOST ENVIRONMENT                    |
|                                                       |
|  [ Linux Stack ]               [ Linux Stack (ns_b) ] |
|  IP: 10.0.0.1                  IP: 10.0.0.2           |
|        |                                 ^            |
|        v (tap0)                          | (tap1)     |
+--------|---------------------------------|------------+
         | Raw Bytes                       | Raw Bytes  
+--------|---------------------------------|------------+
|        v                                 |            |
|  [ wrapper.cpp ]                   [ wrapper.cpp ]    |
|        |                                 ^            |
|        v AXI-Stream                      | AXI-Stream |
|  +-------------------------------------------------+  |
|  | top.sv                                          |  |
|  |                                                 |  |
|  | [ MAC TX A ] -----> XGMII Bus -----> [ MAC RX B]|  |
|  +-------------------------------------------------+  |
+-------------------------------------------------------+
```

---

## 2. Hardware Hierarchy inside `top.sv`

```text
+-------------------------------------------------------+
|                   top.sv DATAPATH                     |
|                                                       |
|  AXI-Stream Input (wrapper.cpp)                       |
|        | (tx_tdata, tx_tkeep, tx_tvalid, tx_tlast)    |
|        v                                              |
|  +------------+                                       |
|  |   mac_tx   | ---> Generates Preamble / SFD         |
|  |  +------+  | ---> Computes CRC32 via crc32.sv      |
|  |  |CRC32 |  | ---> Appends FCS & Inserts IPG (0x07) |
|  +------------+                                       |
|        | XGMII Bus (TXD[63:0], TXC[7:0])              |
|        v                                              |
|  +------------+                                       |
|  |   mac_rx   | ---> Aligns on SFD (0xD5)             |
|  |  +------+  | ---> Checks CRC32 via crc32.sv        |
|  |  |CRC32 |  | ---> Strips Preamble & FCS            |
|  +------------+                                       |
|        | AXI-Stream Output                            |
|        v (rx_tdata, rx_tkeep, rx_tvalid, rx_tlast)    |
|  AXI-Stream Output (wrapper.cpp)                      |
+-------------------------------------------------------+
```

---

## 3. Top-Level Interface Signals (`top.sv`)

| Signal | Direction | Width | Description |
| :--- | :--- | :--- | :--- |
| `clk` | Input | 1 bit | System clock (156.25 MHz equivalent) |
| `rst_n` | Input | 1 bit | Active-low synchronous reset |
| **Node A AXI-Stream TX** | | | |
| `a_tx_tdata` | Input | 64 bits | Transmit packet payload |
| `a_tx_tkeep` | Input | 8 bits | Byte valid mask |
| `a_tx_tvalid` | Input | 1 bit | Payload valid indicator |
| `a_tx_tlast` | Input | 1 bit | End-of-packet marker |
| `a_tx_tready` | Output | 1 bit | MAC ready indicator |
| **Node B AXI-Stream RX** | | | |
| `b_rx_tdata` | Output | 64 bits | Received packet payload |
| `b_rx_tkeep` | Output | 8 bits | Byte valid mask |
| `b_rx_tvalid` | Output | 1 bit | Payload valid indicator |
| `b_rx_tlast` | Output | 1 bit | End-of-packet marker |
| `b_rx_crc_err` | Output | 1 bit | Asserted high on CRC32 mismatch |

---

## 4. Verification Workflows

### Scenario A: Co-Simulation Ping Verification

1. **Setup Interfaces and Build Emulator:**
   ```bash
   sudo ./scripts/setup_netns.sh
   verilator --cc --trace-fst --exe --build --top-module top -j 0 \
     -Wall -Wno-DECLFILENAME -Wno-BLKSEQ wrapper.cpp top.sv -o emulator
   ```

2. **Run Emulator:**
   ```bash
   sudo ./obj_dir/emulator
   ```

3. **Trigger Traffic (Second Terminal):**
   ```bash
   ping -c 5 -I tap0 10.0.0.2
   ```

---

### Scenario B: Waveform Trace Generation

1. **Compile with Tracing Enabled:**
   ```bash
   verilator --cc --trace-fst --exe --build --top-module top \
     -CFLAGS "-DTRACE_ENABLE" wrapper.cpp top.sv -o emulator_trace
   ```

2. **Execute and Capture Trace:**
   ```bash
   sudo ./obj_dir/emulator_trace
   ping -c 1 -I tap0 10.0.0.2
   ```

3. **Inspect in GTKWave:**
   ```bash
   gtkwave dump.fst &
   ```

---

## 5. GTKWave Signal Checklist

| Module Path | Signal | Expected Hardware Behavior |
| :--- | :--- | :--- |
| `top.u_mac_tx_a` | `xgmii_txc[7:0]` | `8'hFF` during IPG; `8'h01` on Start/SFD; `8'h00` during frame payload. |
| `top.u_mac_tx_a` | `xgmii_txd[63:0]` | `0x0707...` during IPG; `0xD5555555555555FB` on frame start. |
| `top.u_mac_tx_a` | `crc_out[31:0]` | Computes CRC32 over payload and appends immediately following `tlast`. |
| `top.u_mac_rx_b` | `crc_error` | Must remain `1'b0`. Asserts `1'b1` if received FCS does not match calculated CRC. |
| `top.u_mac_rx_b` | `axis_rx_tvalid` | Asserts high only when clean packet payload bytes are presented on `rx_tdata`. |
