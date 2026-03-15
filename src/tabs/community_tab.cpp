#include "community_tab.h"
#include "core/config.h"
#include "core/logger.h"
#include "net/api_client.h"
#include "net/coord_client.h"
#include "net/peer_tunnel_manager.h"
#include "net/tunnel.h"
#include "net/tun_adapter.h"
#include "texture_cache.h"
#include "theme.h"
#include "imgui.h"
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace tunngle {

namespace {

static bool StringsEqualIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

static bool RoomMatchesGenre(const std::string& room_genre, const std::string& genre_name) {
    if (StringsEqualIgnoreCase(room_genre, genre_name)) return true;
    size_t pos = 0;
    while (pos < room_genre.size()) {
        size_t comma = room_genre.find(',', pos);
        if (comma == std::string::npos) comma = room_genre.size();
        size_t start = pos;
        while (start < comma && room_genre[start] == ' ') start++;
        size_t end = comma;
        while (end > start && room_genre[end - 1] == ' ') end--;
        if (StringsEqualIgnoreCase(room_genre.substr(start, end - start), genre_name)) return true;
        pos = comma + 1;
    }
    return false;
}

static bool s_chat_popped_out = false;
static std::unordered_map<std::string, size_t> s_lobby_msg_counts;
static std::unordered_map<std::string, size_t> s_lobby_last_seen;
static std::string s_pending_url_to_open;
static bool s_show_url_warning = false;

void OpenURLWithWarning(const std::string& url) {
    if (url.empty()) return;
    s_pending_url_to_open = url;
    s_show_url_warning = true;
}

void RenderUrlWarningModal() {
    if (!s_show_url_warning || s_pending_url_to_open.empty()) return;
    ImGui::OpenPopup("##UrlWarning");
    s_show_url_warning = false;
}

static bool IsUrlChar(char c) {
    return c && c != ' ' && c != '\n' && c != '\r' && c != '"' && c != '\'' && c != ')';
}

static void RenderChatTextWithLinks(const char* text, const char* id_prefix) {
    const char* p = text;
    int seg = 0;
    while (*p) {
        const char* start = p;
        if (std::strncmp(p, "https://", 8) == 0 || std::strncmp(p, "http://", 7) == 0) {
            p += (p[4] == 's') ? 8 : 7;
            while (IsUrlChar(*p)) p++;
            std::string url(start, p - start);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 0.95f, 1.0f));
            char sel_id[64];
            snprintf(sel_id, sizeof(sel_id), "%s##%d", id_prefix, seg++);
            std::string display = url.length() > 50 ? url.substr(0, 47) + "..." : url;
            if (ImGui::Selectable(display.c_str(), false, ImGuiSelectableFlags_None)) {
                OpenURLWithWarning(url);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", url.c_str());
        } else {
            const char* end = p;
            while (*end && std::strncmp(end, "http://", 7) != 0 && std::strncmp(end, "https://", 8) != 0) end++;
            if (end > p) {
                ImGui::TextWrapped("%.*s", (int)(end - p), p);
                p = end;
            } else {
                ImGui::TextWrapped("%s", p);
                break;
            }
        }
    }
}

void DoOpenURL(const std::string& url) {
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

uint64_t NowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void RenderNetworksSidebar(net::CoordClient* coord, net::PeerTunnelManager* peer_mgr) {
    float s = tunngle::GetUIScale();
    bool connected = coord && coord->IsConnected();
    bool in_room = connected && coord->IsInRoom();

    if (in_room) {
        const auto& room_info = coord->GetCurrentRoomInfo();
        if (!room_info.game_name.empty() || !room_info.voice_chat_url.empty()) {
            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Current Room");
            ImGui::Separator();
            ImGui::Spacing();
            if (!room_info.game_name.empty()) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", room_info.game_name.c_str());
                if (!room_info.game_thumbnail.empty()) {
                    TexID tex = TextureCacheGet(room_info.game_thumbnail);
                    if (tex > 0) {
                        ImGui::Image(static_cast<ImTextureID>((uintptr_t)tex), ImVec2(64.0f * s, 36.0f * s));
                    }
                }
                ImGui::Spacing();
            }
            if (!room_info.voice_chat_url.empty()) {
                if (ImGui::Button("Join Discord/Teamspeak", ImVec2(-1, 26.0f * s))) {
                    OpenURLWithWarning(room_info.voice_chat_url);
                }
                ImGui::Spacing();
            }
            ImGui::Separator();
            ImGui::Spacing();
        }

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Connection");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Server");
        ImGui::SameLine(80.0f * s);
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Healthy");

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Status");
        ImGui::SameLine(80.0f * s);
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Connected");

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Host IP");
        ImGui::SameLine(80.0f * s);
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "7.0.0.1");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Use this IP in your game's LAN/Direct Connect.\nThis is the room host's virtual network address.");
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Your IP");
        ImGui::SameLine(80.0f * s);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s",
            coord->GetAssignedTunIP().c_str());

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Peers");
        ImGui::SameLine(80.0f * s);
        ImGui::Text("%d", (int)coord->GetPeers().size());

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Lobby Code");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("Share this 6-character code so friends can join:");
        ImGui::BeginGroup();
        static bool s_lobby_code_visible = false;
        const char* display_str = room_info.code.empty() ? room_info.name.c_str() : room_info.code.c_str();
        const char* shown_str = (s_lobby_code_visible || room_info.code.empty()) ? display_str : "••••••";
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
        ImGui::Text("%s", shown_str);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 8.0f * s);
        if (ImGui::Button(s_lobby_code_visible ? "Hide##LobbyCode" : "Show##LobbyCode", ImVec2(50.0f * s, 0))) {
            s_lobby_code_visible = !s_lobby_code_visible;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(s_lobby_code_visible ? "Hide lobby code" : "Show lobby code");
        }
        ImGui::SameLine(0.0f, 8.0f * s);
        const char* copy_str = room_info.code.empty() ? room_info.name.c_str() : room_info.code.c_str();
        if (ImGui::Button("Copy##LobbyCode", ImVec2(50.0f * s, 0))) {
            ImGui::SetClipboardText(copy_str);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Copy lobby code to clipboard");
        }
        ImGui::EndGroup();

        static uint64_t room_join_time = 0;
        if (room_join_time == 0) room_join_time = NowMs();
        uint64_t elapsed_s = (NowMs() - room_join_time) / 1000;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Uptime");
        ImGui::SameLine(80.0f * s);
        if (elapsed_s < 60) {
            ImGui::Text("%lus", (unsigned long)elapsed_s);
        } else if (elapsed_s < 3600) {
            ImGui::Text("%lum %lus", (unsigned long)(elapsed_s / 60), (unsigned long)(elapsed_s % 60));
        } else {
            ImGui::Text("%luh %lum", (unsigned long)(elapsed_s / 3600), (unsigned long)((elapsed_s % 3600) / 60));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Peers");
        ImGui::Separator();
        ImGui::Spacing();

        const auto& peers = coord->GetPeers();
        if (peers.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Waiting for\nplayers...");
        } else {
            for (const auto& p : peers) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "  %s", p.nick.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Spacing();
        if (ImGui::Button("Leave Room", ImVec2(-1, 26.0f * s))) {
            peer_mgr->ClearAll();
            coord->Leave();
            coord->Disconnect();
            room_join_time = 0;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Chat");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10.0f * s);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
        if (ImGui::SmallButton(s_chat_popped_out ? "v##chatdock" : "^##chatpop")) {
            s_chat_popped_out = !s_chat_popped_out;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(s_chat_popped_out ? "Dock chat" : "Pop out chat");
        }
        ImGui::Separator();
        ImGui::Spacing();

        if (!s_chat_popped_out) {
            const auto& chat_msgs = coord->GetChatMessages();
            float chat_h = ImGui::GetContentRegionAvail().y - 36.0f * s;
            if (chat_h < 60.0f * s) chat_h = 60.0f * s;
            ImGui::BeginChild("##ChatLog", ImVec2(-1, chat_h), true, ImGuiWindowFlags_None);
            for (size_t mi = 0; mi < chat_msgs.size(); mi++) {
                const auto& m = chat_msgs[mi];
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s:", m.nick.c_str());
                ImGui::SameLine(0.0f, 4.0f * s);
                char prefix[32];
                snprintf(prefix, sizeof(prefix), "chat%zu", mi);
                RenderChatTextWithLinks(m.text.c_str(), prefix);
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            static char chat_buf[256] = "";
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##ChatInput", "Type a message...", chat_buf, sizeof(chat_buf),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (chat_buf[0]) {
                    coord->SendChat(chat_buf);
                    chat_buf[0] = '\0';
                }
                ImGui::SetKeyboardFocusHere(-1);
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Chat is in\nseparate window");
        }
    } else {
        static uint64_t room_join_time_reset = 0;
        (void)room_join_time_reset;

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Connection");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Server");
        ImGui::SameLine(80.0f * s);
        if (connected) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Healthy");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.3f, 1.0f), "Disconnected");
        }

        if (!connected) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Not connected to\ncoordination server.");
            return;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Status");
        ImGui::SameLine(80.0f * s);
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Browsing");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Nickname");
        ImGui::SameLine(80.0f * s);
        ImGui::Text("%s", Config::Instance().GetNickname().c_str());
    }
}

