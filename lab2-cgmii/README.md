# Lab 2: Media Access Control (MAC) Layer (AXI-Stream <-> XGMII/CGMII)

This laboratory introduces the **Ethernet Media Access Control (MAC)** layer for 10G/100G networks as specified in **IEEE 802.3 Clause 4** and **Clause 46**. You will implement and verify a complete 64-bit Ethernet MAC pipeline in SystemVerilog that bridges user-side **AXI-Stream** packet interfaces with the **XGMII/CGMII** physical interface bus.

---

## 1. IEEE 802.3 MAC Core Concepts

The MAC layer is responsible for framing network layer packets into valid IEEE 802.3 Ethernet frames. Its key functions include:

* **Preamble & SFD Framing:** Prefixing outgoing frames with 7 bytes of Preamble (`0x55`) and 1 byte of Start-of-Frame Delimiter (`SFD = 0xD5`) to establish byte synchronization.
* **Frame Check Sequence (FCS):** Calculating and appending a 32-bit Cyclic Redundancy Check (**CRC32**) across the frame payload, and validating the FCS on receive.
* **Inter-Packet Gap (IPG) Enforcement:** Inserting idle control characters (`/I/` = `0x07`, `TXC = 1`) between frames to maintain a minimum 12-byte IPG for physical receiver alignment.

```text
+-------------------------------------------------------+
|                 AXI-Stream Interface                  |
|          (tdata[63:0], tkeep[7:0], tvalid, tlast)     |
+-------------------------------------------------------+
                            |
                            v
+-------------------------------------------------------+
|                   MAC Layer (top.sv)                  |
|  +--------------------+       +--------------------+  |
|  | MAC Transmit       |       | MAC Receive        |  |
|  | - Preamble / SFD   |       | - SFD Sync         |  |
|  | - CRC32 Append     |       | - CRC32 Check      |  |
|  | - IPG Insertion    |       | - Preamble Strip   |  |
|  +--------------------+       +--------------------+  |
+-------------------------------------------------------+
                            |
                            v
+-------------------------------------------------------+
|               XGMII / CGMII Physical Bus              |
|              (TXD[63:0], TXC[7:0] @ 156.25 MHz)       |
+-------------------------------------------------------+
```

---

## 2. Ethernet Frame Format on XGMII

Every Ethernet frame transmitted over the 64-bit XGMII bus follows the IEEE 802.3 standard structure:

| Field | Size (Bytes) | XGMII Representation (`TXD[63:0]`, `TXC[7:0]`) |
| :--- | :--- | :--- |
| **Inter-Packet Gap** | $\ge 12$ | `TXC = 8'hFF`, `TXD = 0x0707070707070707` (`/I/`) |
| **Preamble & SFD** | 8 | `TXC = 8'h01`, `TXD = 0xD5555555555555FB` (`/S/` + 6x`/P/` + `/SFD/`) |
| **Header & Payload**| 46–1500 | `TXC = 8'h00`, `TXD = Destination MAC, Source MAC, Type, Data` |
| **FCS (CRC32)** | 4 | `TXC = 8'h00`, Appended immediately after payload bytes |
| **Terminate** | 1 | `TXC = 8'h01`, `TXD = 0xFD` (`/T/`) followed by `/I/` padding |

---

## 3. System Architecture & File Hierarchy

```text
+-------------------------------------------------------+
|                   VERILATOR EMULATOR                  |
|                                                       |
|  [tap0] <--> [MAC TX A] ---> XGMII ---> [MAC RX B]    |
|                                            |          |
|  [tap1] <----------------------------------+          |
+-------------------------------------------------------+
```

### Module Files

* `mac_tx.sv`: Accepts AXI-Stream packets, inserts Preambles/SFD, appends CRC32, and enforces 12-byte IPG on `TXD`/`TXC`.
* `mac_rx.sv`: Synchronizes on SFD, validates incoming CRC32, strips framing, and outputs clean AXI-Stream packets.
* `crc32.sv`: Parallel 64-bit IEEE 802.3 CRC calculation engine ($x^{32} + x^{26} + x^{23} + \dots + 1$).
* `top.sv`: Top-level SystemVerilog wrapper connecting Node A and Node B MAC pipelines.
* `wrapper.cpp`: Verilator C++ driver bridging host Linux TAP interfaces (`tap0`, `tap1`) with SystemVerilog AXI-Stream ports.
* `setup_netns.sh`: Shell script setting up Linux virtual network interfaces and namespaces.

---

## 4. Quickstart & Execution

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y verilator build-essential iproute2 ethtool wireshark
```

### Execution Steps

1. **Configure Virtual Network Interfaces:**
   ```bash
   chmod +x scripts/setup_netns.sh
   sudo ./scripts/setup_netns.sh
   ```

2. **Compile the Hardware Model:**
   ```bash
   verilator --cc --trace-fst --exe --build --top-module top -j 0 \
     -Wall -Wno-DECLFILENAME -Wno-BLKSEQ wrapper.cpp top.sv -o emulator
   ```

3. **Run Simulation:**
   ```bash
   sudo ./obj_dir/emulator
   ```

4. **Verify Traffic (Separate Terminal):**
   ```bash
   ping -c 4 -I tap0 10.0.0.2
   ```
