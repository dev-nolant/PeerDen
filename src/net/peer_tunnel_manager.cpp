#include "peer_tunnel_manager.h"
#include "relay_connection.h"
#include "relay_peer_tunnel.h"
#include "udp_tunnel.h"
#include "core/logger.h"
#include <algorithm>

namespace tunngle {
namespace net {

PeerTunnelManager::PeerTunnelManager() = default;

PeerTunnelManager::~PeerTunnelManager() = default;

void PeerTunnelManager::SetRelayAddr(const std::string& host, uint16_t port,
                                      const std::string& my_tun_ip,
                                      const std::string& relay_token) {
    bool changed = (relay_host_ != host || relay_port_ != port || my_tun_ip_ != my_tun_ip || relay_token_ != relay_token);
    relay_host_ = host;
    relay_port_ = port;
    my_tun_ip_ = my_tun_ip;
    relay_token_ = relay_token;
    relay_available_ = !host.empty() && port != 0;
    if (relay_available_ && changed) {
        LOG_INFO("PeerTunnelManager: relay fallback available");
    }
}

bool PeerTunnelManager::AddPeer(const std::string& ip, uint16_t port, const std::string& tun_ip,
                                const std::string& auth_token,
                                uint16_t base_port) {
    if (tun_ip.empty() || tunnels_.count(tun_ip)) return false;

    uint16_t local_port = base_port + next_local_port_;
    next_local_port_++;

    peer_auth_tokens_[tun_ip] = auth_token;
    if (relay_) {
        relay_->SetPeerToken(tun_ip, auth_token);
    }

    auto tunnel = std::make_unique<UdpTunnel>();
    if (!tunnel->Connect(ip, port, local_port, auth_token)) {
        LOG_ERROR("PeerTunnelManager: failed to connect to peer %s", tun_ip.c_str());
        next_local_port_--;
        return false;
    }
    tunnels_[tun_ip] = std::move(tunnel);
    LOG_INFO("PeerTunnelManager: added peer %s (P2P)", tun_ip.c_str());
    return true;
}

void PeerTunnelManager::RemovePeer(const std::string& tun_ip) {
    auto it = tunnels_.find(tun_ip);
    if (it != tunnels_.end()) {
        tunnels_.erase(it);
        peer_auth_tokens_.erase(tun_ip);
        LOG_INFO("PeerTunnelManager: removed peer %s", tun_ip.c_str());
    }
}

void PeerTunnelManager::ClearAll() {
    if (!tunnels_.empty() || relay_) {
        tunnels_.clear();
        peer_auth_tokens_.clear();
        relay_.reset();
        next_local_port_ = 0;
        LOG_INFO("PeerTunnelManager: cleared (left room)");
    }
}

Tunnel* PeerTunnelManager::GetTunnelFor(const std::string& tun_ip) const {
    auto it = tunnels_.find(tun_ip);
    return it != tunnels_.end() ? it->second.get() : nullptr;
}

std::vector<Tunnel*> PeerTunnelManager::GetAllTunnels() const {
    std::vector<Tunnel*> out;
    for (const auto& [_, t] : tunnels_) {
        out.push_back(t.get());
    }
    return out;
}

std::vector<std::string> PeerTunnelManager::GetPeerTunIPs() const {
    std::vector<std::string> out;
    for (const auto& [tun_ip, _] : tunnels_) {
        out.push_back(tun_ip);
    }
    return out;
}

void PeerTunnelManager::Pump(uint64_t now_ms) {
    for (const auto& [_, t] : tunnels_) {
        t->Pump(now_ms);
    }

    if (relay_) {
        relay_->Pump(now_ms);
    }

    CheckRelayFallback();
}

void PeerTunnelManager::CheckRelayFallback() {
    if (!relay_available_) return;

    std::vector<std::string> to_relay;
    for (const auto& [tun_ip, t] : tunnels_) {
        if (t->TimedOut()) {
            to_relay.push_back(tun_ip);
        }
    }
    if (to_relay.empty()) return;

    if (!relay_) {
        relay_ = std::make_unique<RelayConnection>();
        if (!relay_->Connect(relay_host_, relay_port_, my_tun_ip_, relay_token_)) {
            LOG_ERROR("PeerTunnelManager: relay connection failed, P2P-only mode");
            relay_.reset();
            relay_available_ = false;
            return;
        }
        for (const auto& it : peer_auth_tokens_) {
            relay_->SetPeerToken(it.first, it.second);
        }
    }

    for (const auto& tun_ip : to_relay) {
        LOG_INFO("PeerTunnelManager: P2P failed for %s, falling back to relay", tun_ip.c_str());
        tunnels_[tun_ip] = std::make_unique<RelayPeerTunnel>(relay_.get(), tun_ip);
    }
}

}  // namespace net
}  // namespace tunngle