void RenderBrowserSidebar(net::CoordClient* coord, net::ApiClient* api, std::string& active_genre_filter,
                         net::CoordRoomInfo& pending_join_room, bool& show_password_popup,
                         bool& show_confirm_join_popup,
                         std::string& active_chat_lobby, char* lobby_search_buf, size_t lobby_search_buf_size,
                         bool& open_account_popup) {
    float s = tunngle::GetUIScale();
    static uint64_t s_last_room_list_request = 0;
    if (coord && coord->IsConnected()) {
        uint64_t now = NowMs();
        if (now - s_last_room_list_request > 3000) {
            coord->RequestRoomList();
            s_last_room_list_request = now;
        }
    }

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Join by Code");
    ImGui::Spacing();
    static char join_code_buf[8] = "";
    ImGui::SetNextItemWidth(-60.0f * s);
    ImGui::InputTextWithHint("##JoinCode", "6-char code", join_code_buf, sizeof(join_code_buf),
                             ImGuiInputTextFlags_CharsUppercase);
    ImGui::SameLine(0.0f, 4.0f * s);
    bool join_by_code = ImGui::Button("Join##ByCode", ImVec2(50.0f * s, 0));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Paste a 6-character code from a friend");
    }
    if (join_by_code && join_code_buf[0] && coord && coord->IsConnected()) {
        if (!api || !api->IsLoggedIn()) {
            open_account_popup = true;
        } else {
            std::string code(join_code_buf);
            if (code.size() == 6) {
                for (char& c : code) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
                net::CoordRoomInfo info;
                info.name = code;
                info.has_password = true;
                pending_join_room = info;
                show_password_popup = true;
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Network Search");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##Search", lobby_search_buf, lobby_search_buf_size);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Categories");
    ImGui::Spacing();

    static std::vector<net::GenreInfo> genres;
    static bool genres_loaded = false;
    if (!genres_loaded && api) {
        genres = api->GetGameGenres();
        genres_loaded = !genres.empty(); 
    }

    std::vector<net::CoordRoomInfo> rooms;
    if (coord && coord->IsConnected()) {
        rooms = coord->GetRoomList();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 14.0f);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.22f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.30f, 0.25f, 0.28f, 1.0f));

    if (genres.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Loading...");
    } else {
        const std::string& nick = Config::Instance().GetNickname();
        bool already_in_room = coord && coord->IsInRoom();
        for (size_t gi = 0; gi < genres.size(); ++gi) {
            const auto& g = genres[gi];
            std::vector<net::CoordRoomInfo> genre_rooms;
            for (const auto& r : rooms) {
                if (RoomMatchesGenre(r.genre, g.name)) {
                    genre_rooms.push_back(r);
                }
            }
            float avail = ImGui::GetContentRegionAvail().x;
            const float count_width = 50.0f;
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
            if (genre_rooms.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            bool open = ImGui::TreeNodeEx(g.name.c_str(), flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                active_genre_filter = (active_genre_filter == g.name) ? std::string() : g.name;
            }
            ImGui::SameLine(avail - count_width);
            char cnt_buf[16];
            snprintf(cnt_buf, sizeof(cnt_buf), "%zu", genre_rooms.size());
            ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "%s", cnt_buf);
            if (open) {
                for (const auto& r : genre_rooms) {
                    ImGui::Indent();
                    if (r.has_password) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
                    if (already_in_room) {
                        ImGui::Selectable(r.name.c_str(), false, ImGuiSelectableFlags_Disabled);
                    } else if (ImGui::Selectable(r.name.c_str())) {
                        if (!api || !api->IsLoggedIn()) {
                            open_account_popup = true;
                        } else {
                            pending_join_room = r;
                            if (r.has_password) {
                                show_password_popup = true;
                            } else {
                                show_confirm_join_popup = true;
                            }
                        }
                    }
                    if (r.has_password) ImGui::PopStyleColor();
                    ImGui::Unindent();
                }
                ImGui::TreePop();
            }
        }
    }

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Chat Lobbies");
    ImGui::Spacing();

    static uint64_t s_lobby_poll_time = 0;
    uint64_t now_ms = NowMs();
    static constexpr uint64_t kLobbyPollIntervalMs = 5 * 60 * 1000;

    if (api && now_ms - s_lobby_poll_time > kLobbyPollIntervalMs) {
        for (const char* lob : {"General", "LFG", "Trading"}) {
            if (active_chat_lobby == lob) continue;
            auto msgs = api->GetChatLobbyMessages(lob);
            s_lobby_msg_counts[lob] = msgs.size();
        }
        s_lobby_poll_time = now_ms;
    } else if (api && s_lobby_poll_time == 0) {
        s_lobby_poll_time = now_ms;
    }

    for (const char* lobby : {"General", "LFG", "Trading"}) {
        bool selected = (active_chat_lobby == lobby);
        size_t cur_count = s_lobby_msg_counts.count(lobby) ? s_lobby_msg_counts[lobby] : 0;
        size_t seen_count = s_lobby_last_seen.count(lobby) ? s_lobby_last_seen[lobby] : 0;
        bool has_unread = cur_count > seen_count;

        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
            s_lobby_last_seen[lobby] = cur_count;
        } else if (has_unread) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.6f, 1.0f));
        }
        if (ImGui::Selectable((std::string("  ") + lobby).c_str(), selected)) {
            active_chat_lobby = lobby;
            snprintf(lobby_search_buf, lobby_search_buf_size, "%s", lobby);
        }
        ImGui::PopStyleColor();
    }
}

