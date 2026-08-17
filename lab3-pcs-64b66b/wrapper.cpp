// ============================================================================
// Module: wrapper.cpp
// Description: Verilator C++ testbench acting as a Layer-2 MAC.
//              Generates IEEE 802.3 Preamble, SFD, Frames, calculates CRC-32 (FCS),
//              and prints side-by-side Unscrambled vs Scrambled PCS blocks.
// ============================================================================

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <memory>
#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vtop.h"

// Standard IEEE 802.3 CRC-32 (FCS) Calculation
uint32_t calculate_crc32(const std::vector<uint8_t>& data) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return ~crc;
}

// Helper to extract 2-bit Sync Header (bits [1:0]) from VlWide<3> (66-bit block)
template <typename T>
uint8_t extract_sync_header(const T& block) {
    return static_cast<uint8_t>(block[0] & 0x3);
}

// Helper to extract 64-bit Payload (bits [65:2]) from VlWide<3> (66-bit block)
template <typename T>
uint64_t extract_payload(const T& block) {
    uint64_t w0 = static_cast<uint64_t>(block[0]) >> 2;               // Payload [29:0]
    uint64_t w1 = static_cast<uint64_t>(block[1]) << 30;              // Payload [61:30]
    uint64_t w2 = static_cast<uint64_t>(block[2] & 0x3) << 62;        // Payload [63:62]
    return w0 | w1 | w2;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto top = std::make_unique<Vtop>();
    auto tfp = std::make_unique<VerilatedVcdC>();

    top->trace(tfp.get(), 99);
    tfp->open("waveform.vcd");

    vluint64_t main_time = 0;
    auto tick = [&]() {
        top->clk = 0; top->eval(); tfp->dump(main_time++);
        top->clk = 1; top->eval(); tfp->dump(main_time++);
    };

    // Reset Sequence
    top->rst_n = 0;
    top->xgmii_txd = 0x0707070707070707ULL; // Idle Characters (/I/)
    top->xgmii_txc = 0xFF;                  // All Control
    for (int i = 0; i < 10; ++i) tick();
    top->rst_n = 1;

    std::cout << "====================================================================================\n";
    std::cout << " 🚀 PCS 64b/66b Encoder: Unscrambled vs. Scrambled Side-by-Side Comparison\n";
    std::cout << "====================================================================================\n";

    // Build raw Ethernet Frame payload
    std::vector<uint8_t> frame_payload = {
        0x00, 0x0A, 0x35, 0x00, 0x01, 0x02, // Dest MAC
        0x00, 0x0A, 0x35, 0x00, 0x01, 0x03, // Src MAC
        0x08, 0x00,                         // EtherType (IPv4)
        0x45, 0x00, 0x00, 0x1C, 0x00, 0x01, 0x00, 0x00, 0x40, 0x01, 0x7C, 0xCE,
        0x0A, 0x00, 0x00, 0x01, 0x0A, 0x00, 0x00, 0x02 // IP Payload
    };

    // Append CRC-32 (FCS)
    uint32_t fcs = calculate_crc32(frame_payload);
    frame_payload.push_back((fcs >> 0)  & 0xFF);
    frame_payload.push_back((fcs >> 8)  & 0xFF);
    frame_payload.push_back((fcs >> 16) & 0xFF);
    frame_payload.push_back((fcs >> 24) & 0xFF);

    // Assemble full L1 MAC Stream: Preamble (7 bytes) + SFD (1 byte) + Frame/FCS
    std::vector<uint8_t> mac_stream = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xD5};
    mac_stream.insert(mac_stream.end(), frame_payload.begin(), frame_payload.end());

    // Send Idle cycles before frame transmission
    for (int i = 0; i < 5; ++i) tick();

    // Transmit MAC Stream over 64-bit XGMII interface
    size_t byte_idx = 0;
    while (byte_idx < mac_stream.size()) {
        uint64_t txd = 0;
        uint8_t  txc = 0x00;

        for (int lane = 0; lane < 8; ++lane) {
            if (byte_idx < mac_stream.size()) {
                txd |= (static_cast<uint64_t>(mac_stream[byte_idx]) << (lane * 8));
                byte_idx++;
            } else {
                txd |= (static_cast<uint64_t>(0x07) << (lane * 8)); // Pad tail with Idle (/I/)
                txc |= (1 << lane);                                 // Control byte flag
            }
        }

        top->xgmii_txd = txd;
        top->xgmii_txc = txc;
        tick();

        // Extract Sync Header and Payload from VlWide<3> signals using helper functions
        uint8_t  sync_hdr            = extract_sync_header(top->unscrambled_tx_block);
        uint64_t unscrambled_payload = extract_payload(top->unscrambled_tx_block);
        uint64_t scrambled_payload   = extract_payload(top->scrambled_tx_block);

        std::cout << "[Cycle " << std::setw(3) << main_time/2 << "] "
                  << "Sync: 2'b" << (sync_hdr == 1 ? "01 (DATA)" : "10 (CTRL)")
                  << " | Raw: 0x" << std::hex << std::setw(16) << std::setfill('0') << unscrambled_payload
                  << " | Scrambled: 0x" << std::setw(16) << std::setfill('0') << scrambled_payload 
                  << std::dec << "\n";
    }

    // Return interface to Idle state
    top->xgmii_txd = 0x0707070707070707ULL;
    top->xgmii_txc = 0xFF;
    for (int i = 0; i < 10; ++i) tick();

    tfp->close();
    std::cout << "====================================================================================\n";
    std::cout << " ✅ Simulation complete. Waveform saved to waveform.vcd\n";
    std::cout << "====================================================================================\n";

    return 0;
}
