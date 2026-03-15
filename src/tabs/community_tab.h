#pragma once

namespace tunngle {
namespace net {
class TunAdapter;
class CoordClient;
class PeerTunnelManager;
class ApiClient;
}

enum class CommunitySubTab {
    Networks,
    TunngleLobby,
    Forum,
    EventPlanner,
    DownloadSkins,
};

void RenderCommunityTab(net::TunAdapter* tun, net::CoordClient* coord,
                        net::PeerTunnelManager* peer_mgr,
                        net::ApiClient* api,
                        CommunitySubTab sub_tab,
                        bool& show_create_lobby_modal,
                        bool& open_account_popup);

void RenderCreateLobbyModal(net::CoordClient* coord, net::ApiClient* api);
}