void RenderChatLobbyPanel(net::ApiClient* api, const std::string& lobby_id, std::string& active_chat_lobby) {
    float s = tunngle::GetUIScale();
    static uint64_t last_fetch = 0;
    static std::string s_last_lobby;
    static std::vector<net::ChatLobbyMessage> s_messages;
    static char s_input_buf[512] = "";
    uint64_t now = NowMs();

    if (s_last_lobby != lobby_id) {
        s_last_lobby = lobby_id;
        last_fetch = 0;
        s_messages.clear();
    }
    static constexpr uint64_t kChatRefreshIntervalMs = 5 * 60 * 1000;
    if (now - last_fetch > kChatRefreshIntervalMs && api) {
        s_messages = api->GetChatLobbyMessages(lobby_id);
        s_lobby_msg_counts[lobby_id] = s_messages.size();
        s_lobby_last_seen[lobby_id] = s_messages.size();
        last_fetch = now;
    }

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "%s Lobby", lobby_id.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100.0f);
    if (ImGui::SmallButton("Back to rooms")) {
        active_chat_lobby.clear();
    }
    ImGui::Separator();
    ImGui::Spacing();

    float avail_y = ImGui::GetContentRegionAvail().y;
    float input_h = 80.0f * s;
    ImGui::BeginChild("##LobbyChatLog", ImVec2(-1, avail_y - input_h - 8.0f * s), true, ImGuiWindowFlags_None);
    for (size_t i = 0; i < s_messages.size(); i++) {
        const auto& m = s_messages[i];
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s:", m.nick.c_str());
        ImGui::SameLine(0.0f, 4.0f * s);
        char prefix[48];
        snprintf(prefix, sizeof(prefix), "lobby%zu", i);
        RenderChatTextWithLinks(m.text.c_str(), prefix);
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, 8.0f * s));
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(-60.0f * s);
    bool send = ImGui::InputTextWithHint("##LobbyInput", "Type a message... (Enter to send)",
                                         s_input_buf, sizeof(s_input_buf),
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AllowTabInput);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enter or click Send. Messages are persistent and visible to everyone.");
    }
    ImGui::SameLine(0.0f, 4.0f * s);
    bool send_btn = ImGui::Button("Send", ImVec2(56.0f * s, 0));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Enter or click Send. Messages are persistent and visible to everyone.");
    }
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    if ((send || send_btn) && s_input_buf[0] && api) {
        std::string nick = Config::Instance().GetNickname();
        if (nick.empty()) nick = "Guest";
        if (api->IsLoggedIn()) nick = api->GetDisplayName();
        if (api->PostChatLobbyMessage(lobby_id, nick, s_input_buf)) {
            s_input_buf[0] = '\0';
            last_fetch = 0;
        }
    }
}

static ImVec4 PingColor(int ping_ms) {
    if (ping_ms < 0) return ImVec4(0.5f, 0.5f, 0.55f, 1.0f);  // unknown
    if (ping_ms < 50) return ImVec4(0.4f, 0.9f, 0.5f, 1.0f);   // green
    if (ping_ms < 150) return ImVec4(0.9f, 0.75f, 0.25f, 1.0f); // yellow
    return ImVec4(0.95f, 0.5f, 0.35f, 1.0f);                   // orange
}

static ImU32 StatusDotColor(bool connected, bool connecting, int ping_ms) {
    if (!connected && !connecting) return IM_COL32(90, 90, 95, 255);   // gray
    if (connecting) return IM_COL32(230, 180, 60, 255);                // yellow
    if (ping_ms >= 0 && ping_ms >= 150) return IM_COL32(230, 180, 60, 255);  // yellow (high ping)
    return IM_COL32(80, 210, 110, 255);  // green
}

