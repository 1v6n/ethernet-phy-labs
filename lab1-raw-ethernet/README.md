# Lab 1: Raw Layer 2 Ethernet Pipeline

This laboratory establishes the foundational Hardware-in-the-Loop (HIL) pipeline. It streams raw Layer 2 Ethernet frames byte-by-byte through a SystemVerilog model (`top.sv`) connected to Linux TAP interfaces (`tap0` and `tap1`).

---

## 💡 Key Concept: Anatomy of a Raw Ethernet Frame

When Linux TAP devices (or OS DMA engines) exchange packets with software, they present **raw Layer 2 frames**. Lower-level hardware artifacts like **Preamble/SFD** and **FCS (CRC-32)** are excluded at this level because they are generated or stripped by physical MAC/PHY hardware:

```text
  +-------------------+-------------------+-------------------+------------------------+
  |  Dst MAC Address  |  Src MAC Address  | EtherType / Length|        Payload         |
  |     (6 Bytes)     |     (6 Bytes)     |     (2 Bytes)     |    (46 - 1500 Bytes)   |
  +-------------------+-------------------+-------------------+------------------------+
  ^                   ^                   ^                   ^
  Byte 0              Byte 6              Byte 12             Byte 14
```

### Frame Field Breakdown

| Field | Length | Description | Common Values |
| :--- | :--- | :--- | :--- |
| **Destination MAC** | 6 Bytes | Physical hardware address of target interface. | `ff:ff:ff:ff:ff:ff` (Broadcast), `02:42:...` |
| **Source MAC** | 6 Bytes | Physical hardware address of sender interface. | Unique MAC assigned to `tap0` or `tap1`. |
| **EtherType** | 2 Bytes | Identifies upper Layer 3 protocol in payload. | `0x0800` (IPv4), `0x0806` (ARP), `0x86DD` (IPv6) |
| **Payload** | 46–1500 B | Network layer data (IPv4 Header + ICMP Request/Reply). | Padded to 46 bytes if shorter. |

---

## 🎯 Educational Objectives

1. Understand how Linux kernel socket buffers (`sk_buff`) pass raw frames to hardware.
2. Capture traffic using **Wireshark / tcpdump** and trace the exact byte layout.
3. Inspect the VCD simulation waveform in **GTKWave** to manually decode signals byte-by-byte on clock edges.

---

## 🔍 Packet Inspection & Analysis

### 1. Wireshark Packet Inspection
When capturing on `tap0`, Wireshark parses the raw byte array into standard protocol layers:

- **Ethernet II Header:** Displays Destination MAC, Source MAC, and EtherType.
- **Address Resolution Protocol (ARP) / IPv4:** Displays inner protocol details decoded directly from byte offset `14` onwards.

### 2. GTKWave (VCD Waveform) Packet Decoding
In `GTKWave`, inspect the 8-bit bus `a_data_in[7:0]` (or `b_data_out[7:0]`) alongside `a_valid_in` and `clk`:

```text
Clock Cycle   Signal (a_data_in)    Decoded Meaning
-------------------------------------------------------------------------
Cycle 0..5    50 54 00 12 34 56     Destination MAC (50:54:00:12:34:56)
Cycle 6..11   02 42 0a 00 00 01     Source MAC      (02:42:0a:00:00:01)
Cycle 12..13  08 06                 EtherType       (0x0806 = ARP)
Cycle 14..15  00 01                 Hardware Type   (0x0001 = Ethernet)
Cycle 16..17  08 00                 Protocol Type   (0x0800 = IPv4)
...           ...                   ...
```

---

## 🏃 How to Run

1. **Initialize Network Environment:**
   ```bash
   sudo ../scripts/setup_netns.sh
   ```

2. **Compile and Run Emulation:**
   ```bash
   verilator --cc --trace --exe --build -j 0 -Wall wrapper.cpp top.sv -o emulator
   sudo ./obj_dir/emulator
   ```

3. **Generate Traffic (In a second terminal):**
   ```bash
   ping -c 2 -I tap0 10.0.0.2
   ```

4. **Inspect Waveforms:**
   ```bash
   gtkwave waveform.vcd
   ```
