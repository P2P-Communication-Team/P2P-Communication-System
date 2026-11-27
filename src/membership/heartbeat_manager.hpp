#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace p2p::membership {

using namespace std;

enum class PeerStatus : uint8_t {
    ALIVE = 1,
    DEAD  = 2
};

struct PeerHealth {
    uint16_t   peer_id = 0;
    uint32_t   ipv4 = 0;          // HOST byte order
    uint16_t   port = 0;          // HOST byte order
    PeerStatus status = PeerStatus::ALIVE;
    uint64_t   last_seen_ms = 0;
    uint64_t   last_tx_ms = 0;
};

class HeartbeatMonitor {
public:
    using SendHeartbeatFn = function<bool(uint32_t dst_ipv4_host, uint16_t dst_port)>;
    using PeerDownCb      = function<void(uint16_t peer_id)>;

    HeartbeatMonitor(uint16_t local_peer_id,
                     SendHeartbeatFn send_fn,
                     uint64_t heartbeat_interval_ms = 1000,
                     uint64_t dead_timeout_ms = 3500);
    ~HeartbeatMonitor();

    // Register File/Chat module callbacks for peer-down events
    void on_peer_failed(PeerDownCb cb);

    // Track/update peers and record incoming packet activity
    void register_peer(uint16_t peer_id, uint32_t ipv4_host, uint16_t port);
    void record_activity(uint16_t peer_id);
    void record_activity_by_endpoint(uint32_t src_ipv4_host, uint16_t src_port);

    // Query currently alive peers (crashed/dead peers are automatically excluded)
    vector<PeerHealth> get_active_peers() const;

    void start();
    void stop();

private:
    uint16_t local_peer_id_;
    SendHeartbeatFn send_fn_;
    uint64_t heartbeat_interval_ms_;
    uint64_t dead_timeout_ms_;
    static constexpr uint64_t TICK_MS = 250;

    atomic<bool> running_{false};
    mutable shared_mutex mutex_;
    map<uint16_t, PeerHealth> peers_;
    vector<PeerDownCb> peer_down_cbs_;

    thread scheduler_thread_;

    static uint64_t now_ms();
    void scheduler_loop();
};

} // namespace p2p::membership