void RenderLobbyView(net::CoordClient* coord, net::PeerTunnelManager* peer_mgr) {
    float s = tunngle::GetUIScale();
    const auto& room_info = coord->GetCurrentRoomInfo();
    const std::string& my_tun_ip = coord->GetAssignedTunIP();
    const std::string& my_nick = Config::Instance().GetNickname();

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Lobby");
    ImGui::SameLine(0.0f, 12.0f * s);
    if (!room_info.game_name.empty()) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "— %s", room_info.game_name.c_str());
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
        "Players in your lobby. Connect to 7.0.0.1 in your game for LAN.");
    ImGui::Spacing();

    if (ImGui::BeginTable("##LobbyTable", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Virtual IP", ImGuiTableColumnFlags_WidthFixed, 90.0f * s);
        ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 72.0f * s);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cp = ImGui::GetCursorScreenPos();
        dl->AddCircleFilled(ImVec2(cp.x + 5.0f, cp.y + 7.0f), 4.0f, IM_COL32(80, 210, 110, 255));
        ImGui::Dummy(ImVec2(14.0f * s, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s (You)", my_nick.empty() ? "Player" : my_nick.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", my_tun_ip.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "—");

        for (const auto& p : coord->GetPeers()) {
            bool connected = false;
            bool connecting = false;
            int ping_ms = -1;
            if (peer_mgr) {
                net::Tunnel* t = peer_mgr->GetTunnelFor(p.tun_ip);
                if (t) {
                    connected = t->IsConnected();
                    connecting = t->IsConnecting();
                    ping_ms = t->GetPingMs();
                }
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            cp = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(cp.x + 5.0f, cp.y + 7.0f), 4.0f,
                StatusDotColor(connected, connecting, ping_ms));
            ImGui::Dummy(ImVec2(14.0f * s, 0.0f));
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.88f, 1.0f), "%s", p.nick.empty() ? "Player" : p.nick.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", p.tun_ip.c_str());
            ImGui::TableSetColumnIndex(2);
            if (ping_ms >= 0) {
                ImGui::TextColored(PingColor(ping_ms), "%dms", ping_ms);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "—");
            }
        }

        ImGui::EndTable();
    }
}

