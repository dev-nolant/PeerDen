#include "system_tab.h"
#include "app.h"
#include "core/config.h"
#include "core/logger.h"
#include "theme.h"
#include "net/tun_adapter.h"
#include "imgui.h"
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
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

static void OpenLogFolder() {
    std::string dir = Logger::Instance().GetLogDir();
    if (dir.empty()) return;
#ifdef _WIN32
    (void)ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    pid_t pid = fork();
    if (pid == 0) {
        execlp("open", "open", dir.c_str(), (char*)nullptr);
        _exit(0);
    }
#else
    pid_t pid = fork();
    if (pid == 0) {
        execlp("xdg-open", "xdg-open", dir.c_str(), (char*)nullptr);
        _exit(0);
    }
#endif
}

static char s_log_search[256] = "";
static bool s_filter_trace = true;
static bool s_filter_debug = true;
static bool s_filter_info = true;
static bool s_filter_warn = true;
static bool s_filter_error = true;
static bool s_auto_scroll = true;

bool MatchesSearch(const std::string& msg, const char* search) {
    if (!search || !search[0]) return true;
    std::string lower_msg = msg;
    std::string lower_search = search;
    std::transform(lower_msg.begin(), lower_msg.end(), lower_msg.begin(), ::tolower);
    std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), ::tolower);
    return lower_msg.find(lower_search) != std::string::npos;
}

bool MatchesLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return s_filter_trace;
        case LogLevel::Debug: return s_filter_debug;
        case LogLevel::Info:  return s_filter_info;
        case LogLevel::Warn:  return s_filter_warn;
        case LogLevel::Error: return s_filter_error;
        default: return true;
    }
}

ImVec4 LevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        case LogLevel::Warn:  return ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
        case LogLevel::Info:  return ImVec4(0.4f, 0.85f, 0.55f, 1.0f);
        case LogLevel::Debug: return ImVec4(0.5f, 0.65f, 0.95f, 1.0f);
        case LogLevel::Trace: return ImVec4(0.55f, 0.55f, 0.6f, 1.0f);
        default: return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
    }
}

}  // namespace

