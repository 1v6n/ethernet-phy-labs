#include <iostream>
#include <vector>
#include <queue>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <csignal>

#include "Vtop.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

// XGMII Control Characters (IEEE 802.3ae)
static const uint8_t XGMII_IDLE     = 0x07;
static const uint8_t XGMII_START    = 0xFB;
static const uint8_t XGMII_TERM     = 0xFD;
static const uint8_t XGMII_PREAMBLE = 0x55;
static const uint8_t XGMII_SFD      = 0xD5;

static bool running = true;

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

// IEEE 802.3 CRC-32 (FCS) Standard Calculation
uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else         crc = crc >> 1;
        }
    }
    return ~crc;
}

int alloc_tap(const char *dev, const char *netns_name = nullptr) {
    int old_ns = -1;
    if (netns_name != nullptr) {
        old_ns = open("/proc/self/ns/net", O_RDONLY);
        std::string ns_path = std::string("/var/run/netns/") + netns_name;
        int new_ns = open(ns_path.c_str(), O_RDONLY);
        if (new_ns < 0 || setns(new_ns, CLONE_NEWNET) < 0) {
            perror("[ERROR] Failed to switch namespace");
            if (old_ns >= 0) close(old_ns);
            return -1;
        }
        close(new_ns);
    }

    struct ifreq ifr;
    int fd;
    if ((fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK)) < 0) {
        perror("[ERROR] Open /dev/net/tun failed");
        if (old_ns >= 0) { setns(old_ns, CLONE_NEWNET); close(old_ns); }
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (*dev) strncpy(ifr.ifr_name, dev, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        perror("[ERROR] ioctl(TUNSETIFF) failed");
        close(fd);
        if (old_ns >= 0) { setns(old_ns, CLONE_NEWNET); close(old_ns); }
        return -1;
    }

    if (old_ns >= 0) { setns(old_ns, CLONE_NEWNET); close(old_ns); }
    std::cout << "[TAP] Interface " << ifr.ifr_name 
              << " bound successfully (" << (netns_name ? netns_name : "host") << ")." << std::endl;
    return fd;
}

struct XGMIIWord {
    uint64_t data;
    uint8_t ctrl;
};

// Converts TAP frame -> Appends CRC32 FCS -> Encodes 64-bit XGMII Stream
std::queue<XGMIIWord> frame_to_xgmii(const std::vector<uint8_t>& frame) {
    std::queue<XGMIIWord> q;
    
    // 1. Calculate FCS (CRC-32)
    uint32_t fcs = calculate_crc32(frame.data(), frame.size());
    
    // Build byte buffer: [L2 Frame] + [4-byte FCS]
    std::vector<uint8_t> full_stream = frame;
    full_stream.push_back((fcs >> 0)  & 0xFF);
    full_stream.push_back((fcs >> 8)  & 0xFF);
    full_stream.push_back((fcs >> 16) & 0xFF);
    full_stream.push_back((fcs >> 24) & 0xFF);

    // 2. Start Word (/S/ + 6x Preamble + SFD)
    XGMIIWord start_word;
    start_word.data = ((uint64_t)XGMII_SFD << 56)      |
                      ((uint64_t)XGMII_PREAMBLE << 48) |
                      ((uint64_t)XGMII_PREAMBLE << 40) |
                      ((uint64_t)XGMII_PREAMBLE << 32) |
                      ((uint64_t)XGMII_PREAMBLE << 24) |
                      ((uint64_t)XGMII_PREAMBLE << 16) |
                      ((uint64_t)XGMII_PREAMBLE << 8)  |
                      ((uint64_t)XGMII_START);
    start_word.ctrl = 0x01; // Only Lane 0 is Control (/S/)
    q.push(start_word);

    // 3. Encode Frame Data + FCS + /T/ + /I/
    size_t i = 0;
    while (i <= full_stream.size()) {
        XGMIIWord w;
        w.data = 0;
        w.ctrl = 0;

        for (int lane = 0; lane < 8; lane++) {
            if (i < full_stream.size()) {
                w.data |= ((uint64_t)full_stream[i]) << (lane * 8);
                i++;
            } else if (i == full_stream.size()) {
                w.data |= ((uint64_t)XGMII_TERM) << (lane * 8); // /T/ byte
                w.ctrl |= (1 << lane);
                i++;
            } else {
                w.data |= ((uint64_t)XGMII_IDLE) << (lane * 8); // /I/ byte
                w.ctrl |= (1 << lane);
            }
        }
        q.push(w);
    }
    return q;
}

