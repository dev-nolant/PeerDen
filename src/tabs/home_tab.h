#pragma once

namespace tunngle {
namespace net {
class TunAdapter;
class CoordClient;
class PeerTunnelManager;
class ApiClient;
}
void RenderHomeTab(net::TunAdapter* tun, net::CoordClient* coord, net::PeerTunnelManager* peer_mgr,
                   net::ApiClient* api = nullptr);
}
