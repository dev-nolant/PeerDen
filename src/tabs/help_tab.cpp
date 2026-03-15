#include "help_tab.h"
#include "theme.h"
#include "imgui.h"

#include <cstdlib>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

namespace tunngle {

namespace {

void OpenURL(const std::string& url) {
    if (url.empty()) return;
#ifdef _WIN32
    (void)ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    pid_t pid = fork();
    if (pid == 0) {
        execlp("open", "open", url.c_str(), (char*)nullptr);
        _exit(0);
    }
#else
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", url.c_str(), (char*)nullptr);
        _exit(0);
    }
#endif
}

void SectionHeader(const char* title) {
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

void Link(const char* label, const char* url) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 0.95f, 1.0f));
    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_None)) {
        OpenURL(url);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", url);
    ImGui::PopStyleColor();
}

}  // namespace

void RenderHelpTab() {
    float s = GetUIScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * s, 8.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, 5.0f * s));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##HelpScroll", avail, false, ImGuiWindowFlags_None);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
    float title_w = ImGui::CalcTextSize("Help").x;
    ImGui::SetCursorPosX((avail.x - title_w) * 0.5f);
    ImGui::Text("Help");
    ImGui::PopStyleColor();
    const char* subtitle = "Everything you need to get started";
    float sub_w = ImGui::CalcTextSize(subtitle).x;
    ImGui::SetCursorPosX((avail.x - sub_w) * 0.5f);
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.6f, 1.0f), "%s", subtitle);
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── What is PeerDen ──
    SectionHeader("What is PeerDen?");
    ImGui::TextWrapped(
        "PeerDen is a P2P virtual LAN application for low-latency multiplayer gaming. "
        "It creates a virtual network adapter on your machine so games think you're on a local network, "
        "even when you're playing with friends across the internet.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Think of it as a spiritual successor to Tunngle: lightweight, peer-to-peer, and built for gamers who want "
        "direct connections without heavy relay servers.");

    SectionHeader("Quick Start");
    if (ImGui::TreeNodeEx("Getting started in 4 steps", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        const float indent_w = 30.0f * s;
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "1.");
        ImGui::SameLine(indent_w);
        ImGui::TextWrapped("Connect to the coordination server (it auto-connects when you launch).");
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "2.");
        ImGui::SameLine(indent_w);
        ImGui::TextWrapped("Go to Community and browse public lobbies, or create your own (hover over Community and click Make a lobby).");
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "3.");
        ImGui::SameLine(indent_w);
        ImGui::TextWrapped("Once in a room, the TUN adapter connects automatically. Your virtual IP appears in the sidebar.");
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "4.");
        ImGui::SameLine(indent_w);
        ImGui::TextWrapped("Launch your game and use LAN / Direct Connect. Use 7.0.0.1 as the host IP (or your room host's virtual IP).");
        ImGui::Unindent();
        ImGui::TreePop();
    }

    SectionHeader("Community & Networks");
    if (ImGui::TreeNodeEx("Browsing and joining lobbies", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextWrapped("Lobbies are organized by game genre. Expand a category to see public rooms. Click a room to join.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Join by code");
        ImGui::TextWrapped("Have a 6-character code from a friend? Enter it in the Join by Code field and click Join. You'll be prompted for a password if the room is private.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Create a lobby");
        ImGui::TextWrapped("Hover over the Community tab and select Make a lobby. Pick a game (or Custom Room), set a name, visibility, and optional password. Share your lobby code so friends can join.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Lobby code");
        ImGui::TextWrapped("Every lobby gets a unique 6-character code. Share it with friends so they can join quickly without browsing the list.");
        ImGui::Unindent();
        ImGui::TreePop();
    }

    SectionHeader("TUN Adapter & Networking");
    if (ImGui::TreeNodeEx("Virtual network explained", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextWrapped(
            "The TUN adapter is a virtual network interface. When you join a room, PeerDen assigns you an IP (e.g. 7.0.0.2) "
            "and routes game traffic over encrypted UDP tunnels to other players in your room.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Host IP: 7.0.0.1");
        ImGui::TextWrapped("In most games, the room host appears as 7.0.0.1. Other players connect to that IP for LAN / Direct Connect.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Administrator required (Windows)");
        ImGui::TextWrapped("Creating a virtual network adapter needs elevated permissions. Run PeerDen as Administrator, or use the Grant Permission button when prompted.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Settings");
        ImGui::TextWrapped("Go to System to configure the TUN adapter, UDP port, nickname, and view logs.");
        ImGui::Unindent();
        ImGui::TreePop();
    }

    SectionHeader("Troubleshooting");
    if (ImGui::TreeNodeEx("Common issues", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "TUN interface failed");
        ImGui::TextWrapped("Run as Administrator (Windows) or ensure you have permission to create network interfaces. Check System > Logs for details.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Can't see other players");
        ImGui::TextWrapped("Ensure you're in the same room. Check that your firewall allows PeerDen and the game's UDP port. Try hosting if joining fails.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Server Unhealthy");
        ImGui::TextWrapped("The coordination server may be down or unreachable. Check your connection and that the server is running.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Wrong password");
        ImGui::TextWrapped("Private rooms require the correct password. Ask the host for the code and password.");
        ImGui::Unindent();
        ImGui::TreePop();
    }

    SectionHeader("Resources");
    ImGui::TextWrapped("Need more help? Join the community or get in touch:");
    ImGui::Spacing();
    Link("Forum", "https://forums.peerden.io");
    Link("Support on Ko-fi", "https://ko-fi.com/devnt");
    ImGui::Spacing();

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

}  // namespace tunngle
