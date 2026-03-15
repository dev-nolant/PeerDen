#include "app.h"
#include "core/config.h"
#include "core/version.h"
#include "imgui.h"
#if defined(_WIN32) && defined(CPPHTTPLIB_OPENSSL_SUPPORT)
#include "updater.h"
#endif
#include "texture_cache.h"
#include "theme.h"
#include "tabs/home_tab.h"
#include "tabs/community_tab.h"
#include "tabs/system_tab.h"
#include "tabs/help_tab.h"

#include <cstdlib>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

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
}  // namespace

namespace tunngle {

App::App() {
    tun_adapter_ = net::TunAdapter::Create();
    udp_tunnel_ = std::make_unique<net::UdpTunnel>();
    coord_client_ = std::make_unique<net::CoordClient>();
    peer_tunnel_manager_ = std::make_unique<net::PeerTunnelManager>();
    api_client_ = std::make_unique<net::ApiClient>(Config::Instance().GetApiServer());
    api_client_->PrefetchGenresAsync();

#if defined(_WIN32) && defined(CPPHTTPLIB_OPENSSL_SUPPORT)
    UpdaterCheckAsync();
#endif

    const std::string& saved_token = Config::Instance().GetAuthToken();
    if (!saved_token.empty()) {
        api_client_->RestoreSession(saved_token,
            Config::Instance().GetAuthUserID(),
            Config::Instance().GetAuthUsername(),
            Config::Instance().GetAuthDisplayName());

        std::thread([api = api_client_.get()]() {
            if (!api->ValidateSession()) {
                Config::Instance().ClearAuth();
            }
        }).detach();
    }
}

void App::Render() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##MainWindow", nullptr, flags);

    ImVec2 win_min = ImGui::GetWindowPos();
    ImVec2 win_max = ImVec2(win_min.x + ImGui::GetWindowWidth(), win_min.y + ImGui::GetWindowHeight());
    ImU32 col_top = IM_COL32(18, 18, 24, 255);
    ImU32 col_bot = IM_COL32(8, 8, 12, 255);
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(win_min, win_max, col_top, col_top, col_bot, col_bot);

