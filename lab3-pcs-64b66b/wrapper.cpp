// ============================================================================
// File: wrapper.cpp
// Description: Dual TAP Verilator Engine supporting Network Namespaces (ns_b),
//              Runtime Scrambler CLI options, and clean SIGINT handling.
// ============================================================================

#include <iostream>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <string>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>

#include "Vtop.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

// Signal handling flag for Ctrl+C
volatile bool g_stop_requested = false;

void handle_sigint(int sig) {
    (void)sig;
    g_stop_requested = true;
}

class TapPort {
public:
    int tap_fd;

    // RX (RTL -> Linux TAP)
    uint8_t raw_rx_buf[2048];
    size_t rx_pos;

    // TX (Linux TAP -> RTL)
    uint8_t raw_tx_buf[2048];
    ssize_t tx_len;
    size_t tx_pos;
    bool sending;

    TapPort() : tap_fd(-1), rx_pos(0), tx_len(0), tx_pos(0), sending(false) {}

    // Initialize TAP interface in current network namespace context
    int init(const char* dev_name) {
        struct ifreq ifr;
        if ((tap_fd = open("/dev/net/tun", O_RDWR)) < 0) {
            std::cerr << "[TAP ERROR] Cannot open /dev/net/tun: " << strerror(errno) << std::endl;
            return -1;
        }

        memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);

        if (ioctl(tap_fd, TUNSETIFF, (void*)&ifr) < 0) {
            std::cerr << "[TAP ERROR] ioctl(TUNSETIFF) failed on " << dev_name << ": " << strerror(errno) << std::endl;
            close(tap_fd);
            tap_fd = -1;
            return -1;
        }

        // Set non-blocking mode
        int flags = fcntl(tap_fd, F_GETFL, 0);
        fcntl(tap_fd, F_SETFL, flags | O_NONBLOCK);

