# Lab 3: Physical Coding Sublayer (PCS) 64b/66b Encoding & Scrambling

This laboratory introduces the **Physical Coding Sublayer (PCS)** for 10G/40G/100G Ethernet networks as specified in **IEEE 802.3 Clause 49** (10GBASE-R) and **Clause 82** (40G/100GBASE-R). You will implement and verify a complete 64b/66b encoder/decoder pair alongside a 58-bit self-synchronizing scrambler/descrambler pipeline in SystemVerilog.

---

## 1. IEEE 802.3 PCS Core Concepts

The PCS sits between the Media Access Control (MAC) interface (XGMII/XLGMII) and the Physical Medium Attachment (PMA) layer. Its primary tasks include:

* **Framing & Overhead Reduction:** Replacing 8b/10b encoding (25% overhead) with 64b/66b framing (3.125% overhead).
* **Control Signaling:** Transporting Ethernet control characters (Idle, Start, Terminate, Error, Ordered Sets) out-of-band within 66-bit blocks.
* **Spectral Density & DC Balance:** Randomizing payload bits using a continuous additive scrambler to guarantee transition density and prevent baseline wander.

```text
+-------------------------------------------------------+
|            Reconciliation Sublayer (RS)               |
+-------------------------------------------------------+
                            |
                            v  XGMII (TXD[63:0], TXC[7:0])
+-------------------------------------------------------+
|            Physical Coding Sublayer (PCS)             |
|  +---------------+  +---------------+  +-----------+  |
|  | 64b/66b Block |->| Self-Sync     |->| Gearbox / |  |
|  | Encoder       |  | Scrambler     |  | Alignment |  |
|  +---------------+  +---------------+  +-----------+  |
+-------------------------------------------------------+
                            |
                            v  66-bit Blocks
+-------------------------------------------------------+
|          Physical Medium Attachment (PMA)             |
+-------------------------------------------------------+
```

---

## 2. 64b/66b Framing & Sync Headers

Every 64-bit payload chunk from the MAC layer is prefixed with a 2-bit **Sync Header** to form a 66-bit block. Sync headers are never scrambled and provide frame alignment at the receiver.

| Sync Header `[1:0]` | Block Classification | Description |
| :--- | :--- | :--- |
| `2'b01` | **Data Block** | Pure 64-bit payload consisting strictly of 8 data octets (`/D0/`–`/D7/`). No Block Type Field is present. |
| `2'b10` | **Control Block** | Mixed block containing an 8-bit Block Type Field (BTF) followed by control codes, delimiters (`/S/`, `/T/`), or ordered sets (`/O/`). |
| `2'b00` | **Invalid** | Reserved / Synchronization error state. |
| `2'b11` | **Invalid** | Reserved / Synchronization error state. |

---

## 3. Block Encoding & Character Mapping

When control characters or frame delimiters are present in the 64-bit input stream, the 66-bit block is formatted as a **Control Block** (`Sync = 2'b10`). The lower 8 bits of the payload contain the **Block Type Field (BTF)**, which defines the structural format of the remaining 56 bits.

### XGMII Control Character to PCS Code Mapping

XGMII control characters (`TXC = 1`) are compressed into 7-bit or 4-bit PCS control codes inside control blocks to make space for the 8-bit BTF:

| Character | XGMII Code (`TXD`) | XGMII Control (`TXC`) | PCS Control Code | Description |
| :--- | :--- | :--- | :--- | :--- |
| `/I/` | `0x07` | `1` | `0x00` | **Idle:** Inter-packet gap (IPG) padding |
| `/E/` | `0xFE` | `1` | `0x1E` | **Error:** Explicit PHY / MAC transfer error |
| `/S/` | `0xFB` | `1` | *Implicit (BTF)* | **Start Delimiter:** Start of frame |
| `/T/` | `0xFD` | `1` | *Implicit (BTF)* | **Terminate Delimiter:** End of frame |
| `/O/` | `0x9C` | `1` | `0x4` / `0xF` | **Ordered Set:** Link fault / status |

---

### IEEE 802.3 Clause 49/82 Block Type Formats

The layout of the 64-bit payload depends directly on the BTF value:

