# Lab 3 Assignment: Full-Duplex 64b/66b PCS Hardware-in-the-Loop Analysis

## Objective
In this assignment, you will analyze an IEEE 802.3ae Clause 49 64b/66b Physical Coding Sublayer (PCS) hardware datapath. You will generate custom ICMP traffic using Linux network tools, capture the raw packet in Wireshark, and trace how 66-bit blocks, sync headers, LFSR scrambling, and receiver alignment (block lock) operate across clock cycles in GTKWave.

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
   sudo tcpdump -i tap0 -w lab3_capture.pcap
   ```
4. In a third terminal, transmit ICMP Echo Requests with the `CAFE0003` pattern:
   ```bash
   ping -c 2 -p cafe0003 -I tap0 10.0.0.2
   ```
5. Stop `tcpdump` (`Ctrl+C`) and exit the emulator.

---

### Step 2: Wireshark Packet Inspection
1. Open `lab3_capture.pcap` in Wireshark:
   ```bash
   wireshark lab3_capture.pcap
   ```
2. Select the first **ICMP Echo Request** packet.
3. Locate and highlight the following fields in the Packet Details and Hex Dump panels:
   * **Destination MAC Address**
   * **Source MAC Address**
   * **EtherType** (`0x0800` for IPv4)
   * **Custom Payload Pattern** (`CAFE0003` / `cafe0003...`)

---

### Step 3: GTKWave Waveform Inspection
1. Open the generated VCD trace:
   ```bash
   gtkwave waveform.vcd
   ```
2. Add the following key PCS signals to your wave viewer:
   * `TOP.top.clk`
   * `TOP.top.rst_n`
   * `TOP.top.a_txd[63:0]`
   * `TOP.top.a_txc[7:0]`
   * `TOP.top.a2b_unscrambled_tx_block[65:0]`
   * `TOP.top.a2b_scrambled_tx_block[65:0]`
   * `TOP.top.a2b_block_lock`
   * `TOP.top.b_rxd[63:0]`
   * `TOP.top.b_rxc[7:0]`
3. Locate the transition from Control/Idle blocks to active frame transmission.
4. Perform a cycle-by-cycle trace to observe and identify:
   * **Sync Headers**: Verify sync header transitions between Control blocks (`2'b10` for Idles) and Data blocks (`2'b01` during payload transfer) on bits `[1:0]` of `a2b_unscrambled_tx_block`.
   * **LFSR Scrambler**: Compare `a2b_unscrambled_tx_block[65:0]` against `a2b_scrambled_tx_block[65:0]` on the same clock cycle to confirm that payload bits `[65:2]` are randomized while sync header bits `[1:0]` remain un-scrambled.
   * **Sync Lock State Machine**: Verify that `a2b_block_lock` is asserted high (`1'b1`) before valid decoded data reaches `b_rxd[63:0]`.
   * **Payload Identification**: Trace the descrambled/decoded output on `b_rxd[63:0]` to locate the `CAFE0003` payload pattern delivered to `ns_b`.

---

## Submission Deliverable: Slide Deck (Maximum 10 Slides)

Submit a presentation deck (maximum 10 slides) documenting your experimental setup, software packet analysis, and hardware trace inspection. 

Rather than adhering to a rigid slide-by-slide format, your slide deck should thoroughly cover the following core topics:

* **Testbench Setup & Execution Environment**: Visual proof of the active Linux network interfaces (`tap0`, `tap1`/`ns_b`), emulator compilation, and execution of the `ping` command using the `cafe0003` payload pattern.
* **Wireshark Packet Analysis**: Annotated Wireshark captures identifying key Layer 2 and Layer 3 fields (Destination/Source MAC addresses, EtherType) and highlighting the hex representation of `CAFE0003`.
* **Software-to-Hardware Correlation**: Side-by-side comparative views demonstrating the exact alignment between byte sequences in Wireshark and decoded 64-bit XGMII words (`a_txd` and `b_rxd`) in GTKWave.
* **64b/66b PCS Layer Signal Tracing**: GTKWave waveform analysis with callouts explaining:
  * 2-bit Sync Header encoding (`2'b10` for Control/Idle vs. `2'b01` for Data).
  * Self-synchronizing LFSR scrambler action (comparing unscrambled vs. scrambled 66-bit vectors).
  * Assertion of `a2b_block_lock` by the sync header alignment state machine.
  * End-to-end pipeline latency from Host XGMII input (`a_txd`) through the PCS channel to Node B XGMII output (`b_rxd`).