// Receiver: Validates FCS & forwards clear frame to TAP
class XGMIIReceiver {
public:
    bool in_frame = false;
    std::vector<uint8_t> rx_buf;

    void process(uint64_t data, uint8_t ctrl, int tap_fd) {
        for (int lane = 0; lane < 8; lane++) {
            uint8_t byte = (data >> (lane * 8)) & 0xFF;
            bool is_ctrl = (ctrl >> lane) & 1;

            if (is_ctrl && byte == XGMII_START && lane == 0) {
                in_frame = true;
                rx_buf.clear();
                lane = 7; // Skip preamble/SFD lanes
                continue;
            }

            if (in_frame) {
                if (is_ctrl && byte == XGMII_TERM) {
                    in_frame = false;
                    
                    // FCS Check (Minimum length: 14 MAC + 46 Payload + 4 FCS = 64B)
                    if (rx_buf.size() >= 18) {
                        size_t frame_len = rx_buf.size() - 4;
                        uint32_t rx_fcs = rx_buf[frame_len]     |
                                         (rx_buf[frame_len+1] << 8)  |
                                         (rx_buf[frame_len+2] << 16) |
                                         (rx_buf[frame_len+3] << 24);

                        uint32_t calc_fcs = calculate_crc32(rx_buf.data(), frame_len);

                        if (rx_fcs == calc_fcs) {
                            // FCS valid: Strip 4-byte FCS and forward frame to TAP
                            write(tap_fd, rx_buf.data(), frame_len);
                        } else {
                            std::cerr << "[XGMII RX ERROR] FCS mismatch! Dropping frame." << std::endl;
                        }
                    }
                    rx_buf.clear();
                    break;
                } else if (!is_ctrl) {
                    rx_buf.push_back(byte);
                }
            }
        }
    }
};

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);
    signal(SIGINT, signal_handler);

    int tap0_fd = alloc_tap("tap0", nullptr);
    int tap1_fd = alloc_tap("tap1", "ns_b");

    if (tap0_fd < 0 || tap1_fd < 0) return 1;

    Vtop *top = new Vtop;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    top->trace(tfp, 99);
    tfp->open("waveform.vcd");

    uint64_t main_time = 0;
    std::queue<XGMIIWord> tx_q_a, tx_q_b;
    XGMIIReceiver rx_a, rx_b;

    // Reset Sequence
    top->clk = 0;
    top->rst_n = 0;
    for (int i = 0; i < 10; i++) {
        top->clk = !top->clk;
        top->eval();
        tfp->dump(main_time++);
    }
    top->rst_n = 1;

    std::cout << "[EMULADOR LAB 2] Running 64-bit XGMII emulator with FCS calculation & checking..." << std::endl;

    while (running && !Verilated::gotFinish()) {
        uint8_t buf[2048];

        ssize_t n_a = read(tap0_fd, buf, sizeof(buf));
        if (n_a > 0) {
            std::vector<uint8_t> frame(buf, buf + n_a);
            auto q = frame_to_xgmii(frame);
            while(!q.empty()) { tx_q_a.push(q.front()); q.pop(); }
        }

        ssize_t n_b = read(tap1_fd, buf, sizeof(buf));
        if (n_b > 0) {
            std::vector<uint8_t> frame(buf, buf + n_b);
            auto q = frame_to_xgmii(frame);
            while(!q.empty()) { tx_q_b.push(q.front()); q.pop(); }
        }

        if (!tx_q_a.empty()) {
            top->a_txd = tx_q_a.front().data;
            top->a_txc = tx_q_a.front().ctrl;
            tx_q_a.pop();
        } else {
            top->a_txd = 0x0707070707070707ULL;
            top->a_txc = 0xFF;
        }

        if (!tx_q_b.empty()) {
            top->b_txd = tx_q_b.front().data;
            top->b_txc = tx_q_b.front().ctrl;
            tx_q_b.pop();
        } else {
            top->b_txd = 0x0707070707070707ULL;
            top->b_txc = 0xFF;
        }

        // Clock High
        top->clk = 1;
        top->eval();
        tfp->dump(main_time++);

        // Process RX
        rx_b.process(top->b_rxd, top->b_rxc, tap1_fd);
        rx_a.process(top->a_rxd, top->a_rxc, tap0_fd);

        // Clock Low
        top->clk = 0;
        top->eval();
        tfp->dump(main_time++);

        usleep(1);
    }

    tfp->close();
    top->final();
    close(tap0_fd);
    close(tap1_fd);

    delete top;
    delete tfp;
    return 0;
}