| BTF (Hex) | Input Structure | Payload Bit Construction `[63:0]` |
| :--- | :--- | :--- |
| **Data** | `D0 D1 D2 D3 D4 D5 D6 D7` | `D7 D6 D5 D4 D3 D2 D1 D0` *(Sync = 2'b01)* |
| `0x1E` | `C0 C1 C2 C3 C4 C5 C6 C7` | `C7 C6 C5 C4 C3 C2 C1 C0 0x1E` |
| `0x78` | `S0 D1 D2 D3 D4 D5 D6 D7` | `D7 D6 D5 D4 D3 D2 D1 0x78` |
| `0x4B` | `O0 D1 D2 D3 C4 C5 C6 C7` | `C7 C6 C5 C4 O0 D3 D2 D1 0x4B` |
| `0x87` | `D0 D1 D2 D3 D4 D5 D6 T7` | `0x0000000 D6 D5 D4 D3 D2 D1 D0 0x87` |
| `0x99` | `D0 D1 D2 D3 D4 D5 T6 C7` | `C7 0x00000 D5 D4 D3 D2 D1 D0 0x99` |
| `0xAA` | `D0 D1 D2 D3 D4 T5 C6 C7` | `C7 C6 0x000 D4 D3 D2 D1 D0 0xAA` |
| `0xB4` | `D0 D1 D2 D3 T4 C5 C6 C7` | `C7 C6 C5 0x0 D3 D2 D1 D0 0xB4` |
| `0xCC` | `D0 D1 D2 T3 C4 C5 C6 C7` | `C7 C6 C5 C4 0x00 D2 D1 D0 0xCC` |
| `0xD2` | `D0 D1 T2 C3 C4 C5 C6 C7` | `C7 C6 C5 C4 C3 0x0000 D1 D0 0xD2` |
| `0xE1` | `D0 T1 C2 C3 C4 C5 C6 C7` | `C7 C6 C5 C4 C3 C2 0x000000 D0 0xE1` |
| `0xFF` | `T0 C1 C2 C3 C4 C5 C6 C7` | `C7 C6 C5 C4 C3 C2 C1 0x00000000 0xFF` |

---

## 4. Self-Synchronizing Scrambler & Descrambler

To ensure sufficient signal transitions and eliminate DC bias without increasing bandwidth overhead, the 64-bit payload is processed by a 58-bit polynomial additive scrambler.

### Generator Polynomial
$$G(x) = x^{58} + x^{39} + 1$$

```text
        +---------------------------------------+
        |     58-bit LFSR Shift Register        |
        +---------------------------------------+
            | [57]                   | [38]
            v                        v
Input Bit -> (XOR) <-----------------+
              |
              v
     Scrambled Output Bit
```

For each payload bit $i$ ($0 \le i < 64$):
* **Scramble Equation:** $E_i = D_i \oplus S_{i-39} \oplus S_{i-58}$
* **Descramble Equation:** $D_i = E_i \oplus S_{i-39} \oplus S_{i-58}$

> **Critical Rule:** The 2-bit Sync Header (`Sync[1:0]`) is **NEVER** passed through the scrambler. Only the 64-bit payload is scrambled.

---

## 5. System Architecture & Lab Files

```text
+-------------------------------------------------------+
|                   VERILATOR EMULATOR                  |
|                                                       |
|  [tap0] <--> [MAC A] <--> [PCS A]                     |
|                             |  Scrambled 66-bit Link  |
|                             v                         |
|  [tap1] <--> [MAC B] <--> [PCS B]                     |
+-------------------------------------------------------+
```

### Module File Hierarchy

* `pcs_tx_64b66b.sv`: Translates XGMII data/control streams into 66-bit blocks.
* `pcs_rx_64b66b.sv`: Decodes 66-bit blocks back to XGMII frames.
* `scrambler.sv`: 58-bit parallel LFSR scrambler.
* `descrambler.sv`: 58-bit parallel LFSR descrambler.
* `top.sv`: Top-level SystemVerilog module instantiating Node A and Node B.
* `wrapper.cpp`: Verilator C++ driver handling Linux TAP interfaces (`tap0`, `tap1`).
* `setup_netns.sh`: Shell script configuring `ns_b` network namespace.

---

## 6. Quickstart & Execution

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y verilator build-essential iproute2 ethtool
```

### Execution Steps

1. **Configure Network Environment:**
   ```bash
   chmod +x scripts/setup_netns.sh
   sudo ./scripts/setup_netns.sh
   ```

2. **Build Hardware Emulator:**
   ```bash
   verilator --cc --trace-fst --exe --build --top-module top -j 0 \
     -Wall -Wno-DECLFILENAME -Wno-BLKSEQ wrapper.cpp top.sv -o emulator
   ```

3. **Run Simulation:**
   ```bash
   # Run with scrambler active
   sudo ./obj_dir/emulator --enable-scrambler
   ```

4. **Verify ICMP Traffic (Separate Terminal):**
   ```bash
   ping -c 4 -I tap0 10.0.0.2
   ```
