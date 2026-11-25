#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace p2p::membership {

using namespace std;

enum class PeerStatus : uint8_t {
    ALIVE   = 1,
    SUSPECT = 2,
    DEAD    = 3
};

struct ActivePeer {
    uint16_t   peer_id = 0;
    uint32_t   ipv4 = 0;
    uint16_t   port = 0;
    PeerStatus status = PeerStatus::ALIVE;
    uint32_t   incarnation = 0;
    uint64_t   last_seen_ms = 0;
};

using PeerFailedCb = function<void(uint16_t peer_id)>;
using PeerJoinedCb = function<void(const ActivePeer& peer)>;

class MembershipManager {
public:
    MembershipManager(uint16_t listen_port);
    ~MembershipManager();

    void start(const string& bootstrap_ip = "", uint16_t bootstrap_port = 0);
    void stop();

    // P2P-25 Required Interface Signatures
    vector<ActivePeer> get_active_peers() const;
    void on_peer_failed(PeerFailedCb cb);
    void on_peer_joined(PeerJoinedCb cb);

    // Transport Ingress Hooks
    void notify_activity_by_id(uint16_t peer_id);
    void handle_control_packet(uint8_t msg_type, const uint8_t* payload, size_t payload_len,
                               uint32_t src_ipv4_host, uint16_t src_port);

private:
    uint16_t listen_port_;
    vector<PeerFailedCb> failed_cbs_;
    vector<PeerJoinedCb> join_cbs_;
};

} // namespace p2p::membership