void RenderRoomBrowser(net::CoordClient* coord, net::ApiClient* api, std::string& active_genre_filter,
                      net::CoordRoomInfo& pending_join_room, bool& show_password_popup,
                      bool& show_confirm_join_popup, bool& open_account_popup) {
    float s = tunngle::GetUIScale();
    static uint64_t last_list_request = 0;
    static bool show_info_popup = false;
    static net::CoordRoomInfo info_room;
    static char room_search_buf[128] = "";
    static char join_password_buf[64] = "";
    uint64_t now = NowMs();

    if (now - last_list_request > 3000) {
        coord->RequestRoomList();
        last_list_request = now;
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##RoomSearch", "Search rooms by name or game...",
        room_search_buf, sizeof(room_search_buf));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Filters the room list. Case-insensitive.");
    }

    ImGui::Spacing();
    if (ImGui::Button("Refresh", ImVec2(80.0f * s, 26.0f * s))) {
        coord->RequestRoomList();
        last_list_request = now;
    }
    ImGui::SameLine(0.0f, 12.0f * s);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Auto-refreshes every 3s");
    ImGui::SameLine(0.0f, 12.0f * s);
    ImGui::SetNextItemWidth(140.0f * s);
    if (ImGui::BeginCombo("##GenreFilter", active_genre_filter.empty() ? "All genres" : active_genre_filter.c_str())) {
        if (ImGui::Selectable("All genres", active_genre_filter.empty())) active_genre_filter.clear();
        std::vector<net::CoordRoomInfo> all_rooms = coord->GetRoomList();
        std::vector<std::string> genres;
        for (const auto& r : all_rooms) {
            if (!r.genre.empty() && std::find(genres.begin(), genres.end(), r.genre) == genres.end())
                genres.push_back(r.genre);
        }
        std::sort(genres.begin(), genres.end());
        for (const auto& g : genres) {
            if (ImGui::Selectable(g.c_str(), active_genre_filter == g)) active_genre_filter = g;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, 12.0f);
    static bool hide_private = false;
    ImGui::Checkbox("Hide Private", &hide_private);
    ImGui::Spacing();

    const std::string& nick = Config::Instance().GetNickname();
    std::vector<net::CoordRoomInfo> all_rooms = coord->GetRoomList();
    std::string room_search_filter(room_search_buf);
    std::vector<net::CoordRoomInfo> rooms;
    for (const auto& r : all_rooms) {
        if (!active_genre_filter.empty() && !RoomMatchesGenre(r.genre, active_genre_filter)) continue;
        if (hide_private && r.has_password) continue;
        if (!room_search_filter.empty()) {
            std::string lower_filter = room_search_filter;
            std::string lower_name = r.name;
            std::string lower_game = r.game_name;
            for (char& c : lower_filter) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : lower_name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (char& c : lower_game) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lower_name.find(lower_filter) == std::string::npos &&
                lower_game.find(lower_filter) == std::string::npos) continue;
        }
        rooms.push_back(r);
    }
    std::sort(rooms.begin(), rooms.end(),
        [](const net::CoordRoomInfo& a, const net::CoordRoomInfo& b) {
            if (a.peers != b.peers) return a.peers > b.peers;
            return a.name < b.name;  // stable tiebreaker when peer count equal
        });

    bool logged_in = api && api->IsLoggedIn();
    const auto& favs = logged_in ? api->CachedFavorites() : std::vector<std::string>{};

    if (show_password_popup) {
        join_password_buf[0] = '\0';
        ImGui::OpenPopup("##JoinPassword");
        show_password_popup = false;
    }
    if (show_confirm_join_popup) {
        ImGui::OpenPopup("##ConfirmJoin");
        show_confirm_join_popup = false;
    }
    if (ImGui::BeginPopupModal("##ConfirmJoin", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Join room \"%s\"?", pending_join_room.name.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Join", ImVec2(80.0f * s, 0))) {
            uint16_t udp_port = Config::Instance().GetLocalPort();
            if (udp_port == 0) udp_port = 11155;
            coord->SetCurrentRoomInfo(pending_join_room);
            coord->Join(pending_join_room.name, nick, udp_port, true, pending_join_room.max_size);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f * s, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("##JoinPassword", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter password for %s", pending_join_room.name.c_str());
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Leave empty if room has no password");
        ImGui::SetNextItemWidth(220.0f * s);
        ImGui::InputText("##JoinPass", join_password_buf, sizeof(join_password_buf), ImGuiInputTextFlags_Password);
        if (ImGui::Button("Join", ImVec2(80.0f * s, 0))) {
            uint16_t udp_port = Config::Instance().GetLocalPort();
            if (udp_port == 0) udp_port = 11155;
            coord->SetCurrentRoomInfo(pending_join_room);
            if (coord->Join(pending_join_room.name, nick, udp_port, true, pending_join_room.max_size,
                           join_password_buf, {}, {}, {}, {}, {})) {
                LOG_INFO("Joined room %s", pending_join_room.name.c_str());
                join_password_buf[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f * s, 0))) {
            join_password_buf[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (rooms.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "No rooms available.");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f),
            "Create one from Community > Networks > Make a lobby.");
        ImGui::Spacing();
    }

    if (ImGui::BeginTable("##RoomTable", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Online", ImGuiTableColumnFlags_WidthFixed, 70.0f * s);
        ImGui::TableSetupColumn("Genre", ImGuiTableColumnFlags_WidthFixed, 100.0f * s);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        bool already_in_room = coord->IsInRoom();
        for (size_t ri = 0; ri < rooms.size(); ri++) {
            const auto& r = rooms[ri];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            char sel_id[256];
            snprintf(sel_id, sizeof(sel_id), "%s##row%zu", r.name.c_str(), ri);

            if (r.has_password) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.4f, 0.4f, 1.0f));
            if (already_in_room) {
                ImGui::Selectable(sel_id, false,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_Disabled);
            } else {
                if (ImGui::Selectable(sel_id, false, ImGuiSelectableFlags_SpanAllColumns)) {
                    if (!api || !api->IsLoggedIn()) {
                        open_account_popup = true;
                    } else {
                        pending_join_room = r;
                        if (r.has_password) {
                            join_password_buf[0] = '\0';
                            show_password_popup = true;
                        } else {
                            show_confirm_join_popup = true;
                        }
                    }
                }
            }
            if (r.has_password) ImGui::PopStyleColor();

            char ctx_id[64];
            snprintf(ctx_id, sizeof(ctx_id), "##ctx%zu", ri);
            if (ImGui::BeginPopupContextItem(ctx_id)) {
                ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "%s", r.name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Info")) {
                    info_room = r;
                    show_info_popup = true;
                }
                if (logged_in) {
                    bool is_fav = std::find(favs.begin(), favs.end(), r.name) != favs.end();
                    if (ImGui::MenuItem(is_fav ? "Unfavorite" : "Favorite")) {
                        if (is_fav) api->RemoveFavorite(r.name);
                        else api->AddFavorite(r.name);
                    }
                }
                if (!already_in_room) {
                    ImGui::Separator();
                    if (ImGui::MenuItem("Join Room")) {
                        if (!api || !api->IsLoggedIn()) {
                            open_account_popup = true;
                        } else {
                            pending_join_room = r;
                            if (r.has_password) {
                                join_password_buf[0] = '\0';
                                show_password_popup = true;
                            } else {
                                show_confirm_join_popup = true;
                            }
                        }
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d/%d", r.peers, r.max_size);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", r.genre.empty() ? "LAN" : r.genre.c_str());
        }
        ImGui::EndTable();
    }

    if (show_info_popup) {
        ImGui::OpenPopup("##RoomInfo");
        show_info_popup = false;
    }
    if (ImGui::BeginPopup("##RoomInfo")) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Stats for Nerds");
        ImGui::Separator();
        ImGui::Spacing();

        const float row_w = 120.0f * s;
        auto Row = [row_w](const char* label, const char* value, ImVec4 vcol = ImVec4(0.8f, 0.8f, 0.82f, 1.0f)) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%-14s", label);
            ImGui::SameLine(row_w);
            ImGui::TextColored(vcol, "%s", value);
        };

        Row("Room", info_room.name.c_str());
        char players[32];
        snprintf(players, sizeof(players), "%d / %d", info_room.peers, info_room.max_size);
        Row("Players", players);
        Row("Genre", info_room.genre.empty() ? "LAN" : info_room.genre.c_str());
        Row("Visibility", info_room.has_password ? "Private" : "Public");
        if (!info_room.game_name.empty()) Row("Game", info_room.game_name.c_str());
        if (!info_room.voice_chat_url.empty()) Row("Quick link", info_room.voice_chat_url.c_str());
        if (!info_room.game_thumbnail.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%-14s", "Thumbnail");
            ImGui::SameLine(row_w);
            TexID tex = TextureCacheGet(info_room.game_thumbnail);
            if (tex > 0) {
                ImGui::Image(static_cast<ImTextureID>((uintptr_t)tex), ImVec2(64.0f * s, 36.0f * s));
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "(loading)");
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Network");
        ImGui::Separator();
        ImGui::Spacing();

        Row("Connection", "P2P Direct");
        Row("Protocol", "UDP Tunnel");
        Row("Ping", "< 1 ms", ImVec4(0.4f, 0.9f, 0.5f, 1.0f));
        Row("Jitter", "-- ms");
        Row("Packet Loss", "-- %%");
        Row("NAT Type", "Unknown");
        Row("Encryption", "None");
        Row("Relay", "Disabled");
        Row("Server", "Healthy", ImVec4(0.4f, 0.9f, 0.5f, 1.0f));

        ImGui::Spacing();
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
}

}  // namespace

namespace {
bool IsValidVoiceChatURL(const std::string& url) {
    if (url.empty()) return false;
    std::string lower = url;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("discord.gg/") == 0 || lower.find("discord.com/invite/") == 0 ||
           lower.find("https://discord.gg/") == 0 || lower.find("https://discord.com/invite/") == 0 ||
           lower.find("teamspeak.com") != std::string::npos ||
           lower.find("ts3.") == 0 || lower.find(".ts3.") != std::string::npos;
}
}  // namespace

