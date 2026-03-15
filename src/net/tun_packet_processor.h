#pragma once

#include <string>

namespace tunngle {
namespace net {

class TunAdapter;
class Tunnel;
class PeerTunnelManager;

void ProcessTunPackets(TunAdapter* tun, Tunnel* tunnel, const std::string& local_ip);

void ProcessTunPackets(TunAdapter* tun, PeerTunnelManager* mgr, const std::string& local_ip);

void ProcessTunnelPackets(TunAdapter* tun, Tunnel* tunnel);

void ProcessTunnelPackets(TunAdapter* tun, PeerTunnelManager* mgr, const std::string& local_ip);

}  // namespace net
}  // namespace tunngle