        std::cout << "[TAP] Bound successfully to interface: " << dev_name << std::endl;
        return 0;
    }

    // Initialize TAP interface within a target Linux Network Namespace (e.g., ns_b)
    int init_in_ns(const char* dev_name, const char* ns_name) {
        int old_ns_fd = open("/proc/self/ns/net", O_RDONLY);
        std::string ns_path = std::string("/var/run/netns/") + ns_name;
        int target_ns_fd = open(ns_path.c_str(), O_RDONLY);

        if (target_ns_fd < 0) {
            std::cerr << "[TAP ERROR] Target namespace file " << ns_path << " not found: " << strerror(errno) << std::endl;
            if (old_ns_fd >= 0) close(old_ns_fd);
            return -1;
        }

        // Temporarily switch process context to target network namespace
        if (setns(target_ns_fd, CLONE_NEWNET) < 0) {
            std::cerr << "[TAP ERROR] setns failed into namespace " << ns_name << ": " << strerror(errno) << std::endl;
            close(target_ns_fd);
            if (old_ns_fd >= 0) close(old_ns_fd);
            return -1;
        }
        close(target_ns_fd);

        // Open TAP descriptor inside the target namespace
        int status = init(dev_name);

        // Restore host process default network namespace context
        if (old_ns_fd >= 0) {
            setns(old_ns_fd, CLONE_NEWNET);
            close(old_ns_fd);
        }

        return status;
    }

    // Process packets coming from Hardware MAC RX out to Linux TAP
    void process_hw_to_tap(uint64_t data, uint8_t keep, uint8_t valid, uint8_t last) {
        if (!valid) return;

        for (int i = 0; i < 8; i++) {
            if (keep & (1 << i)) {
                raw_rx_buf[rx_pos++] = static_cast<uint8_t>((data >> (i * 8)) & 0xFF);
            }
        }

        if (last && rx_pos > 0) {
            // Drop runt frames under standard 14-byte Ethernet header length to avoid TAP driver EIO errors
            if (rx_pos >= 14) {
                ssize_t bytes_written = write(tap_fd, raw_rx_buf, rx_pos);
                if (bytes_written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    std::cerr << "[TAP ERROR] Write error on TAP fd " << tap_fd << ": " << strerror(errno) << std::endl;
                }
            }
            rx_pos = 0;
        }
    }

    // Ingest packets from Linux TAP into Hardware MAC TX
    void process_tap_to_hw(uint64_t &tx_data, uint8_t &tx_keep, uint8_t &tx_valid, uint8_t &tx_last, uint8_t tx_ready) {
        if (!sending) {
            tx_len = read(tap_fd, raw_tx_buf, sizeof(raw_tx_buf));
            if (tx_len > 0) {
                sending = true;
                tx_pos = 0;
            } else {
                tx_valid = 0;
                tx_data  = 0;
                tx_keep  = 0;
                tx_last  = 0;
                return;
            }
        }

        if (sending && tx_ready) {
            uint64_t d = 0;
            uint8_t k = 0;
            size_t rem = tx_len - tx_pos;
            size_t chunk = (rem >= 8) ? 8 : rem;

            for (size_t b = 0; b < chunk; b++) {
                d |= (static_cast<uint64_t>(raw_tx_buf[tx_pos + b]) << (b * 8));
                k |= (1 << b);
            }

            tx_data  = d;
            tx_keep  = k;
            tx_valid = 1;

            tx_pos += chunk;
            if (tx_pos >= static_cast<size_t>(tx_len)) {
                tx_last = 1;
                sending = false;
            } else {
                tx_last = 0;
            }
        } else if (!tx_ready) {
            tx_valid = 0;
        }
    }

    ~TapPort() {
        if (tap_fd >= 0) close(tap_fd);
    }
};

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    std::signal(SIGINT, handle_sigint);

    Vtop* top = new Vtop;
    VerilatedVcdC* tfp = new VerilatedVcdC;
    top->trace(tfp, 99);
    tfp->open("waveform.vcd");

    TapPort tap_a;
    TapPort tap_b;

    bool enable_scrambler = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--enable-scrambler") == 0 || std::strcmp(argv[i], "-s") == 0) {
            enable_scrambler = true;
        }
    }

    // Bind tap0 in default host namespace and tap1 inside ns_b namespace
    if (tap_a.init("tap0") < 0 || tap_b.init_in_ns("tap1", "ns_b") < 0) {
        std::cerr << "[EMULATOR FATAL] Failed to initialize TAP interfaces across namespaces." << std::endl;
        return 1;
    }

    top->enable_scrambler = enable_scrambler ? 1 : 0;

    std::cout << "========================================================\n"
              << "[EMULATOR] Dual TAP Node A (tap0) <-> Node B (ns_b/tap1) Active\n"
              << "[EMULATOR] Scrambler Status: " 
              << (enable_scrambler ? "ENABLED (Polynomial G(x) = x^58 + x^39 + 1)" : "DISABLED (Bypassed)") << "\n"
              << "[EMULATOR] Waveform VCD trace logging enabled (waveform.vcd)\n"
              << "[EMULATOR] Press Ctrl+C to stop simulation cleanly.\n"
              << "========================================================" << std::endl;

    uint64_t main_time = 0;

    // Reset Sequence
    top->clk = 0; top->rst_n = 0; top->eval();
    tfp->dump(main_time++);
    top->clk = 1; top->rst_n = 0; top->eval();
    tfp->dump(main_time++);
    top->clk = 0; top->rst_n = 1; top->eval();
    tfp->dump(main_time++);

    // Continuous Hardware Emulation Loop
    while (!Verilated::gotFinish() && !g_stop_requested) {
        // Phase 1: Rising edge (clk = 1) -> evaluate RTL
        top->clk = 1;
        top->enable_scrambler = enable_scrambler ? 1 : 0;
        top->eval();

        tap_a.process_hw_to_tap(top->a_rx_data, top->a_rx_keep, top->a_rx_valid, top->a_rx_last);
        tap_b.process_hw_to_tap(top->b_rx_data, top->b_rx_keep, top->b_rx_valid, top->b_rx_last);

        tap_a.process_tap_to_hw(top->a_tx_data, top->a_tx_keep, top->a_tx_valid, top->a_tx_last, top->a_tx_ready);
        tap_b.process_tap_to_hw(top->b_tx_data, top->b_tx_keep, top->b_tx_valid, top->b_tx_last, top->b_tx_ready);

        tfp->dump(main_time++);

        // Phase 2: Falling edge (clk = 0)
        top->clk = 0;
        top->eval();
        tfp->dump(main_time++);
    }

    std::cout << "\n[EMULATOR] Shutting down cleanly and closing waveform trace..." << std::endl;
    tfp->close();
    delete tfp;
    top->final();
    delete top;
    return 0;
}
