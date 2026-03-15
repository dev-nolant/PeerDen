#pragma once

#include "tunnel.h"
#include <string>

namespace tunngle {
namespace net {

class RelayConnection;

/// Per-peer tunnel that routes through a shared RelayConnection.
/// Implements the Tunnel interface so PeerTunnelManager can use it
/// interchangeably with UdpTunnel.
class RelayPeerTunnel : public Tunnel {
public:
    RelayPeerTunnel(RelayConnection* relay, const std::string& peer_tun_ip);
    ~RelayPeerTunnel() override = default;

    bool IsConnected() const override;
    bool IsConnecting() const override { return false; }
    void Pump(uint64_t now_ms) override;
    ssize_t Send(const uint8_t* data, size_t len) override;
    ssize_t Receive(uint8_t* buf, size_t capacity) override;
    void Disconnect() override;

private:
    RelayConnection* relay_;  // shared, not owned
    std::string peer_tun_ip_;
};

}  // namespace net
}  // namespace tunngle
