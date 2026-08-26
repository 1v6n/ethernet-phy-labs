// ============================================================================
// File: wrapper.cpp
// Description: Multi-Namespace TAP Bridge for Verilator MAC
// ============================================================================

#include <iostream>
#include <memory>
#include <csignal>
#include <vector>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include "Vtop.h"
#include "verilated.h"
#include "verilated_fst_c.h"

vluint64_t main_time = 0;
volatile sig_atomic_t stop_simulation = 0;

void handle_sigint(int sig) {
    stop_simulation = 1;
}

double sc_time_stamp() {
    return main_time;
}

// Opens TAP device handling Namespace switching via setns()
int alloc_tap_netns(const char* dev_name, const char* netns_name = nullptr) {
    int host_ns_fd = -1;

    // Switch to target namespace if specified
    if (netns_name) {
        host_ns_fd = open("/proc/self/ns/net", O_RDONLY);
        std::string ns_path = std::string("/var/run/netns/") + netns_name;
        int target_ns_fd = open(ns_path.c_str(), O_RDONLY);

        if (target_ns_fd < 0) {
            perror(("[TAP ERROR] Failed to open netns file: " + ns_path).c_str());
            if (host_ns_fd >= 0) close(host_ns_fd);
            return -1;
        }

        if (setns(target_ns_fd, CLONE_NEWNET) < 0) {
            perror("[TAP ERROR] setns switch failed");
            close(target_ns_fd);
            if (host_ns_fd >= 0) close(host_ns_fd);
            return -1;
        }
        close(target_ns_fd);
    }

    int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("[TAP ERROR] Opening /dev/net/tun failed");
    } else {
        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);

        if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
            perror("[TAP ERROR] ioctl(TUNSETIFF) failed");
            close(fd);
            fd = -1;
        }
    }

    // Switch back to original Host Namespace
    if (host_ns_fd >= 0) {
        setns(host_ns_fd, CLONE_NEWNET);
        close(host_ns_fd);
    }

    if (fd >= 0) {
        std::cout << "[TAP] Connected interface: " << dev_name 
                  << (netns_name ? (std::string(" (inside netns: ") + netns_name + ")") : " (host ns)")
                  << " -> File Descriptor: " << fd << std::endl;
    }

    return fd;
}

struct TapPort {
    int tap_fd;
    uint8_t raw_tx_buf[2048];
    ssize_t raw_tx_len = 0;
    ssize_t tx_pos = 0;
    bool    tx_active = false;

    uint8_t raw_rx_buf[2048];
    ssize_t rx_pos = 0;

    void process_tap_to_hw(uint64_t& tx_data, uint8_t& tx_keep, uint8_t& tx_valid, uint8_t& tx_last, bool tx_ready) {
        if (!tx_active) {
            raw_tx_len = read(tap_fd, raw_tx_buf, sizeof(raw_tx_buf));
            if (raw_tx_len > 0) {
                tx_active = true;
                tx_pos = 0;
            }
        }

        if (tx_active) {
            uint64_t data = 0;
            uint8_t  keep = 0;
            ssize_t rem = raw_tx_len - tx_pos;
            int bytes_in_word = (rem >= 8) ? 8 : rem;

            for (int b = 0; b < bytes_in_word; b++) {
                data |= ((uint64_t)raw_tx_buf[tx_pos + b]) << (b * 8);
                keep |= (1 << b);
            }

            tx_data  = data;
            tx_keep  = keep;
            tx_valid = 1;
            tx_last  = (rem <= 8) ? 1 : 0;

            if (tx_ready && tx_valid) {
                tx_pos += bytes_in_word;
                if (tx_pos >= raw_tx_len) {
                    tx_active = false;
                }
            }
        } else {
            tx_valid = 0;
            tx_last  = 0;
        }
    }

    void process_hw_to_tap(uint64_t rx_data, uint8_t rx_keep, uint8_t rx_valid, uint8_t rx_last) {
        if (rx_valid) {
            for (int b = 0; b < 8; b++) {
                if (rx_keep & (1 << b)) {
                    raw_rx_buf[rx_pos++] = (rx_data >> (b * 8)) & 0xFF;
                }
            }
            if (rx_last) {
                write(tap_fd, raw_rx_buf, rx_pos);
                rx_pos = 0;
            }
        }
    }
};

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_sigint);

    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    auto top = std::make_unique<Vtop>();
    auto tfp = std::make_unique<VerilatedFstC>();

    top->trace(tfp.get(), 99);
    tfp->open("waveform.fst");

    // Open tap0 in Host NS, and tap1 in ns_b
    TapPort port_a, port_b;
    port_a.tap_fd = alloc_tap_netns("tap0", nullptr);
    port_b.tap_fd = alloc_tap_netns("tap1", "ns_b");

    if (port_a.tap_fd < 0 || port_b.tap_fd < 0) {
        std::cerr << "[ERROR] Could not initialize TAP interfaces! Check script setup." << std::endl;
        return -1;
    }

    // Reset sequence
    top->clk = 0; top->rst_n = 0;
    for (int i = 0; i < 20 && !stop_simulation; i++) {
        top->clk = !top->clk;
        top->eval();
        tfp->dump(main_time);
        main_time += 5000;
    }
    top->rst_n = 1;

    std::cout << "[TB] Emulator active! Ready to bridge traffic (Host: 10.0.0.1 <-> ns_b: 10.0.0.2)..." << std::endl;

    while (!Verilated::gotFinish() && !stop_simulation) {
        top->clk = 1;

        if (top->rst_n) {
            // TAP A (Host) -> MAC A TX
            uint64_t a_d; uint8_t a_k, a_v, a_l;
            port_a.process_tap_to_hw(a_d, a_k, a_v, a_l, top->a_tx_ready);
            top->a_tx_data = a_d; top->a_tx_keep = a_k; top->a_tx_valid = a_v; top->a_tx_last = a_l;

            // MAC A RX -> TAP A
            port_a.process_hw_to_tap(top->a_rx_data, top->a_rx_keep, top->a_rx_valid, top->a_rx_last);

            // TAP B (ns_b) -> MAC B TX
            uint64_t b_d; uint8_t b_k, b_v, b_l;
            port_b.process_tap_to_hw(b_d, b_k, b_v, b_l, top->b_tx_ready);
            top->b_tx_data = b_d; top->b_tx_keep = b_k; top->b_tx_valid = b_v; top->b_tx_last = b_l;

            // MAC B RX -> TAP B
            port_b.process_hw_to_tap(top->b_rx_data, top->b_rx_keep, top->b_rx_valid, top->b_rx_last);
        }

        top->eval();
        tfp->dump(main_time);
        main_time += 5000;

        top->clk = 0;
        top->eval();
        tfp->dump(main_time);
        main_time += 5000;
    }

    tfp->close();
    top->final();
    close(port_a.tap_fd);
    close(port_b.tap_fd);
    return 0;
}
