#include "membership.hpp"

namespace p2p::membership {

MembershipManager::MembershipManager(uint16_t listen_port)
    : listen_port_(listen_port) {}

MembershipManager::~MembershipManager() = default;

void MembershipManager::start(const string& /*bootstrap_ip*/, uint16_t /*bootstrap_port*/) {}

void MembershipManager::stop() {}

vector<ActivePeer> MembershipManager::get_active_peers() const {
    return {};
}

void MembershipManager::on_peer_failed(PeerFailedCb cb) {
    failed_cbs_.push_back(move(cb));
}

void MembershipManager::on_peer_joined(PeerJoinedCb cb) {
    join_cbs_.push_back(move(cb));
}

void MembershipManager::notify_activity_by_id(uint16_t /*peer_id*/) {}

void MembershipManager::handle_control_packet(uint8_t /*msg_type*/, const uint8_t* /*payload*/,
                                              size_t /*payload_len*/, uint32_t /*src_ipv4_host*/,
                                              uint16_t /*src_port*/) {}

} // namespace p2p::membership
