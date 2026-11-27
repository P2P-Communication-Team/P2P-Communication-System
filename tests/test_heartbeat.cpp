#include "../src/membership/heartbeat_manager.hpp"
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;
using namespace p2p::membership;

// Minimal 16-byte HEARTBEAT frame (version=1, msg_type=6, payload_len=0)
static vector<uint8_t> make_heartbeat_frame() {
    vector<uint8_t> pkt(16, 0);
    pkt[0] = (1 << 6); // version 1, flags 0
    pkt[1] = 6;        // HEARTBEAT
    return pkt;
}

static int bind_udp(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }
    return sock;
}

int main() {
    constexpr uint16_t PORT_A = 51001;
    constexpr uint16_t PORT_B = 51002;
    constexpr uint16_t ID_A   = 101;
    constexpr uint16_t ID_B   = 202;

    pid_t child = fork();
    if (child == 0) {
        // Peer B: sends periodic heartbeats until force-killed with SIGKILL
        int sock_b = bind_udp(PORT_B);
        HeartbeatMonitor mon_b(ID_B, [sock_b](uint32_t ip, uint16_t port) {
            auto pkt = make_heartbeat_frame();
            sockaddr_in dst{};
            dst.sin_family = AF_INET;
            dst.sin_addr.s_addr = htonl(ip);
            dst.sin_port = htons(port);
            sendto(sock_b, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
            return true;
        }, 500, 3000);

        mon_b.register_peer(ID_A, INADDR_LOOPBACK, PORT_A);
        mon_b.start();
        while (true) pause();
    }

    // Peer A: monitors Peer B
    int sock_a = bind_udp(PORT_A);
    atomic<bool> recv_active{true};
    atomic<bool> file_chat_notified{false};
    atomic<uint16_t> failed_peer_id{0};

    HeartbeatMonitor mon_a(ID_A, [sock_a](uint32_t ip, uint16_t port) {
        auto pkt = make_heartbeat_frame();
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_addr.s_addr = htonl(ip);
        dst.sin_port = htons(port);
        sendto(sock_a, pkt.data(), pkt.size(), 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        return true;
    }, 500, 3000);

    mon_a.register_peer(ID_B, INADDR_LOOPBACK, PORT_B);
    mon_a.on_peer_failed([&](uint16_t id) {
        failed_peer_id.store(id);
        file_chat_notified.store(true);
        cout << "[EVENT] Peer-down callback fired for PeerID=" << id << " (File/Chat notified)\n";
    });

    thread rx([&]() {
        uint8_t buf[64];
        while (recv_active.load()) {
            sockaddr_in src{};
            socklen_t len = sizeof(src);
            ssize_t n = recvfrom(sock_a, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&src), &len);
            if (n == 16 && buf[1] == 6) {
                mon_a.record_activity_by_endpoint(ntohl(src.sin_addr.s_addr), ntohs(src.sin_port));
            }
        }
    });

    mon_a.start();
    this_thread::sleep_for(chrono::milliseconds(1200));
    assert(mon_a.get_active_peers().size() == 1);
    cout << "[OK] Active peers exchanging heartbeats normally.\n";

    // Simulate abrupt crash via SIGKILL (kill -9)
    auto t_kill = chrono::steady_clock::now();
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
    cout << "[ACTION] Sent SIGKILL (kill -9) to Peer " << ID_B << ". Waiting for timeout...\n";

    while (!file_chat_notified.load()) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - t_kill).count();

    assert(failed_peer_id.load() == ID_B);
    assert(mon_a.get_active_peers().empty());
    assert(elapsed_ms <= 5000);
    cout << "[PASS] SIGKILL detected in " << elapsed_ms << " ms (<= 5000 ms), peer removed, File/Chat notified.\n";

    mon_a.stop();
    recv_active.store(false);
    shutdown(sock_a, SHUT_RDWR);
    close(sock_a);
    rx.join();
    return 0;
}