void RenderCreateLobbyModal(net::CoordClient* coord, net::ApiClient* api) {
    if (!coord) return;
    float s = tunngle::GetUIScale();
    ImGui::SetNextWindowSize(ImVec2(580.0f * s, 520.0f * s), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Create Lobby", nullptr, ImGuiWindowFlags_NoResize)) {
        return;
    }

    static bool modal_init = true;
    static int step = 0; // 0 = game select, 1 = room config
    static char search_buf[128] = "";
    static std::vector<net::GameInfo> search_results;
    static std::string selected_game;
    static std::string selected_genre;
    static std::string selected_game_thumbnail;

    static char room_buf[64];
    static char pass_buf[32];
    static char voice_chat_url_buf[256] = "";
    static int max_room_size = 8;
    static bool room_public = true;
    static bool voice_chat = false;
    static char create_error[128] = "";

    if (modal_init) {
        step = 0;
        search_buf[0] = '\0';
        search_results.clear();
        selected_game.clear();
        selected_genre.clear();
        selected_game_thumbnail.clear();
        room_buf[0] = '\0';
        pass_buf[0] = '\0';
        voice_chat_url_buf[0] = '\0';
        create_error[0] = '\0';
        max_room_size = 8;
        room_public = true;
        voice_chat = false;
        modal_init = false;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 6.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, 4.0f * s));
    const float label_w = 120.0f * s;
    const float field_w = -1.0f;

    if (step == 0) {

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Choose a Game");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(-1);
        bool searched = ImGui::InputTextWithHint("##GameSearch", "Search games...", search_buf, sizeof(search_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (searched || ImGui::IsItemDeactivatedAfterEdit()) {
            if (api && search_buf[0]) api->SearchGamesAsync(search_buf);
        }
        if (api && api->PollSearchResults(search_results)) {}
        ImGui::Spacing();

        const float thumb_w = 64.0f;
        const float thumb_h = 36.0f;
        const float row_h = 44.0f;
        const float text_x = thumb_w + 10.0f;

        ImGui::BeginChild("##GameList", ImVec2(-1, 340.0f * s), true, ImGuiWindowFlags_None);
        if (search_results.empty()) {
            if (api && api->IsSearchingGames()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Searching...");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Type above and press Enter to search for a game.");
            }
        }
        for (size_t i = 0; i < search_results.size(); i++) {
            const auto& g = search_results[i];
            ImGui::PushID(static_cast<int>(i));

            bool is_sel = (selected_game == g.name);
            ImVec2 cursor = ImGui::GetCursorPos();

            if (ImGui::Selectable("##sel", is_sel, ImGuiSelectableFlags_None, ImVec2(0, row_h))) {
                selected_game = g.name;
                selected_genre = g.genres.empty() ? "Unknown" : g.genres[0];
                selected_game_thumbnail = g.thumbnail;
            }

            TexID tex = TextureCacheGet(g.thumbnail);
            float thumb_y_off = (row_h - thumb_h) * 0.5f;
            ImGui::SetCursorPos(ImVec2(cursor.x + 2.0f, cursor.y + thumb_y_off));
            if (tex > 0) {
                ImGui::Image(static_cast<ImTextureID>((uintptr_t)tex), ImVec2(thumb_w, thumb_h));
            } else {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 sp = ImGui::GetCursorScreenPos();
                dl->AddRectFilled(sp, ImVec2(sp.x + thumb_w, sp.y + thumb_h),
                    IM_COL32(30, 30, 35, 255), 3.0f);
                dl->AddRect(sp, ImVec2(sp.x + thumb_w, sp.y + thumb_h),
                    IM_COL32(50, 50, 55, 255), 3.0f);
                ImGui::Dummy(ImVec2(thumb_w, thumb_h));
            }

            ImGui::SetCursorPos(ImVec2(cursor.x + text_x, cursor.y + 3.0f));
            ImGui::Text("%s", g.name.c_str());

            ImGui::SetCursorPos(ImVec2(cursor.x + text_x, cursor.y + 22.0f));
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%s", g.genre_str.c_str());

            if (!g.released.empty()) {
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "|");
                ImGui::SameLine(0.0f, 4.0f * s);
                std::string year = g.released.size() >= 4 ? g.released.substr(0, 4) : g.released;
                ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "%s", year.c_str());
            }

            if (g.rating > 0.0f) {
                ImGui::SameLine(0.0f, 8.0f);
                char rating_str[16];
                snprintf(rating_str, sizeof(rating_str), "%.1f/5", g.rating);
                ImVec4 rat_col = g.rating >= 4.0f ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f) :
                                 g.rating >= 3.0f ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                                                    ImVec4(0.9f, 0.4f, 0.35f, 1.0f);
                ImGui::TextColored(rat_col, "%s", rating_str);
            }

            if (g.metacritic > 0) {
                ImGui::SameLine(0.0f, 8.0f);
                char mc_str[16];
                snprintf(mc_str, sizeof(mc_str), "MC:%d", g.metacritic);
                ImVec4 mc_col = g.metacritic >= 75 ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f) :
                                g.metacritic >= 50 ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                                                     ImVec4(0.9f, 0.4f, 0.35f, 1.0f);
                ImGui::TextColored(mc_col, "%s", mc_str);
            }

            ImGui::SetCursorPos(ImVec2(cursor.x, cursor.y + row_h));
            ImGui::PopID();
        }
        if (search_results.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "No games found. Try a different search.");
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float btn_w = 130.0f * s;
        float total = btn_w * 3 + 24.0f * s;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - total) * 0.5f + ImGui::GetCursorStartPos().x);

        if (selected_game.empty()) ImGui::BeginDisabled();
        if (ImGui::Button("Next", ImVec2(btn_w, 32.0f * s))) {
            snprintf(room_buf, sizeof(room_buf), "%s", selected_game.c_str());
            step = 1;
        }
        if (selected_game.empty()) ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 12.0f * s);
        if (ImGui::Button("Custom Room", ImVec2(btn_w, 32.0f * s))) {
            selected_game.clear();
            selected_genre = "Custom";
            selected_game_thumbnail.clear();
            room_buf[0] = '\0';
            step = 1;
        }

        ImGui::SameLine(0.0f, 12.0f * s);
        if (ImGui::Button("Cancel##s0", ImVec2(btn_w, 32.0f * s))) {
            ImGui::CloseCurrentPopup();
            modal_init = true;
        }
    } else {
        ImGui::BeginChild("##LobbyScroll", ImVec2(-1, 380.0f * s), true, ImGuiWindowFlags_None);

        if (!selected_game.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", selected_game.c_str());
            ImGui::SameLine(0.0f, 8.0f * s);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "(%s)", selected_genre.c_str());
            ImGui::Spacing();
        }

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "General");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Room Name");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(field_w);
        if (!selected_game.empty()) ImGui::BeginDisabled();
        ImGui::InputText("##RoomName", room_buf, sizeof(room_buf));
        if (!selected_game.empty()) ImGui::EndDisabled();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Genre");
        ImGui::SameLine(label_w);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", selected_genre.c_str());

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Access");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Visibility");
        ImGui::SameLine(label_w);
        ImGui::Checkbox("Public", &room_public);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Password");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(180.0f * s);
        if (room_public) ImGui::BeginDisabled();
        ImGui::InputText("##Pass", pass_buf, sizeof(pass_buf), ImGuiInputTextFlags_Password);
        if (room_public) ImGui::EndDisabled();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Max Players");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(180.0f * s);
        ImGui::SliderInt("##Max", &max_room_size, 2, 64, "%d players");

        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Extras");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::AlignTextToFramePadding();
        ImGui::Text("Voice Chat");
        ImGui::SameLine(label_w);
        ImGui::Checkbox("##VoiceChat", &voice_chat);
        if (voice_chat) {
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::SameLine(label_w);
            ImGui::SetNextItemWidth(280.0f * s);
            ImGui::InputTextWithHint("##VoiceURL", "Discord or Teamspeak URL", voice_chat_url_buf, sizeof(voice_chat_url_buf));
            std::string vurl(voice_chat_url_buf);
            if (!vurl.empty() && !IsValidVoiceChatURL(vurl)) {
                ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "Only Discord or Teamspeak URLs allowed");
            }
        }

        ImGui::EndChild();

        ImGui::Spacing();
        if (create_error[0]) {
            ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "%s", create_error);
            ImGui::Spacing();
        }

        float btn_w = 110.0f * s;
        float total = btn_w * 3 + 24.0f * s;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - total) * 0.5f + ImGui::GetCursorStartPos().x);

        if (ImGui::Button("Back", ImVec2(btn_w, 32.0f * s))) {
            step = 0;
            create_error[0] = '\0';
        }

        ImGui::SameLine(0.0f, 12.0f * s);

        bool name_empty = (room_buf[0] == '\0');
        bool not_connected = !coord->IsConnected();
        bool already_in = coord->IsInRoom();

        bool voice_invalid = voice_chat && (!voice_chat_url_buf[0] || !IsValidVoiceChatURL(voice_chat_url_buf));
        bool create_disabled = name_empty || not_connected || already_in || voice_invalid;

        if (create_disabled) ImGui::BeginDisabled();
        if (ImGui::Button("Create Lobby", ImVec2(btn_w, 32.0f * s))) {
            uint16_t udp_port = Config::Instance().GetLocalPort();
            if (udp_port == 0) udp_port = 11155;
            const std::string& nick = Config::Instance().GetNickname();
            std::string pass(pass_buf);
            std::string voice_url(voice_chat_url_buf);
            net::CoordRoomInfo create_room_info;
            create_room_info.name = room_buf;
            create_room_info.genre = selected_genre;
            create_room_info.game_name = selected_game;
            create_room_info.game_thumbnail = selected_game_thumbnail;
            create_room_info.voice_chat_url = voice_url;
            coord->SetCurrentRoomInfo(create_room_info);
            if (coord->Join(room_buf, nick, udp_port, room_public, max_room_size,
                           pass, selected_genre, {},
                           selected_game, selected_game_thumbnail, voice_url, true)) {
                LOG_INFO("Created room %s", room_buf);
                create_error[0] = '\0';
                coord->RequestRoomList();
                ImGui::CloseCurrentPopup();
                modal_init = true;
            } else {
                snprintf(create_error, sizeof(create_error), "Failed to create lobby. Check server connection.");
            }
        }
        if (create_disabled) ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 12.0f * s);
        if (ImGui::Button("Cancel##s1", ImVec2(btn_w, 32.0f * s))) {
            ImGui::CloseCurrentPopup();
            modal_init = true;
        }

        if (already_in) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.3f, 1.0f), "Leave your current room before creating a new one.");
        } else if (not_connected) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.3f, 1.0f), "Not connected to server. Start server first.");
        }
    }

    ImGui::Spacing();
    ImGui::PopStyleVar(2);
    ImGui::EndPopup();
}

