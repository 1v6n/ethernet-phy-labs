# Lab 2: 64-Bit XGMII Interface & Reconciliation Sublayer (RS)

This laboratory focuses on the 10G/25G **XGMII (10 Gigabit Media Independent Interface)** operating at the Reconciliation Sublayer (RS) level, based on **IEEE 802.3ae** specifications. It converts raw Ethernet frames into 64-bit parallel data/control streams (`TXD[63:0]` / `TXC[7:0]`), calculates and appends the 32-bit Frame Check Sequence (FCS / CRC-32), and exposes internal packet header fields via RTL state tracking.

---

## 💡 Key Concepts from IEEE 802.3ae

The Reconciliation Sublayer (RS) maps the IEEE 802.3 MAC service interface to the physical coding layers using the XGMII structure.

### 1. Data vs. Control Signals
The 64-bit XGMII interface splits traffic into 8 parallel 8-bit lanes (`Lane 0` to `Lane 7`). An 8-bit control mask (`TXC` / `RXC`) designates whether each lane contains **Data** or a **Control Character**:

* **`TXC[i] = 0`**: Lane $i$ carries standard **Data** (Payload, MAC addresses, EtherType, or FCS).
* **`TXC[i] = 1`**: Lane $i$ carries an **XGMII Control Character** (Idle, Start, Terminate, Error).

### 2. XGMII Control Characters

| Control Character | Symbol | Hex Code | TXC Bit | Description |
| :--- | :--- | :--- | :--- | :--- |
| **Idle** | `/I/` | `0x07` | `1` | Transmitted during Inter-Frame Gap (IFG) when no packet is active. |
| **Start** | `/S/` | `0xFB` | `1` | Marks the beginning of a frame. **Must strictly align to Lane 0**. |
| **Terminate** | `/T/` | `0xFD` | `1` | Marks the end of a frame. Can appear in any lane (`Lane 0..7`). |
| **Preamble** | — | `0x55` | `0` | Synchronization sequence (6 bytes following `/S/`). |
| **SFD** | — | `0xD5` | `0` | Start Frame Delimiter (1 byte following Preamble). |

### 3. XGMII 64-Bit Frame Structure

```text
Clock Cycle 0 (Idle State):
  TXD: 0x0707070707070707 | TXC: 0xFF  (All 8 Lanes = /I/ Control)

Clock Cycle 1 (Start of Frame - /S/ + Preamble + SFD):
  TXD: 0xD5555555555555FB | TXC: 0x01  (Lane 0 = /S/, Lanes 1..6 = Preamble, Lane 7 = SFD)

Clock Cycle 2..N (Ethernet Frame Header + Payload + FCS):
  TXD: [8 Bytes Data]     | TXC: 0x00  (All 8 Lanes = Data)

Clock Cycle N+1 (End of Frame - Payload/FCS + /T/ + /I/):
  TXD: 0x070707FD[FCS/Data]| TXC: 0xFC  (Data/FCS Bytes, Lane X = /T/, Remaining = /I/)
```

### 4. Frame Check Sequence (FCS / CRC-32)
* **Transmit (TX):** The C++ wrapper computes the 32-bit IEEE 802.3 polynomial CRC over the L2 frame (`Dst MAC` through `Payload`) and appends it immediately before inserting the `/T/` character.
* **Receive (RX):** The C++ receiver checks the trailing 4-byte CRC-32 before forwarding the frame to the TAP interface. If invalid, the frame is dropped.

---

## 🎯 RTL Debug Signals & Waveform Inspection

The SystemVerilog top module (`top.sv`) includes an internal FSM and sliding window that decodes packet fields in real time and latches header values for GTKWave waveform inspection:

### State Machine Reference (`debug_state_a` / `debug_state_b`)

| Value | State Name | Description |
| :---: | :--- | :--- |
| **0** | `ST_IDLE` | Inter-Frame Gap / Transmitting Idles (`/I/ = 0x07`). |
| **1** | `ST_PREAMBLE` | Start Character (`/S/ = 0xFB`) + Preamble (`0x55`) + SFD (`0xD5`). |
| **2** | `ST_DST_MAC` | 48-bit Destination MAC Address extraction. |
| **3** | `ST_SRC_MAC` | 48-bit Source MAC Address extraction. |
| **4** | `ST_ETHERTYPE` | 16-bit EtherType / Length field. |
| **5** | `ST_PAYLOAD` | Packet payload data. |
| **6** | `ST_FCS` | 32-bit Frame Check Sequence (CRC-32). |
| **7** | `ST_TERMINATE` | Terminate Control character (`/T/ = 0xFD`) and trailing `/I/` idles. |

### Latched Signals Available in GTKWave
* **`mac_dst_a[47:0]` / `mac_dst_b[47:0]`**: Latches target hardware address.
* **`mac_src_a[47:0]` / `mac_src_b[47:0]`**: Latches sender hardware address.
* **`ethertype_a[15:0]` / `ethertype_b[15:0]`**: Latches Layer 3 protocol type (e.g., `0x0800` for IPv4, `0x0806` for ARP).
* **`fcs_a[31:0]` / `fcs_b[31:0]`**: Latches trailing CRC-32 value.

---

## 🏃 How to Run

1. **Initialize Network Namespaces and TAP Interfaces:**
   ```bash
   sudo ../scripts/setup_netns.sh
   ```

2. **Compile and Run Verilator Emulation:**
   ```bash
   verilator --cc --trace --exe --build -j 0 -Wall wrapper.cpp top.sv -o emulator
   sudo ./obj_dir/emulator
   ```

3. **Generate Test Traffic (In a second terminal):**
   ```bash
   ping -c 2 -I tap0 10.0.0.2
   ```

4. **Inspect Waveforms in GTKWave:**
   ```bash
   gtkwave waveform.vcd
   ```
   *Add `debug_state_a`, `mac_dst_a`, `mac_src_a`, `ethertype_a`, and `fcs_a` to observe header field extraction and state transitions aligned with clock cycles.*
