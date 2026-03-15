#include "relay_peer_tunnel.h"
#include "relay_connection.h"
#include "core/logger.h"

namespace tunngle {
namespace net {

RelayPeerTunnel::RelayPeerTunnel(RelayConnection* relay, const std::string& peer_tun_ip)
    : relay_(relay), peer_tun_ip_(peer_tun_ip) {}

bool RelayPeerTunnel::IsConnected() const {
    return relay_ && relay_->IsConnected();
}

void RelayPeerTunnel::Pump(uint64_t /*now_ms*/) {
    // RelayConnection::Pump() is called once by PeerTunnelManager, not per-tunnel
}

ssize_t RelayPeerTunnel::Send(const uint8_t* data, size_t len) {
    if (!relay_ || !relay_->IsConnected()) return -1;
    return relay_->Send(peer_tun_ip_, data, len) ? static_cast<ssize_t>(len) : -1;
}

ssize_t RelayPeerTunnel::Receive(uint8_t* buf, size_t capacity) {
    if (!relay_ || !relay_->IsConnected()) return -1;
    return relay_->ReceiveFor(peer_tun_ip_, buf, capacity);
}

void RelayPeerTunnel::Disconnect() {
    // Nothing per-peer to clean up; the shared connection stays alive
}

}  // namespace net
}  // namespace tunngle
