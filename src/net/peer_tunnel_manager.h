#pragma once

#include "tunnel.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tunngle {
namespace net {

class RelayConnection;

class PeerTunnelManager {
public:
    PeerTunnelManager();
    ~PeerTunnelManager();

    void SetRelayAddr(const std::string& host, uint16_t port,
                      const std::string& my_tun_ip,
                      const std::string& relay_token);

    bool AddPeer(const std::string& ip, uint16_t port, const std::string& tun_ip,
                 const std::string& auth_token,
                 uint16_t base_port = 11155);

    void RemovePeer(const std::string& tun_ip);

    void ClearAll();

    Tunnel* GetTunnelFor(const std::string& tun_ip) const;

    std::vector<Tunnel*> GetAllTunnels() const;

    std::vector<std::string> GetPeerTunIPs() const;

    void Pump(uint64_t now_ms);

private:
    void CheckRelayFallback();

    std::map<std::string, std::unique_ptr<Tunnel>> tunnels_;
    uint16_t next_local_port_ = 0;

    std::unique_ptr<RelayConnection> relay_;
    std::string relay_host_;
    uint16_t relay_port_ = 0;
    std::string my_tun_ip_;
    std::string relay_token_;
    std::map<std::string, std::string> peer_auth_tokens_;
    bool relay_available_ = false;
};

}  // namespace net
}  // namespace tunngle