void RenderSystemTab(App* app) {
    auto* tun = app ? app->GetTunAdapter() : nullptr;
    float s = GetUIScale();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 6.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, 4.0f * s));

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float lbl = 130.0f * s;

    if (app && app->HasPendingTunError() && tun) {
        ImGui::OpenPopup("TUN Error");
    }

    if (!tun) {
        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.2f, 1.0f), "TUN adapter not available on this platform.");
        ImGui::TextWrapped("Supported platforms: macOS (utun) and Windows (WinTun).");
        ImGui::PopStyleVar(2);
        return;
    }

    float settings_w = avail.x * 0.45f;
    ImGui::BeginChild("##SysSettings", ImVec2(settings_w, avail.y), false, ImGuiWindowFlags_None);

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "TUN Adapter");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("##TunPanel", ImVec2(0, 160.0f * s), true, ImGuiWindowFlags_None);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Status");
    ImGui::SameLine(lbl);
    if (tun->IsOpen()) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Connected");
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Interface");
        ImGui::SameLine(lbl);
        ImGui::Text("%s", tun->GetInterfaceName().c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Assigned IP");
        ImGui::SameLine(lbl);
        ImGui::Text("%s", tun->GetAssignedIP().c_str());
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f), "Disconnected");
    }

    bool auto_connect = Config::Instance().GetAutoConnectTun();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Auto-connect");
    ImGui::SameLine(lbl);
    if (ImGui::Checkbox("##AutoTun", &auto_connect)) {
        Config::Instance().SetAutoConnectTun(auto_connect);
    }

    static char local_ip_buf[32];
    if (local_ip_buf[0] == '\0') {
        snprintf(local_ip_buf, sizeof(local_ip_buf), "%s", Config::Instance().GetLocalTunIP().c_str());
    }
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Fallback IP");
    ImGui::SameLine(lbl);
    ImGui::SetNextItemWidth(120.0f * s);
    if (ImGui::InputText("##TunIP", local_ip_buf, sizeof(local_ip_buf))) {
        Config::Instance().SetLocalTunIP(local_ip_buf);
    }

    ImGui::Spacing();
    if (tun->IsOpen()) {
        if (ImGui::Button("Disconnect", ImVec2(110.0f * s, 28.0f * s))) {
            tun->Close();
        }
    } else {
        if (ImGui::Button("Connect", ImVec2(110.0f * s, 28.0f * s))) {
            tun->SetLocalIP(Config::Instance().GetLocalTunIP());
            tun->SetUnit(Config::Instance().GetTunUnit());
            if (!tun->Open()) {
                ImGui::OpenPopup("TUN Error");
            }
        }
    }

    if (ImGui::BeginPopupModal("TUN Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Failed to open TUN interface.");
        ImGui::TextWrapped("Creating a virtual network requires administrator access.");
        ImGui::Spacing();
        if (tun->SupportsElevationPrompt()) {
            if (ImGui::Button("Grant Permission", ImVec2(130.0f * s, 0))) {
                tun->RequestElevationAndRelaunch();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button("OK", ImVec2(60.0f * s, 0))) {
            if (app) app->ClearPendingTunError();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    const std::string& log_path = Logger::Instance().GetLogPath();
    if (!log_path.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Log file");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%s", log_path.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", log_path.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Open folder", ImVec2(100.0f * s, 0))) OpenLogFolder();
        ImGui::SameLine();
        if (ImGui::Button("Copy path", ImVec2(90.0f * s, 0))) ImGui::SetClipboardText(log_path.c_str());
    }

    ImGui::EndChild();

    ImGui::SameLine(0.0f, 10.0f * s);

    ImGui::BeginChild("##LogPanel", ImVec2(0, avail.y), false, ImGuiWindowFlags_None);

    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "Logs");
    float search_width = 160.0f * s;
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - search_width - 50.0f * s);
    ImGui::SetNextItemWidth(search_width);
    ImGui::InputTextWithHint("##LogSearch", "Search...", s_log_search, sizeof(s_log_search));
    ImGui::SameLine();
    if (ImGui::SmallButton("X")) {
        s_log_search[0] = '\0';
    }
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f * s, 2.0f * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * s, 2.0f * s));
    ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.6f, 1.0f), "Filter:");
    ImGui::SameLine();
    auto FilterBtn = [](bool on, const char* on_label, const char* off_label) {
        if (!on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
        bool clicked = ImGui::SmallButton(on ? on_label : off_label);
        if (!on) ImGui::PopStyleColor();
        return clicked;
    };
    if (FilterBtn(s_filter_error, "[E]", " E ")) s_filter_error = !s_filter_error;
    ImGui::SameLine();
    if (FilterBtn(s_filter_warn, "[W]", " W ")) s_filter_warn = !s_filter_warn;
    ImGui::SameLine();
    if (FilterBtn(s_filter_info, "[I]", " I ")) s_filter_info = !s_filter_info;
    ImGui::SameLine();
    if (FilterBtn(s_filter_debug, "[D]", " D ")) s_filter_debug = !s_filter_debug;
    ImGui::SameLine();
    if (FilterBtn(s_filter_trace, "[T]", " T ")) s_filter_trace = !s_filter_trace;
    ImGui::SameLine(0.0f, 12.0f * s);
    if (ImGui::SmallButton(s_auto_scroll ? "Auto: ON" : "Auto: OFF")) s_auto_scroll = !s_auto_scroll;
    ImGui::PopStyleVar(2);
    ImGui::Spacing();

    ImGui::BeginChild("##LogContent", ImVec2(0, -1), true,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysUseWindowPadding);

    std::vector<std::pair<LogLevel, std::string>> logs;
    Logger::Instance().GetRecentLogs(logs);

    int visible_count = 0;
    for (const auto& [level, msg] : logs) {
        if (!MatchesLevel(level) || !MatchesSearch(msg, s_log_search)) continue;
        visible_count++;
        ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(level));
        ImGui::TextUnformatted(msg.c_str());
        ImGui::PopStyleColor();
    }

    if (s_auto_scroll && visible_count > 0) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::PopStyleVar(2);
}

}  // namespace tunngle
