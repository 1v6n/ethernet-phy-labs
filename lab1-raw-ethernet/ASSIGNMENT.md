# Lab 1 Assignment: Raw 8-Bit Ethernet Hardware-in-the-Loop Analysis

## Objective
In this assignment, you will bridge higher-layer network abstractions with hardware clock cycles. You will generate custom ICMP traffic using Linux network tools, capture the raw packet at Layer 2/3, and trace those exact bytes through an 8-bit Verilated SystemVerilog datapath in GTKWave.

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
   sudo tcpdump -i tap0 -w lab1_capture.pcap
   ```
4. In a third terminal, transmit ICMP Echo Requests with the `CAFE0001` payload pattern:
   ```bash
   ping -c 2 -p cafe0001 -I tap0 10.0.0.2
   ```
5. Stop `tcpdump` (`Ctrl+C`) and exit the emulator.

---

### Step 2: Wireshark Packet Inspection
1. Open `lab1_capture.pcap` in Wireshark:
   ```bash
   wireshark lab1_capture.pcap
   ```
2. Select the first **ICMP Echo Request** packet.
3. Locate and highlight the following fields in the Packet Details and Hex Dump panels:
   * **Destination MAC Address**
   * **Source MAC Address**
   * **EtherType** (`0x0800` for IPv4)
   * **Custom Payload Pattern** (`CAFE0001` / `cafe0001...`)

---

### Step 3: GTKWave Waveform Inspection
1. Open the generated VCD trace:
   ```bash
   gtkwave waveform.vcd
   ```
2. Add the following signals to your wave viewer:
   * `TOP.top.clk`
   * `TOP.top.rst_n`
   * `TOP.top.eth_txd[7:0]`
   * `TOP.top.eth_tx_en`
   * `TOP.top.eth_rxd[7:0]`
   * `TOP.top.eth_rx_dv`
3. Locate the rising edge of `eth_tx_en` (transitioning from `0` to `1`).
4. Perform a cycle-by-cycle trace on `eth_txd[7:0]` to locate the `0xCA`, `0xFE`, `0x00`, `0x01` bytes and map the incoming byte sequence directly to your Wireshark hex capture.

---

## Submission Deliverable: Slide Deck (Maximum 10 Slides)

Submit a presentation deck (maximum 10 slides) documenting your experimental setup, software packet analysis, and hardware trace inspection. 

Rather than adhering to a rigid slide-by-slide format, your slide deck should thoroughly cover the following core topics:

* **Testbench Setup & Execution Environment**: Visual proof of the active Linux network interfaces (`tap0`, `tap1`/`ns_b`), emulator compilation, and execution of the `ping` command using the `cafe0001` payload pattern.
* **Wireshark Packet Analysis**: Annotated Wireshark captures identifying key Layer 2 and Layer 3 fields (Destination/Source MAC addresses, EtherType) and highlighting the hex representation of `CAFE0001`.
* **Software-to-Hardware Correlation**: Side-by-side comparative views demonstrating the exact alignment between byte sequences in Wireshark and cycle-by-cycle values on `eth_txd[7:0]` in GTKWave.
* **RTL Datapath & Control Signal Tracing**: GTKWave waveform analysis with callouts explaining `eth_tx_en` assertion, byte streaming behavior across `eth_txd[7:0]`, output valid assertion (`eth_rx_dv`), and cycle delay measurements through the RTL pipeline.
