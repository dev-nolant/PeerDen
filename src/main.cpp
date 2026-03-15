#define GL_SILENCE_DEPRECATION
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#endif
#include "app.h"
#include "core/config.h"
#include "core/logger.h"
#include "net/coord_client.h"
#include "net/tun_packet_processor.h"
#include "net/udp_tunnel.h"
#include "texture_cache.h"
#include "theme.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "stb_image.h"
#include <GLFW/glfw3.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#include <httplib.h>

namespace {

std::string FetchCoordCertFromAPI(const std::string& api_base) {
    if (api_base.empty()) return "";
    std::string url = api_base;
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.empty()) return "";
    httplib::Client cli(url);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(5);
    auto res = cli.Get("/.well-known/coord-server-cert.pem");
    if (!res || res->status != 200 || res->body.empty()) return "";
    if (res->body.find("-----BEGIN CERTIFICATE-----") == std::string::npos) return "";
    return res->body;
}

bool ParseCoordServer(const std::string& s, std::string& host, uint16_t& port, bool& use_tls) {
    use_tls = false;
    std::string rest = s;
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) rest.erase(0, 1);
    while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) rest.pop_back();
    if (rest.size() >= 6) {
        std::string scheme = rest.substr(0, 6);
        for (char& c : scheme) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (scheme == "tls://") {
            use_tls = true;
            rest = rest.substr(6);
        }
    }
    size_t colon = rest.rfind(':');
    if (colon == std::string::npos) return false;
    host = rest.substr(0, colon);
    int p = std::atoi(rest.substr(colon + 1).c_str());
    if (p <= 0 || p >= 65536) return false;
    port = static_cast<uint16_t>(p);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    tunngle::Logger::Instance().Init();
    tunngle::Config::Instance().ParseArgs(argc, argv);
    tunngle::Config::Instance().Load();
    LOG_INFO("PeerDen starting");

    if (!glfwInit()) {
        LOG_ERROR("Failed to initialize GLFW");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1024, 768, "PeerDen", nullptr, nullptr);
    if (!window) {
        LOG_ERROR("Failed to create GLFW window");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    {
#ifdef _WIN32
        char exe_path[MAX_PATH];
        DWORD len = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
        std::string icon_path = (len > 0) ? std::string(exe_path) : ".";
        size_t slash = icon_path.find_last_of("\\/");
        if (slash != std::string::npos) icon_path = icon_path.substr(0, slash + 1);
        icon_path += "icon.png";
#elif defined(__APPLE__)
        char exe_path[PATH_MAX];
        uint32_t size = sizeof(exe_path);
        std::string icon_path = (_NSGetExecutablePath(exe_path, &size) == 0) ? std::string(exe_path) : ".";
        size_t slash = icon_path.find_last_of('/');
        if (slash != std::string::npos) icon_path = icon_path.substr(0, slash + 1);
        icon_path += "icon.png";
#else
        char exe_path[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        std::string icon_path = (n > 0) ? std::string(exe_path, n) : ".";
        size_t slash = icon_path.find_last_of('/');
        if (slash != std::string::npos) icon_path = icon_path.substr(0, slash + 1);
        icon_path += "icon.png";
#endif
        int iw = 0, ih = 0;
        unsigned char* pixels = stbi_load(icon_path.c_str(), &iw, &ih, nullptr, 4);
        if (pixels && iw > 0 && ih > 0) {
            GLFWimage img = { iw, ih, pixels };
            glfwSetWindowIcon(window, 1, &img);
            stbi_image_free(pixels);
        }
    }
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    tunngle::LoadFonts();
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float ui_scale = (xscale > yscale) ? xscale : yscale;
    if (ui_scale > 1.0f) {
        tunngle::SetUIScale(ui_scale);
        int base_w = 1024, base_h = 768;
        glfwSetWindowSize(window, (int)(base_w * ui_scale), (int)(base_h * ui_scale));
        ImGui::GetIO().FontGlobalScale = ui_scale;
        tunngle::ApplyTunngleTheme();
        ImGui::GetStyle().ScaleAllSizes(ui_scale);
    } else {
        tunngle::SetUIScale(1.0f);
        tunngle::ApplyTunngleTheme();
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    tunngle::TextureCacheInit(tunngle::Config::Instance().GetApiServer());
    tunngle::App app;

    uint64_t now_ms = 0;
    uint64_t last_frame_ms = 0;
    float fps = 0.0f;
    static constexpr int kFpsHistorySize = 120;
    float fps_history[kFpsHistorySize] = {};
    int fps_history_idx = 0;
    bool show_debug_fps = false;
    uint64_t last_connect_attempt = 0;
    uint64_t last_tun_open_attempt = 0;
    int consecutive_tun_failures = 0;
    bool was_in_room = false;
    std::string active_tun_ip;  // IP currently assigned to the TUN interface
    std::atomic<bool> connect_in_progress{false};

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());

        auto* coord = app.GetCoordClient();
        auto* peer_mgr = app.GetPeerTunnelManager();
        auto* tun = app.GetTunAdapter();
        auto* tunnel = app.GetUdpTunnel();

        if (coord) {
            if (!coord->IsConnected() && !connect_in_progress && now_ms - last_connect_attempt > 5000) {
                last_connect_attempt = now_ms;
                std::string host;
                uint16_t port;
                bool use_tls;
                if (ParseCoordServer(tunngle::Config::Instance().GetCoordServer(), host, port, use_tls)) {
                    std::string config_dir = tunngle::Config::Instance().GetConfigDir();
                    std::string cert_path = config_dir;
                    if (!cert_path.empty()) {
#ifdef _WIN32
                        if (cert_path.back() != '/' && cert_path.back() != '\\') cert_path += '\\';
#else
                        if (cert_path.back() != '/') cert_path += '/';
#endif
                    }
                    cert_path += "coord_server_cert.pem";
                    std::ifstream cert_check(cert_path);
                    std::string trusted_cert = cert_check.good() ? cert_path : std::string();
                    std::string api_base = tunngle::Config::Instance().GetApiServer();
                    connect_in_progress = true;
                    std::thread([coord, host, port, use_tls, trusted_cert, cert_path, config_dir, api_base, &connect_in_progress]() {
                        coord->ClearLastError();
                        if (coord->Connect(host, port, use_tls, trusted_cert)) {
                            LOG_INFO("Auto-connected to coord server");
                            coord->RequestRoomList();
                        } else if (use_tls && trusted_cert.empty() && !api_base.empty()) {
                            std::string pem = FetchCoordCertFromAPI(api_base);
                            if (!pem.empty() && !config_dir.empty() &&
                                tunngle::Config::Instance().EnsureConfigDirExists()) {
                                std::ofstream out(cert_path);
                                if (out) {
                                    out << pem;
                                    out.close();
                                    LOG_INFO("Coord: fetched server cert, retrying...");
                                    if (coord->Connect(host, port, use_tls, cert_path)) {
                                        LOG_INFO("Auto-connected to coord server");
                                        coord->RequestRoomList();
                                    }
                                }
                            }
                            if (!coord->IsConnected()) {
                                coord->SetLastError("Could not connect to coordination server. Check your network and that the server is running.");
                            }
                        } else if (!coord->IsConnected()) {
                            coord->SetLastError("Could not connect to coordination server. Check coord_server in Settings.");
                        }
                        connect_in_progress = false;
                    }).detach();
                }
            }
            coord->Pump();
        }

        bool in_room = coord && coord->IsInRoom();
        if (was_in_room && !in_room && peer_mgr) {
            peer_mgr->ClearAll();
            if (tun && tun->IsOpen()) {
                tun->Close();
                active_tun_ip.clear();
            }
            consecutive_tun_failures = 0;
        }
        was_in_room = in_room;

        if (in_room) {
            std::string assigned = coord->GetAssignedTunIP();

            if (tun && !assigned.empty() && tun->IsOpen() && active_tun_ip != assigned) {
                LOG_INFO("TUN IP changed: %s -> %s, reopening", active_tun_ip.c_str(), assigned.c_str());
                tun->Close();
                active_tun_ip.clear();
            }

            if (!assigned.empty() && tun && !tun->IsOpen() &&
                consecutive_tun_failures < 3 &&
                now_ms - last_tun_open_attempt > 5000) {
                last_tun_open_attempt = now_ms;
                tun->SetLocalIP(assigned);
                tun->SetUnit(tunngle::Config::Instance().GetTunUnit());
                if (tun->Open()) {
                    active_tun_ip = assigned;
                    consecutive_tun_failures = 0;
                } else {
                    consecutive_tun_failures++;
                    if (consecutive_tun_failures >= 3) {
                        app.SetPendingTunError(true);
                    }
                }
            }

            std::string relay = coord->GetRelayAddr();
            if (!relay.empty() && !assigned.empty()) {
                size_t rcolon = relay.rfind(':');
                if (rcolon != std::string::npos) {
                    std::string rhost = relay.substr(0, rcolon);
                    int rport = std::atoi(relay.substr(rcolon + 1).c_str());
                    if (rport > 0 && rport < 65536) {
                        peer_mgr->SetRelayAddr(rhost, static_cast<uint16_t>(rport), assigned, coord->GetRelayToken());
                    }
                }
            }
            const auto& peers = coord->GetPeers();
            for (const auto& p : peers) {
                if (peer_mgr->GetTunnelFor(p.tun_ip) == nullptr) {
                    uint16_t base = tunngle::Config::Instance().GetLocalPort();
                    if (base == 0) base = 11155;
                    peer_mgr->AddPeer(p.ip, p.port, p.tun_ip, p.auth_token, base);
                }
            }
            for (const std::string& tun_ip : peer_mgr->GetPeerTunIPs()) {
                bool still_in_list = false;
                for (const auto& p : peers) {
                    if (p.tun_ip == tun_ip) { still_in_list = true; break; }
                }
                if (!still_in_list) {
                    peer_mgr->RemovePeer(tun_ip);
                }
            }
            if (peer_mgr) {
                now_ms = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                peer_mgr->Pump(now_ms);
                tunngle::net::ProcessTunPackets(tun, peer_mgr, assigned);
                tunngle::net::ProcessTunnelPackets(tun, peer_mgr, assigned);
            }
        }
        if (!in_room) {
            if (tunnel) tunnel->Pump(now_ms);
            tunngle::net::ProcessTunPackets(tun, tunnel,
                                            tunngle::Config::Instance().GetLocalTunIP());
            tunngle::net::ProcessTunnelPackets(tun, tunnel);
        }

        tunngle::TextureCachePump();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (last_frame_ms != 0) {
            float delta_ms = static_cast<float>(now_ms - last_frame_ms);
            if (delta_ms > 0.0f) {
                fps = 1000.0f / delta_ms;
                if (fps_history_idx < kFpsHistorySize) {
                    fps_history[fps_history_idx++] = fps;
                } else {
                    for (int i = 0; i < kFpsHistorySize - 1; i++) {
                        fps_history[i] = fps_history[i + 1];
                    }
                    fps_history[kFpsHistorySize - 1] = fps;
                }
            }
        }
        last_frame_ms = now_ms;

        if (ImGui::IsKeyPressed(ImGuiKey_F3)) {
            show_debug_fps = !show_debug_fps;
        }

        app.Render();

        if (show_debug_fps) {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 220, vp->WorkPos.y + 8),
                ImGuiCond_Always, ImVec2(0, 0));
            ImGui::SetNextWindowBgAlpha(0.85f);
            if (ImGui::Begin("##DebugFPS", nullptr,
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%.1f FPS", fps);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "(%.1f ms)",
                    fps > 0.0f ? 1000.0f / fps : 0.0f);
                int count = fps_history_idx;
                if (count > 0) {
                    ImGui::PlotLines("##FpsGraph", fps_history, count, 0, nullptr, 0.0f, 120.0f,
                        ImVec2(200, 50), sizeof(float));
                }
                ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "F3 to toggle");
            }
            ImGui::End();
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    tunngle::TextureCacheShutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    tunngle::Config::Instance().ForceSave();
    tunngle::Logger::Instance().Shutdown();
    return 0;
}
