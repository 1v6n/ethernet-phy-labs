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

static bool running = true;

void signal_handler(int sig) {
    (void)sig;
    running = false;
}

// Configuración de interfaz TAP con soporte para Network Namespaces
int alloc_tap(const char *dev, const char *netns_name = nullptr) {
    int old_ns = -1;

    if (netns_name != nullptr) {
        old_ns = open("/proc/self/ns/net", O_RDONLY);
        std::string ns_path = std::string("/var/run/netns/") + netns_name;
        int new_ns = open(ns_path.c_str(), O_RDONLY);
        if (new_ns < 0 || setns(new_ns, CLONE_NEWNET) < 0) {
            perror("[ERROR] No se pudo cambiar de namespace");
            if (old_ns >= 0) close(old_ns);
            return -1;
        }
        close(new_ns);
    }

    struct ifreq ifr;
    int fd;

    if ((fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK)) < 0) {
        perror("[ERROR] No se pudo abrir /dev/net/tun");
        if (old_ns >= 0) { setns(old_ns, CLONE_NEWNET); close(old_ns); }
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI; // Capa 2 pura sin metadatos del kernel

    if (*dev) strncpy(ifr.ifr_name, dev, IFNAMSIZ - 1);

    if (ioctl(fd, TUNSETIFF, (void *)&ifr) < 0) {
        perror("[ERROR] ioctl(TUNSETIFF) fallo");
        close(fd);
        if (old_ns >= 0) { setns(old_ns, CLONE_NEWNET); close(old_ns); }
        return -1;
    }

    if (old_ns >= 0) {
        setns(old_ns, CLONE_NEWNET);
        close(old_ns);
    }

    std::cout << "[TAP] Interfaz " << ifr.ifr_name 
              << " vinculada correctamente (" << (netns_name ? netns_name : "host") << ")." << std::endl;
    return fd;
}

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

    std::queue<uint8_t> tx_queue_a, tx_queue_b;
    std::vector<uint8_t> rx_buf_a, rx_buf_b;

    // Reset inicial
    top->clk = 0;
    top->rst_n = 0;
    for (int i = 0; i < 10; i++) {
        top->clk = !top->clk;
        top->eval();
        tfp->dump(main_time++);
    }
    top->rst_n = 1;

    std::cout << "[EMULADOR LAB 1] Ejecutando simulación..." << std::endl;

    while (running && !Verilated::gotFinish()) {
        // 1. Leer tramas completas desde las interfaces TAP
        uint8_t buf[2048];
        ssize_t n_a = read(tap0_fd, buf, sizeof(buf));
        if (n_a > 0) {
            for (ssize_t i = 0; i < n_a; i++) tx_queue_a.push(buf[i]);
        }

        ssize_t n_b = read(tap1_fd, buf, sizeof(buf));
        if (n_b > 0) {
            for (ssize_t i = 0; i < n_b; i++) tx_queue_b.push(buf[i]);
        }

        // 2. Inyectar bytes al RTL (Nodo A)
        if (!tx_queue_a.empty()) {
            top->a_data_in = tx_queue_a.front();
            top->a_valid_in = 1;
            tx_queue_a.pop();
        } else {
            top->a_data_in = 0;
            top->a_valid_in = 0;
        }

        // Inyectar bytes al RTL (Nodo B)
        if (!tx_queue_b.empty()) {
            top->b_data_in = tx_queue_b.front();
            top->b_valid_in = 1;
            tx_queue_b.pop();
        } else {
            top->b_data_in = 0;
            top->b_valid_in = 0;
        }

        // 3. Evaluar flanco de subida
        top->clk = 1;
        top->eval();
        tfp->dump(main_time++);

        // 4. Recoger datos procesados por el RTL
        if (top->b_valid_out) {
            rx_buf_b.push_back(top->b_data_out);
        } else if (!rx_buf_b.empty()) {
            write(tap1_fd, rx_buf_b.data(), rx_buf_b.size());
            rx_buf_b.clear();
        }

        if (top->a_valid_out) {
            rx_buf_a.push_back(top->a_data_out);
        } else if (!rx_buf_a.empty()) {
            write(tap0_fd, rx_buf_a.data(), rx_buf_a.size());
            rx_buf_a.clear();
        }

        // 5. Evaluar flanco de bajada
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
