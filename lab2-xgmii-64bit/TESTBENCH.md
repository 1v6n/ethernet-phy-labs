# Hardware-in-the-Loop Testbench Architecture (Lab 2: 64-bit XGMII)

This testbench connects real Linux network stacks to a Verilated IEEE 802.3 Clause 46 64-bit XGMII (10 Gigabit Media Independent Interface) hardware datapath in real time.

```mermaid
graph TD
    subgraph Linux OS
        A[Host Network Stack<br/>tap0: 10.0.0.1]
        B[Namespace ns_b<br/>tap1: 10.0.0.2]
    end

    subgraph Verilator Simulation Engine
        W[wrapper.cpp<br/>TAP I/O & XGMII Handler]
        
        subgraph RTL top.sv
            XGMII[XGMII 64-bit Datapath<br/>Lane Alignment & Pipeline]
        end
        
        VCD[(waveform.vcd<br/>GTKWave Traces)]
    end

    %% Request Path (Host -> ns_b)
    A -->|1. Ping Request| W
    W -->|xgmii_txd / xgmii_txc| XGMII
    XGMII -->|xgmii_rxd / xgmii_rxc| W
    W -->|2. Forward Request| B

    %% Reply Path (ns_b -> Host)
    B -->|3. Ping Reply| W
    W -->|xgmii_txd / xgmii_txc| XGMII
    XGMII -->|xgmii_rxd / xgmii_rxc| W
    W -->|4. Forward Reply| A

    W -.->|Dump Traces| VCD
```

---

## Port Mapping & Datapath Interfaces

| Signal Port | Direction | Interface Width | Description |
| :--- | :--- | :--- | :--- |
| `clk` | Input | 1-bit | XGMII Transmit/Receive Clock domain (156.25 MHz equivalent) |
| `rst_n` | Input | 1-bit | Active-low synchronous reset |
| `xgmii_txd` | Input | 64-bit | Transmit Data bus (8 lanes $\times$ 8 bits) driven by C++ wrapper |
| `xgmii_txc` | Input | 8-bit | Transmit Control bus (1 control bit per data lane) |
| `xgmii_rxd` | Output | 64-bit | Receive Data bus output from RTL datapath |
| `xgmii_rxc` | Output | 8-bit | Receive Control bus output from RTL datapath |

---

## Datapath Processing Flow

1. **Software Framing (`wrapper.cpp`)**
   * Captures raw Layer-2 Ethernet frames from `tap0` or `tap1`.
   * Formats payload into standard 64-bit XGMII words, inserting Start control characters (`/S/` = `0xFB`), Preamble (`0x55`), SFD (`0xD5`), Terminate control characters (`/T/` = `0xFD`), and Idle control characters (`/I/` = `0x07`).

2. **Control Lane Mapping**
   * **Data Lanes (`txc = 0`):** Normal frame payload bytes are transmitted across 8 parallel 8-bit lanes.
   * **Control Lanes (`txc = 1`):** Flag control characters such as Start, Terminate, or Idle across individual lanes.

3. **RTL Processing (`top.sv`)**
   * Buffers, validates lane alignment, and pipes 64-bit XGMII words through the register stages.
   * Preserves alignment requirements across 8-byte boundaries.

4. **Frame Reconstruction & Delivery**
   * Monitors `xgmii_rxd` and `xgmii_rxc` outputs.
   * Strips XGMII control characters, reassembles the frame payload, and forwards packets to the destination TAP interface.

---

## Key Signals for Waveform Analysis (GTKWave)

Inspect `waveform.vcd` using GTKWave:
```bash
gtkwave waveform.vcd
```

Add these top-level signals to analyze 64-bit XGMII datapath transfers:

* **`TOP.top.clk`**: Master clock driving 64-bit transfers.
* **`TOP.top.rst_n`**: Active-low reset state.
* **`TOP.top.xgmii_txd[63:0]`**: 64-bit Transmit Data bus (Lanes 0–7).
* **`TOP.top.xgmii_txc[7:0]`**: Transmit Control flags (`8'h00` for pure payload, `8'h01` for lane 0 Start/Control).
* **`TOP.top.xgmii_rxd[63:0]`**: 64-bit Receive Data bus output.
* **`TOP.top.xgmii_rxc[7:0]`**: Receive Control flags output.
