# Lab 2 Assignment: 64-Bit XGMII Hardware-in-the-Loop Analysis

## Objective
In this assignment, you will analyze a 64-bit IEEE 802.3 Clause 46 XGMII (10 Gigabit Media Independent Interface) hardware datapath. You will generate custom ICMP traffic using Linux network tools, capture the raw packet in Wireshark, and trace how 8-lane parallel XGMII data and control characters are structured across clock cycles in GTKWave.

---

## Step-by-Step Instructions

### Step 1: Environment Setup & Traffic Generation
1. Configure your virtual network interfaces and namespace:
   ```bash
   sudo ./setup_tap.sh
   ```
2. Compile and launch the Verilator emulator:
   ```bash
   verilator --cc --trace --exe --build -j 0 -Wall -Wno-DECLFILENAME wrapper.cpp top.sv -o emulator
   sudo ./obj_dir/emulator
   ```
3. In a separate terminal, start capturing packets on `tap0`:
   ```bash
   sudo tcpdump -i tap0 -w lab2_capture.pcap
   ```
4. In a third terminal, transmit ICMP Echo Requests with the `CAFE0002` pattern:
   ```bash
   ping -c 2 -p cafe0002 -I tap0 10.0.0.2
   ```
5. Stop `tcpdump` (`Ctrl+C`) and exit the emulator.

---

### Step 2: Wireshark Packet Inspection
1. Open `lab2_capture.pcap` in Wireshark:
   ```bash
   wireshark lab2_capture.pcap
   ```
2. Select the first **ICMP Echo Request** packet.
3. Locate and highlight the following fields in the Packet Details and Hex Dump panels:
   * **Destination MAC Address**
   * **Source MAC Address**
   * **EtherType** (`0x0800` for IPv4)
   * **Custom Payload Pattern** (`CAFE0002` / `cafe0002...`)

---

### Step 3: GTKWave Waveform Inspection
1. Open the generated VCD trace:
   ```bash
   gtkwave waveform.vcd
   ```
2. Add the following signals to your wave viewer:
   * `TOP.top.clk`
   * `TOP.top.rst_n`
   * `TOP.top.xgmii_txd[63:0]`
   * `TOP.top.xgmii_txc[7:0]`
   * `TOP.top.xgmii_rxd[63:0]`
   * `TOP.top.xgmii_rxc[7:0]`
3. Locate the transition from XGMII Idle characters (`/I/` = `0x07` across all 8 lanes with `xgmii_txc = 0xFF`) to active frame transmission.
4. Perform a cycle-by-cycle trace on `xgmii_txd[63:0]` and `xgmii_txc[7:0]` to locate:
   * The Start control character (`/S/` = `0xFB` on Lane 0 with `xgmii_txc[0] = 1`).
   * The 7-byte Preamble and 1-byte SFD (`0x55555555555555D5`).
   * The custom `CAFE0002` payload bytes mapped across the parallel data lanes (`xgmii_txc = 0x00`).
   * The Terminate control character (`/T/` = `0xFD`) marking the end of the frame on `xgmii_txc`.

---

## Submission Deliverable: Slide Deck (Maximum 10 Slides)

Submit a presentation deck (maximum 10 slides) documenting your experimental setup, software packet analysis, and hardware trace inspection. 

Rather than adhering to a rigid slide-by-slide format, your slide deck should thoroughly cover the following core topics:

* **Testbench Setup & Execution Environment**: Visual proof of the active Linux network interfaces (`tap0`, `tap1`/`ns_b`), emulator compilation, and execution of the `ping` command using the `cafe0002` payload pattern.
* **Wireshark Packet Analysis**: Annotated Wireshark captures identifying key Layer 2 and Layer 3 fields (Destination/Source MAC addresses, EtherType) and highlighting the hex representation of `CAFE0002`.
* **Software-to-Hardware Correlation**: Side-by-side comparative views demonstrating the exact alignment between byte sequences in Wireshark and 64-bit word transfers on `xgmii_txd[63:0]` in GTKWave.
* **XGMII Control & Datapath Signal Tracing**: GTKWave waveform analysis with callouts explaining:
  * Idle state signaling (`/I/` = `0x07`, `xgmii_txc = 0xFF`).
  * Frame start delimiter (`/S/` = `0xFB` on Lane 0) and preamble/SFD layout across the 64-bit bus.
  * Multi-lane payload distribution across `xgmii_txd[63:0]`.
  * Frame termination (`/T/` = `0xFD`) and control bus flags (`xgmii_txc` / `xgmii_rxc`).
  * Cycle latency from `xgmii_txd` input to `xgmii_rxd` output.