    float s = tunngle::GetUIScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20.0f * s, 12.0f * s));

    static int active_tab = 0;
    static int pending_switch_to_tab = -1;
    static CommunitySubTab community_sub = CommunitySubTab::Networks;
    static bool show_create_lobby_modal = false;
    static bool open_account_popup = false;
    auto* coord = GetCoordClient();
    auto* api = GetApiClient();

    static bool community_hovered = false;
    static ImVec2 s_comm_tab_min, s_comm_tab_max;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::BeginChild("##HeaderBar", ImVec2(0, 44.0f * s), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    TexID logo_tex = TextureCacheGetLogo();
    if (logo_tex > 0) {
        float logo_h = 34.0f * s;
        float logo_w = logo_h * 2.5f;
        ImGui::Image(static_cast<ImTextureID>((uintptr_t)logo_tex), ImVec2(logo_w, logo_h));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
        ImGui::Text("PEERDEN");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0.0f, 24.0f * s);

    if (ImGui::BeginTabBar("##TopTabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Home")) {
            active_tab = 0;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Community  ")) {
            active_tab = 1;
            ImGui::EndTabItem();
        }
        s_comm_tab_min = ImGui::GetItemRectMin();
        s_comm_tab_max = ImGui::GetItemRectMax();

        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float arrow_left = s_comm_tab_max.x - 18.0f * s;
            float cx = s_comm_tab_max.x - 10.0f * s;
            float cy = (s_comm_tab_min.y + s_comm_tab_max.y) * 0.5f + 1.0f * s;

            ImVec2 mouse = ImGui::GetIO().MousePos;
            bool over_arrow = (mouse.x >= arrow_left && mouse.x <= s_comm_tab_max.x &&
                               mouse.y >= s_comm_tab_min.y && mouse.y <= s_comm_tab_max.y);
            community_hovered = over_arrow;

            ImU32 arrow_col = over_arrow ? IM_COL32(240, 240, 245, 255)
                                         : IM_COL32(160, 160, 170, 200);
            dl->AddTriangleFilled(
                ImVec2(cx - 3.0f * s, cy - 2.0f * s),
                ImVec2(cx + 3.0f * s, cy - 2.0f * s),
                ImVec2(cx, cy + 2.0f * s),
                arrow_col);
        }

        bool select_system = (pending_switch_to_tab == 2);
        if (select_system) pending_switch_to_tab = -1;
        if (ImGui::BeginTabItem("System", nullptr, select_system ? ImGuiTabItemFlags_SetSelected : 0)) {
            active_tab = 2;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Help")) {
            active_tab = 3;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 260.0f * s);
    ImGui::AlignTextToFramePadding();
    if (api->IsLoggedIn()) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", api->GetDisplayName().c_str());
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Guest");
    }
    ImGui::SameLine(0.0f, 8.0f * s);
    ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "|");
    ImGui::SameLine(0.0f, 8.0f * s);
    if (coord && coord->IsConnected()) {
        ImGui::TextColored(ImVec4(0.3f, 0.75f, 0.4f, 1.0f), "Online");
    } else {
        ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.3f, 1.0f), "Unhealthy");
    }
    ImGui::SameLine(0.0f, 12.0f * s);
    if (ImGui::Button("Account")) {
        open_account_popup = true;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (community_hovered) {
        ImGui::OpenPopup("##CommunityDropdown");
    }
    ImGui::SetNextWindowPos(ImVec2(s_comm_tab_min.x, s_comm_tab_max.y + 1.0f * s), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##CommunityDropdown")) {
        bool in_lobby = coord && coord->IsInRoom();
        if (ImGui::BeginMenu("Networks")) {
            if (ImGui::MenuItem("Make a lobby", nullptr, false, !in_lobby)) {
                if (!api->IsLoggedIn()) {
                    open_account_popup = true;
                } else {
                    community_sub = CommunitySubTab::Networks;
                    show_create_lobby_modal = true;
                }
            }
            if (ImGui::MenuItem("Leave lobby", nullptr, false, in_lobby)) {
                if (GetCoordClient() && GetPeerTunnelManager()) {
                    for (const auto& p : GetCoordClient()->GetPeers()) {
                        GetPeerTunnelManager()->RemovePeer(p.tun_ip);
                    }
                    GetCoordClient()->Leave();
                    GetCoordClient()->Disconnect();
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Forum")) {
            OpenURL("https://forums.peerden.io");
        }
        if (ImGui::MenuItem("Event Planner")) {
            OpenURL("https://www.gamedate.org/discuss");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("External link. PeerDen is not affiliated with GameDate.");
        }
        ImGui::MenuItem("Download Skins", nullptr, false, false);  // Coming soon
        ImGui::EndPopup();
    }

    if (open_account_popup) {
        ImGui::OpenPopup("##AccountPopup");
        open_account_popup = false;
    }
    if (ImGui::BeginPopup("##AccountPopup")) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 6.0f * s));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, 4.0f * s));
        const float lbl = 110.0f * s;

        static char auth_user[64] = "";
        static char auth_pass[64] = "";
        static char auth_display[64] = "";
        static int account_tab = 0;
        static bool agreed_to_terms = false;
        static char status_msg[256] = "";
        static bool status_ok = false;
        static bool needs_refresh = true;

        if (api->SessionExpired()) {
            api->ClearExpiredFlag();
            Config::Instance().ClearAuth();
            needs_refresh = true;
            snprintf(status_msg, sizeof(status_msg), "Session expired, please sign in again");
            status_ok = false;
        }

        if (ImGui::IsWindowAppearing()) {
            status_msg[0] = '\0';
            if (api->IsLoggedIn() && needs_refresh) {
                api->RefreshFavorites();
                needs_refresh = false;
            }
        }

        if (api->IsLoggedIn()) {
            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Profile");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Signed in as");
            ImGui::SameLine(lbl);
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", api->GetDisplayName().c_str());

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Username");
            ImGui::SameLine(lbl);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "%s", api->GetUsername().c_str());

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Coord Status");
            ImGui::SameLine(lbl);
            if (coord && coord->IsConnected()) {
                if (coord->IsInRoom()) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "In Room (%d peers)", (int)coord->GetPeers().size());
                } else {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Connected");
                }
            } else {
                ImGui::TextColored(ImVec4(0.7f, 0.55f, 0.3f, 1.0f), "Disconnected");
            }

            ImGui::Spacing();
            if (ImGui::Button("Sign Out", ImVec2(100.0f * s, 0))) {
                api->Logout();
                Config::Instance().ClearAuth();
                needs_refresh = true;
                snprintf(status_msg, sizeof(status_msg), "Signed out");
                status_ok = true;
            }

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Favorite Rooms");
            ImGui::Separator();
            ImGui::Spacing();

            const auto& favs = api->CachedFavorites();
            if (favs.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "  No favorites yet");
            } else {
                for (size_t i = 0; i < favs.size(); i++) {
                    ImGui::Text("  %s", favs[i].c_str());
                    ImGui::SameLine(0.0f, 8.0f * s);
                    char rm_id[32];
                    snprintf(rm_id, sizeof(rm_id), "x##fav_%zu", i);
                    if (ImGui::SmallButton(rm_id)) {
                        api->RemoveFavorite(favs[i]);
                    }
                }
            }

        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Account");
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginTabBar("##AuthTabs")) {
                if (ImGui::BeginTabItem("Sign In")) {
                    account_tab = 0;
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Register")) {
                    account_tab = 1;
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Spacing();

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Username");
            ImGui::SameLine(lbl);
            ImGui::SetNextItemWidth(180.0f * s);
            ImGui::InputText("##AuthUser", auth_user, sizeof(auth_user));

            ImGui::AlignTextToFramePadding();
            ImGui::Text("Password");
            ImGui::SameLine(lbl);
            ImGui::SetNextItemWidth(180.0f * s);
            ImGui::InputText("##AuthPass", auth_pass, sizeof(auth_pass), ImGuiInputTextFlags_Password);

            if (account_tab == 1) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Display Name");
                ImGui::SameLine(lbl);
                ImGui::SetNextItemWidth(180.0f * s);
                ImGui::InputText("##AuthDisplay", auth_display, sizeof(auth_display));
            }

            ImGui::Spacing();
            ImGui::Text("By signing in you agree to our ");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 0.95f, 1.0f));
            if (ImGui::Selectable("Terms", false, ImGuiSelectableFlags_None)) {
                OpenURL("https://peerden.io/terms");
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Text(" and ");
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 0.95f, 1.0f));
            if (ImGui::Selectable("Privacy Policy", false, ImGuiSelectableFlags_None)) {
                OpenURL("https://peerden.io/privacy");
            }
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::Text(".");
            ImGui::Checkbox("I agree", &agreed_to_terms);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Required to sign in or create an account");
            }

            ImGui::Spacing();
            bool fields_empty = (auth_user[0] == '\0' || auth_pass[0] == '\0');
            bool cannot_submit = fields_empty || !agreed_to_terms;
            if (cannot_submit) ImGui::BeginDisabled();

            if (account_tab == 0) {
                if (ImGui::Button("Sign In", ImVec2(110.0f * s, 30.0f * s))) {
                    auto res = api->Login(auth_user, auth_pass);
                    if (res.ok) {
                        Config::Instance().SetNickname(res.display_name);
                        Config::Instance().SetAuthToken(res.token);
                        Config::Instance().SetAuthUserID(res.user_id);
                        Config::Instance().SetAuthUsername(res.username);
                        Config::Instance().SetAuthDisplayName(res.display_name);
                        snprintf(status_msg, sizeof(status_msg), "Welcome, %s!", res.display_name.c_str());
                        status_ok = true;
                        needs_refresh = true;
                        api->RefreshFavorites();
                        needs_refresh = false;
                        auth_pass[0] = '\0';
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "%s", res.error.c_str());
                        status_ok = false;
                    }
                }
            } else {
                if (ImGui::Button("Create Account", ImVec2(130.0f * s, 30.0f * s))) {
                    auto res = api->Register(auth_user, auth_pass, "", auth_display);
                    if (res.ok) {
                        Config::Instance().SetNickname(res.display_name);
                        Config::Instance().SetAuthToken(res.token);
                        Config::Instance().SetAuthUserID(res.user_id);
                        Config::Instance().SetAuthUsername(res.username);
                        Config::Instance().SetAuthDisplayName(res.display_name);
                        snprintf(status_msg, sizeof(status_msg), "Account created! Welcome, %s!", res.display_name.c_str());
                        status_ok = true;
                        needs_refresh = true;
                        api->RefreshFavorites();
                        needs_refresh = false;
                        auth_pass[0] = '\0';
                    } else {
                        snprintf(status_msg, sizeof(status_msg), "%s", res.error.c_str());
                        status_ok = false;
                    }
                }
            }
            if (cannot_submit) ImGui::EndDisabled();

            ImGui::Spacing();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Offline Mode");
            ImGui::Separator();
            ImGui::Spacing();
            static char nick_buf[64];
            if (ImGui::IsWindowAppearing()) {
                snprintf(nick_buf, sizeof(nick_buf), "%s", Config::Instance().GetNickname().c_str());
            }
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Nickname");
            ImGui::SameLine(lbl);
            ImGui::SetNextItemWidth(180.0f * s);
            ImGui::InputText("##Nick", nick_buf, sizeof(nick_buf));
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                Config::Instance().SetNickname(nick_buf);
            }
        }

        if (status_msg[0]) {
            ImGui::Spacing();
            ImVec4 col = status_ok ? ImVec4(0.4f, 0.9f, 0.5f, 1.0f) : ImVec4(0.9f, 0.35f, 0.35f, 1.0f);
            ImGui::TextColored(col, "%s", status_msg);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "PeerDen v%s", PEERDDEN_VERSION);

        ImGui::PopStyleVar(2);
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);

    if (HasPendingTunError()) {
        net::TunAdapter* tun = GetTunAdapter();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.25f, 0.12f, 0.12f, 1.0f));
        ImGui::BeginChild("##TunErrorBanner", ImVec2(0, 36.0f * s), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1.0f),
            "TUN interface failed. Creating a virtual network requires administrator access.");
        float btn_x = ImGui::GetContentRegionAvail().x - 280.0f * s;
        if (tun && tun->SupportsElevationPrompt()) {
            btn_x -= 130.0f * s;
        }
        ImGui::SameLine(btn_x);
        if (tun && tun->SupportsElevationPrompt()) {
            if (ImGui::Button("Grant Permission", ImVec2(130.0f * s, 0))) {
                tun->RequestElevationAndRelaunch();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("Open System", ImVec2(100.0f * s, 0))) {
            pending_switch_to_tab = 2;
        }
        ImGui::SameLine();
        if (ImGui::Button("Dismiss", ImVec2(70.0f * s, 0))) {
            ClearPendingTunError();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    const float footer_h = 28.0f * s;
    ImVec2 content_avail = ImGui::GetContentRegionAvail();
    ImGui::BeginChild("##ContentArea", ImVec2(0, content_avail.y - footer_h), false, ImGuiWindowFlags_None);
    if (active_tab == 0) {
        RenderHomeTab(GetTunAdapter(), GetCoordClient(), GetPeerTunnelManager(), GetApiClient());
    } else if (active_tab == 1) {
        tunngle::RenderCommunityTab(GetTunAdapter(), GetCoordClient(), GetPeerTunnelManager(),
                                    GetApiClient(), community_sub, show_create_lobby_modal, open_account_popup);
    } else if (active_tab == 2) {
        RenderSystemTab(this);
    } else if (active_tab == 3) {
        RenderHelpTab();
    }
    ImGui::EndChild();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
    ImGui::BeginChild("##Footer", ImVec2(0, 28.0f * s), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPosY((28.0f * s - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "Made with ");
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.45f, 1.0f), "<3");
    ImGui::SameLine(0.0f, 4.0f * s);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 0.95f, 1.0f));
    if (ImGui::Selectable("Support on Ko-fi", false, ImGuiSelectableFlags_None)) {
        OpenURL("https://ko-fi.com/devnt");
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("https://ko-fi.com/devnt");
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

#if defined(_WIN32) && defined(CPPHTTPLIB_OPENSSL_SUPPORT)
    {
        UpdateStatus ustatus = UpdaterGetStatus();
        static bool update_prompt_dismissed = false;
        if (ustatus == UpdateStatus::UpdateAvailable && !update_prompt_dismissed) {
            ImGui::OpenPopup("Update Available");
        }
        if (ustatus == UpdateStatus::DownloadComplete) {
            ImGui::OpenPopup("Update Ready");
        }
        if (ImGui::BeginPopupModal("Update Available", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            UpdateInfo info = UpdaterGetInfo();
            ImGui::Text("A new version of PeerDen is available.");
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "v%s", info.version.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Download", ImVec2(120.0f * s, 0))) {
                UpdaterDownloadAsync();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Later", ImVec2(80.0f * s, 0))) {
                update_prompt_dismissed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Update Ready", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Update ready to install!");
            ImGui::Spacing();
            if (ImGui::Button("Install and restart", ImVec2(160.0f * s, 0))) {
                UpdaterInstallAndExit();
            }
            ImGui::SameLine();
            if (ImGui::Button("Later", ImVec2(80.0f * s, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ustatus == UpdateStatus::Downloading && !ImGui::IsPopupOpen("Downloading")) {
            ImGui::OpenPopup("Downloading");
        }
        if (((ustatus == UpdateStatus::Downloading) || ImGui::IsPopupOpen("Downloading")) &&
            ImGui::BeginPopupModal("Downloading", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ustatus != UpdateStatus::Downloading) {
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::Text("Downloading update...");
                ImGui::ProgressBar(UpdaterGetProgress(), ImVec2(200.0f * s, 0));
            }
            ImGui::EndPopup();
        }
    }
#endif

    ImGui::End();
}

}
