#pragma once

#include "net/api_client.h"
#include "net/coord_client.h"
#include "net/peer_tunnel_manager.h"
#include "net/tun_adapter.h"
#include "net/udp_tunnel.h"
#include <memory>

namespace tunngle {

class App {
public:
    App();
    void Render();

    net::TunAdapter* GetTunAdapter() { return tun_adapter_.get(); }
    net::UdpTunnel* GetUdpTunnel() { return udp_tunnel_.get(); }

    void SetPendingTunError(bool v) { pending_tun_error_ = v; }
    bool HasPendingTunError() const { return pending_tun_error_; }
    void ClearPendingTunError() { pending_tun_error_ = false; }
    bool GetAndClearPendingTunError() { bool v = pending_tun_error_; pending_tun_error_ = false; return v; }
    net::CoordClient* GetCoordClient() { return coord_client_.get(); }
    net::PeerTunnelManager* GetPeerTunnelManager() { return peer_tunnel_manager_.get(); }
    net::ApiClient* GetApiClient() { return api_client_.get(); }

private:
    std::unique_ptr<net::TunAdapter> tun_adapter_;
    std::unique_ptr<net::UdpTunnel> udp_tunnel_;
    std::unique_ptr<net::CoordClient> coord_client_;
    std::unique_ptr<net::PeerTunnelManager> peer_tunnel_manager_;
    std::unique_ptr<net::ApiClient> api_client_;
    bool pending_tun_error_ = false;
};

}
