# 🔹 Lab 3: Physical Coding Sublayer (PCS) 64b/66b & Scrambling

This lab implements an IEEE 802.3 Clause 49 compliant **Physical Coding Sublayer (PCS)**. The RTL logic maps 64-bit XGMII data/control streams into 66-bit PCS blocks, scrambles payload data using a self-synchronizing polynomial, and executes receiver block lock state tracking.

---

## 📚 Fundamental Theory & Key Concepts

### 1. Why 64b/66b Encoding?
Legacy Ethernet (e.g., 1GbE) uses **8b/10b encoding**, which incurs a **25% bandwidth overhead** (2 extra bits for every 8 data bits). IEEE 802.3 Clause 49 introduced **64b/66b encoding** for 10GbE and faster standards, reducing protocol overhead to **~3.125%** ($2 / 64$) by appending a 2-bit Sync Header to a 64-bit payload vector.

### 2. PCS Block Sync Headers
Every 66-bit block consists of a 2-bit Sync Header followed by a 64-bit payload:

| Sync Header (`[1:0]`) | Block Type | Description / Formatting Rules |
| :--- | :--- | :--- |
| `2'b01` | **Data Block** | All 64 bits contain pure data (`TXD[63:0]`). No block-type byte required. |
| `2'b10` | **Control Block** | Payload contains an 8-bit Block Type field followed by control/data characters. |
| `2'b00` | **Invalid** | Reserved / Framing Error. Indicates bit corruption on serial line. |
| `2'b11` | **Invalid** | Reserved / Framing Error. Triggers lock loss in receiver FSM. |

### 3. IEEE 802.3 Control Block Type Fields
When `Sync Header = 2'b10`, the low-order byte of the 64-bit payload defines the control block structure:

| Block Type Field (`Payload[7:0]`) | Meaning | Signal / Character Mapping |
| :--- | :--- | :--- |
| `0x1E` | Control / Idle | 8 Control Characters (e.g., IDLE `/I/` `0x07`). |
| `0x78` | Start (`/S/`) | Frame Start byte aligned at lane 0 (`TXD = 0xFB`). |
| `0x87` – `0xFF` | Terminate (`/T/`) | Frame End byte located in lanes 0 through 7 (`TXD = 0xFD`). |

---

## ⚡ IEEE 802.3 Polynomial Scrambling

Because 64b/66b encoding does not guarantee transition density or DC balance on its own, IEEE 802.3 applies a **self-synchronizing scrambler** to randomize payload data.

### Scrambler Equations
* **Generator Polynomial:**
  $$G(X) = 1 + X^{39} + X^{58}$$

* **TX Scrambler Logic:**
  $$S_i = P_i \oplus S_{i-39} \oplus S_{i-58}$$
  *(Where $P_i$ is raw payload bit $i$, and $S_i$ is scrambled output bit $i$.)*

* **RX Descrambler Logic:**
  $$P_i = S_i \oplus S_{i-39} \oplus S_{i-58}$$

> ⚠️ **Critical Rule:** The **2-bit Sync Header (`2'b01` / `2'b10`) is NEVER scrambled**. It passes through unchanged so the receiver can establish block framing alignment without needing to descramble first.

---

## 🎯 Lab Objectives
1. Understand **64b/66b Block Construction**: Differentiate between Data sync headers (`2'b01`) and Control sync headers (`2'b10`).
2. Compare **Unscrambled vs. Scrambled Data**: Directly inspect how repeating patterns (like IDLE `0x07`) are randomized without modifying the 2-bit sync headers.
3. Validate **Receiver Block Lock**: Monitor the lock state machine as it scans incoming stream sync headers.

---

## 🛠️ Execution & Waveform Inspection Guide

### 1. Build and Run Simulation
Open **Terminal A** to compile and run the Verilator emulator:

```bash
cd lab3-pcs-64b66b
verilator --cc --trace --exe --build -j 0 -Wall wrapper.cpp top.sv -o emulator
./obj_dir/emulator
```

### 2. Inspect Waveforms in GTKWave
Open **Terminal B** to view signal traces:

```bash
cd lab3-pcs-64b66b
gtkwave waveform.vcd
```

### 🔍 Signals to Add in GTKWave:
* `TOP.top.clk` & `TOP.top.rst_n`
* `TOP.top.xgmii_txd[63:0]` & `TOP.top.xgmii_txc[7:0]` (XGMII Input)
* `TOP.top.unscrambled_tx_block[65:0]` (Raw Encoded PCS Block: `{64'hPayload, 2'bSync}`)
* `TOP.top.scrambled_tx_block[65:0]` (Scrambled Output: `{64'hScrambled, 2'bSync}`)
* `TOP.top.block_lock` (Receiver Alignment Status)
