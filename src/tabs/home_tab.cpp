#include "home_tab.h"
#include "core/config.h"
#include "theme.h"
#include "net/api_client.h"
#include "net/coord_client.h"
#include "net/peer_tunnel_manager.h"
#include "net/tun_adapter.h"
#include "imgui.h"
#include <algorithm>

namespace tunngle {

namespace {

void StatusCard(const char* title, ImVec4 color, const char* detail, float width, float s) {
    ImGui::BeginChild(title, ImVec2(width, 80.0f * s), true, ImGuiWindowFlags_None);
    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "%s", title);
    ImGui::Spacing();
    ImGui::TextColored(color, "%s", detail);
    ImGui::EndChild();
}

}  // namespace

void RenderHomeTab(net::TunAdapter* tun, net::CoordClient* coord, net::PeerTunnelManager* peer_mgr,
                   net::ApiClient* api) {
    float s = GetUIScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, 5.0f * s));

    ImVec2 avail = ImGui::GetContentRegionAvail();

    ImGui::BeginChild("##HomeScroll", avail, false, ImGuiWindowFlags_None);

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
    float title_w = ImGui::CalcTextSize("PEERDEN").x;
    ImGui::SetCursorPosX((avail.x - title_w) * 0.5f);
    ImGui::Text("PEERDEN");
    ImGui::PopStyleColor();

    const char* tagline = "P2P Virtual LAN Gaming";
    float tag_w = ImGui::CalcTextSize(tagline).x;
    ImGui::SetCursorPosX((avail.x - tag_w) * 0.5f);
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", tagline);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    float card_w = (avail.x - 30.0f * s) / 3.0f;

    char server_detail[128];
    if (coord && coord->IsConnected()) {
        if (coord->IsInRoom()) {
            snprintf(server_detail, sizeof(server_detail), "In room  |  %d peer(s)\nTUN IP: %s",
                     (int)coord->GetPeers().size(), coord->GetAssignedTunIP().c_str());
        } else {
            snprintf(server_detail, sizeof(server_detail), "Healthy  |  Connected");
        }
    } else {
        snprintf(server_detail, sizeof(server_detail), "Connecting...");
    }
    ImVec4 server_col = (coord && coord->IsConnected())
        ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
        : ImVec4(0.7f, 0.55f, 0.3f, 1.0f);
    StatusCard("Coordination Server", server_col, server_detail, card_w, s);

    ImGui::SameLine(0.0f, 10.0f * s);

    char tun_detail[128];
    if (tun && tun->IsOpen()) {
        snprintf(tun_detail, sizeof(tun_detail), "Active  |  %s\n%s",
                 tun->GetInterfaceName().c_str(), tun->GetAssignedIP().c_str());
    } else if (tun) {
        snprintf(tun_detail, sizeof(tun_detail), "Ready  |  Not connected");
    } else {
        snprintf(tun_detail, sizeof(tun_detail), "Not available on this platform");
    }
    ImVec4 tun_col = (tun && tun->IsOpen())
        ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f)
        : ImVec4(0.6f, 0.6f, 0.65f, 1.0f);
    StatusCard("TUN Adapter", tun_col, tun_detail, card_w, s);

    ImGui::SameLine(0.0f, 10.0f * s);

    char profile_detail[128];
    snprintf(profile_detail, sizeof(profile_detail), "%s\nUDP Port: %u",
             Config::Instance().GetNickname().c_str(),
             Config::Instance().GetLocalPort());
    StatusCard("Profile", ImVec4(0.5f, 0.7f, 0.95f, 1.0f), profile_detail, card_w, s);

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Getting Started");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("##GettingStarted", ImVec2(0, 140.0f * s), true, ImGuiWindowFlags_None);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.82f, 1.0f));
    ImGui::TextWrapped(
        "1.  Create an account or sign in (click Account in the top-right)");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "2.  Go to the Community tab and browse public rooms, or create your own lobby");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "3.  Once in a room, peers connect automatically. Launch your game and use LAN multiplayer");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "4.  Your game will see the virtual LAN adapter. Pick LAN mode in-game and join or host as usual.");
    ImGui::PopStyleColor();
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Spacing();

    float half_w = (avail.x - 10.0f * s) * 0.5f;

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Configuration");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("##ConfigInfo", ImVec2(half_w, 130.0f * s), true, ImGuiWindowFlags_None);
    const float row_label_w = 140.0f * s;
    auto Row = [row_label_w](const char* label, const char* value) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", label);
        ImGui::SameLine(row_label_w);
        ImGui::Text("%s", value);
    };
    Row("Nickname:", Config::Instance().GetNickname().c_str());
    Row("Server:", (coord && coord->IsConnected()) ? "Healthy" : "Unhealthy");
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", Config::Instance().GetLocalPort());
    Row("UDP Port:", port_str);
    std::string tun_ip_display;
    if (tun && tun->IsOpen()) {
        tun_ip_display = tun->GetAssignedIP();
    } else if (coord && coord->IsInRoom() && !coord->GetAssignedTunIP().empty()) {
        tun_ip_display = coord->GetAssignedTunIP();
    } else {
        tun_ip_display = "—";
    }
    Row("TUN IP:", tun_ip_display.c_str());
    snprintf(port_str, sizeof(port_str), "%u", Config::Instance().GetTunUnit());
    Row("TUN Unit:", port_str);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 10.0f * s);

    ImGui::BeginChild("##About", ImVec2(half_w, 130.0f * s), true, ImGuiWindowFlags_None);
    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "About");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.82f, 1.0f), "PeerDen v1.1.0");
    ImGui::Spacing();
    ImGui::TextWrapped("P2P virtual LAN application for low-latency multiplayer gaming. "
                       "Spiritual successor to the original Tunngle.");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "ImGui + GLFW  |  C++ / Go backend");
    ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

}  // namespace tunngle