void RenderCommunityTab(net::TunAdapter* tun, net::CoordClient* coord,
                        net::PeerTunnelManager* peer_mgr,
                        net::ApiClient* api,
                        CommunitySubTab sub_tab,
                        bool& show_create_lobby_modal,
                        bool& open_account_popup) {
    if (!coord || !peer_mgr) return;
    float s = tunngle::GetUIScale();

    if (sub_tab != CommunitySubTab::Networks) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
            "Coming soon: %s",
            sub_tab == CommunitySubTab::TunngleLobby ? "PeerDen Lobby" :
            sub_tab == CommunitySubTab::Forum ? "Forum" :
            sub_tab == CommunitySubTab::EventPlanner ? "Event Planner" : "Download Skins");
        return;
    }

    static int sidebar_sub_tab = 1; // 1 = Browser, 0 = Networks
    static bool s_was_connected = false;
    static bool s_was_in_room = false;
    static bool s_show_browse_when_in_room = false;  // false = Your Lobby, true = Browse Rooms
    bool connected = coord->IsConnected();
    bool in_room = connected && coord->IsInRoom();

    if (connected && !s_was_connected) {
        sidebar_sub_tab = 0;
        s_was_connected = true;
    } else if (!connected) {
        s_was_connected = false;
    }
    bool just_joined = in_room && !s_was_in_room;
    if (just_joined) {
        sidebar_sub_tab = 0;  // Switch to Networks tab when joining a lobby
        s_show_browse_when_in_room = false;  // Reset to lobby view when joining
    }
    s_was_in_room = in_room;

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float sidebar_w = 200.0f * s;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 6.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * s, 5.0f * s));

    ImGui::BeginChild("##CommunitySidebar", ImVec2(sidebar_w, avail.y), true, ImGuiWindowFlags_None);

    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 0.0f);
        if (ImGui::BeginTabBar("##SidebarTabs", ImGuiTabBarFlags_None)) {
            ImGuiTabItemFlags networks_flags = just_joined ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            if (ImGui::BeginTabItem("Browser")) {
                sidebar_sub_tab = 1;
                ImGui::EndTabItem();
            }
            if (!connected) ImGui::BeginDisabled();
            if (ImGui::BeginTabItem("Networks", nullptr, networks_flags)) {
                sidebar_sub_tab = 0;
                ImGui::EndTabItem();
            }
            if (!connected) ImGui::EndDisabled();
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar(2);
    }

    ImGui::Spacing();

    static std::string s_active_genre_filter;
    static net::CoordRoomInfo s_pending_join_room;
    static bool s_show_password_popup = false;
    static bool s_show_confirm_join_popup = false;
    static std::string s_active_chat_lobby;
    static char s_lobby_search_buf[128] = "";
    if (sidebar_sub_tab == 0 && connected) {
        RenderNetworksSidebar(coord, peer_mgr);
    } else {
        RenderBrowserSidebar(coord, api, s_active_genre_filter, s_pending_join_room,
                            s_show_password_popup, s_show_confirm_join_popup,
                            s_active_chat_lobby, s_lobby_search_buf, sizeof(s_lobby_search_buf),
                            open_account_popup);
    }

    ImGui::EndChild();

    ImGui::SameLine(0.0f, 8.0f);

    ImGui::BeginChild("##CommunityMain", ImVec2(avail.x - sidebar_w - 8.0f * s, avail.y), true, ImGuiWindowFlags_None);

    if (connected) {
        std::string coord_err = coord->GetLastError();
        if (!coord_err.empty()) {
            if (coord_err.find("login required") != std::string::npos) {
                open_account_popup = true;
                coord->ClearLastError();
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("Server error: %s", coord_err.c_str());
                ImGui::PopStyleColor();
                if (ImGui::Button("Dismiss##CoordErr")) {
                    coord->ClearLastError();
                }
                ImGui::Separator();
                ImGui::Spacing();
            }
        }

        if (in_room) {
            ImGui::AlignTextToFramePadding();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 cp = ImGui::GetCursorScreenPos();
            dl->AddCircleFilled(ImVec2(cp.x + 5.0f, cp.y + 7.0f), 4.0f, IM_COL32(80, 210, 110, 255));
            ImGui::Dummy(ImVec2(14.0f * s, 0.0f));
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.6f, 1.0f), "In room  |  %d peer(s)",
                (int)coord->GetPeers().size());
            float avail_x = ImGui::GetContentRegionAvail().x;
            float btn_w = 110.0f;
            float seg_x = avail_x - btn_w * 2 - 8.0f;
            ImGui::SameLine(seg_x);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            bool in_lobby = !s_show_browse_when_in_room;
            if (in_lobby) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.52f, 0.32f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.40f, 0.24f, 1.0f));
            }
            if (ImGui::Button("Your Lobby", ImVec2(btn_w, 0))) {
                s_show_browse_when_in_room = false;
            }
            if (in_lobby) ImGui::PopStyleColor(3);
            ImGui::SameLine(0.0f, 2.0f);
            bool browsing = s_show_browse_when_in_room;
            if (browsing) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.45f, 0.28f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.52f, 0.32f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.40f, 0.24f, 1.0f));
            }
            if (ImGui::Button("Browse Rooms", ImVec2(btn_w, 0))) {
                s_show_browse_when_in_room = true;
            }
            if (browsing) ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGui::Separator();
            ImGui::Spacing();
        }

        if (!s_active_chat_lobby.empty()) {
            RenderChatLobbyPanel(api, s_active_chat_lobby, s_active_chat_lobby);
        } else if (in_room && !s_show_browse_when_in_room) {
            RenderLobbyView(coord, peer_mgr);
        } else {
            RenderRoomBrowser(coord, api, s_active_genre_filter, s_pending_join_room,
                             s_show_password_popup, s_show_confirm_join_popup, open_account_popup);
        }
    } else {
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.35f, 1.0f), "Not connected to server");
        std::string coord_err = coord->GetLastError();
        if (!coord_err.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", coord_err.c_str());
            if (ImGui::Button("Dismiss##CoordErrDisconnected")) {
                coord->ClearLastError();
            }
        }
        ImGui::Spacing();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    if (s_chat_popped_out && connected && coord->IsInRoom()) {
        ImGui::SetNextWindowSize(ImVec2(360.0f * s, 320.0f * s), ImGuiCond_FirstUseEver);
        bool chat_open = true;
        if (ImGui::Begin("Room Chat", &chat_open, ImGuiWindowFlags_None)) {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));

            const auto& chat_msgs = coord->GetChatMessages();
            float input_h = ImGui::GetFrameHeightWithSpacing() + 4.0f;
            ImGui::BeginChild("##PopChatLog", ImVec2(0, -input_h), true, ImGuiWindowFlags_None);
            for (size_t mi = 0; mi < chat_msgs.size(); mi++) {
                const auto& m = chat_msgs[mi];
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s:", m.nick.c_str());
                ImGui::SameLine(0.0f, 4.0f * s);
                char prefix[32];
                snprintf(prefix, sizeof(prefix), "popchat%zu", mi);
                RenderChatTextWithLinks(m.text.c_str(), prefix);
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 2.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();

            static char pop_chat_buf[256] = "";
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##PopChatInput", "Type a message...", pop_chat_buf, sizeof(pop_chat_buf),
                    ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (pop_chat_buf[0]) {
                    coord->SendChat(pop_chat_buf);
                    pop_chat_buf[0] = '\0';
                }
                ImGui::SetKeyboardFocusHere(-1);
            }

            ImGui::PopStyleVar();
        }
        ImGui::End();
        if (!chat_open) {
            s_chat_popped_out = false;
        }
    }

    if (show_create_lobby_modal) {
        ImGui::OpenPopup("Create Lobby");
        show_create_lobby_modal = false;
    }
    RenderCreateLobbyModal(coord, api);

    RenderUrlWarningModal();
    if (ImGui::BeginPopupModal("##UrlWarning", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.4f, 1.0f), "Warning: External Link");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "You are about to open a link shared by another user. "
            "Only open links from people you trust. Malicious links can steal your data or harm your device.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", s_pending_url_to_open.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Open", ImVec2(80.0f * s, 0))) {
            DoOpenURL(s_pending_url_to_open);
            s_pending_url_to_open.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80.0f * s, 0))) {
            s_pending_url_to_open.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}  // namespace tunngle
