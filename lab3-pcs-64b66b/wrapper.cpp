// ============================================================================
// Module: wrapper.cpp (Full-Duplex TAP Bridge for Dual-Channel 64b/66b PCS)
// Description: Bridges tap0 (Host) and tap1 (ns_b) using dedicated A and B
//              RTL interface ports.
// ============================================================================

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vtop.h"

int tap_alloc(const char *dev) {
    struct ifreq ifr;
    int fd, err;

    if ((fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK)) < 0) {
        perror("Opening /dev/net/tun failed");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, dev, IFNAMSIZ);

    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0) {
        perror("ioctl(TUNSETIFF) failed");
        close(fd);
        return err;
    }
    return fd;
}

int tap_alloc_ns(const char* dev, const char* netns = nullptr) {
    int orig_netns = -1;

    if (netns) {
        orig_netns = open("/proc/self/ns/net", O_RDONLY);
        std::string ns_path = std::string("/var/run/netns/") + netns;
        int ns_fd = open(ns_path.c_str(), O_RDONLY);

        if (ns_fd < 0) {
            perror(("Failed to open netns path: " + ns_path).c_str());
            if (orig_netns >= 0) close(orig_netns);
            return -1;
        }

        if (setns(ns_fd, CLONE_NEWNET) < 0) {
            perror("setns failed");
            close(ns_fd);
            if (orig_netns >= 0) close(orig_netns);
            return -1;
        }
        close(ns_fd);
    }

    int fd = tap_alloc(dev);

    if (orig_netns >= 0) {
        setns(orig_netns, CLONE_NEWNET);
        close(orig_netns);
    }

    return fd;
}

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

template <typename TxDataT, typename TxCtrlT, typename RxDataT, typename RxCtrlT, typename TickFunc>
ssize_t process_tap_frame(
    int src_fd, int dst_fd,
    TxDataT& tx_data_pin, TxCtrlT& tx_ctrl_pin,
    const RxDataT& rx_data_pin, const RxCtrlT& rx_ctrl_pin,
    TickFunc& tick, const char* dir_label
) {
    uint8_t buffer[1518];
    ssize_t nread = read(src_fd, buffer, sizeof(buffer));
    if (nread <= 0) return nread;

    std::vector<uint8_t> frame(buffer, buffer + nread);

    // Append CRC-32 (FCS)
    uint32_t fcs = calculate_crc32(frame);
    frame.push_back((fcs >> 0)  & 0xFF);
    frame.push_back((fcs >> 8)  & 0xFF);
    frame.push_back((fcs >> 16) & 0xFF);
    frame.push_back((fcs >> 24) & 0xFF);

    // Assemble MAC Stream: Preamble (7B) + SFD (1B) + Frame + FCS
    std::vector<uint8_t> mac_stream = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0xD5};
    mac_stream.insert(mac_stream.end(), frame.begin(), frame.end());

    // Pad stream to an exact multiple of 8 bytes to maintain pure DATA blocks
    while (mac_stream.size() % 8 != 0) {
        mac_stream.push_back(0x00);
    }

    std::cout << "[" << dir_label << "] Streaming " << nread << " bytes through RTL PCS Datapath...\n";

    std::vector<uint8_t> reconstructed_bytes;
    size_t byte_idx = 0;
    size_t total_stream_bytes = mac_stream.size();
    size_t max_cycles = (total_stream_bytes / 8) + 2; // +2 cycles for pipeline flush

    for (size_t cycle = 0; cycle < max_cycles; ++cycle) {
        uint64_t txd = 0;
        uint8_t  txc = 0x00;

        if (byte_idx < total_stream_bytes) {
            for (int lane = 0; lane < 8; ++lane) {
                txd |= (static_cast<uint64_t>(mac_stream[byte_idx++]) << (lane * 8));
            }
            txc = 0x00; // Pure Data Block
        } else {
            txd = 0x0707070707070707ULL; // Idle Block /I/
            txc = 0xFF;                 // Control
        }

        tx_data_pin = txd;
        tx_ctrl_pin = txc;
        tick();

        // Sample descrambled output bytes from receiving interface
        for (int lane = 0; lane < 8; ++lane) {
            uint8_t rxd_byte = (rx_data_pin >> (lane * 8)) & 0xFF;
            uint8_t rxc_bit  = (rx_ctrl_pin >> lane) & 0x1;

            if (rxc_bit == 0 && reconstructed_bytes.size() < total_stream_bytes) {
                reconstructed_bytes.push_back(rxd_byte);
            }
        }
    }

    // Deliver exact frame (stripping 8B preamble/SFD & trailing FCS/padding)
    if (reconstructed_bytes.size() >= (8 + nread)) {
        ssize_t nw = write(dst_fd, reconstructed_bytes.data() + 8, nread);
        (void)nw;
    }

    return nread;
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

    int tap0_fd = tap_alloc_ns("tap0", nullptr);
    int tap1_fd = tap_alloc_ns("tap1", "ns_b");

    if (tap0_fd < 0 || tap1_fd < 0) {
        std::cerr << "❌ Error: Run setup script with sudo before executing emulator.\n";
        return 1;
    }

    // Reset Sequence
    top->rst_n = 0;
    top->a_txd = 0x0707070707070707ULL;
    top->a_txc = 0xFF;
    top->b_txd = 0x0707070707070707ULL;
    top->b_txc = 0xFF;
    for (int i = 0; i < 10; ++i) tick();
    top->rst_n = 1;

    // FSM Warm-up cycles to establish lock on both channels
    for (int i = 0; i < 70; ++i) tick();

    std::cout << "====================================================================================\n";
    std::cout << " 🚀 Dual-Channel Hardware-in-the-Loop Active: [tap0] <---> [top.sv] <---> [tap1]\n";
    std::cout << " 📡 Execute: ping -c 2 -I tap0 10.0.0.2\n";
    std::cout << "====================================================================================\n";

    while (!Verilated::gotFinish()) {
        // Host -> ns_b: Drives A inputs (a_txd, a_txc) and reads B outputs (b_rxd, b_rxc)
        ssize_t h2n = process_tap_frame(tap0_fd, tap1_fd, top->a_txd, top->a_txc, top->b_rxd, top->b_rxc, tick, "Host -> ns_b");

        // ns_b -> Host: Drives B inputs (b_txd, b_txc) and reads A outputs (a_rxd, a_rxc)
        ssize_t n2h = process_tap_frame(tap1_fd, tap0_fd, top->b_txd, top->b_txc, top->a_rxd, top->a_rxc, tick, "ns_b -> Host");

        if (h2n <= 0 && n2h <= 0) {
            top->a_txd = 0x0707070707070707ULL;
            top->a_txc = 0xFF;
            top->b_txd = 0x0707070707070707ULL;
            top->b_txc = 0xFF;
            tick();
            usleep(1000);
        }
    }

    tfp->close();
    close(tap0_fd);
    close(tap1_fd);
    return 0;
}
