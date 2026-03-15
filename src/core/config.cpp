#include "config.h"
#include "logger.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace tunngle {

namespace {

std::string GetConfigDirFromEnv() {
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata) {
        return std::string(appdata) + "\\peerdden";
    }
    return ".";
#else
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        return std::string(xdg) + "/peerdden";
    }
    const char* home = getenv("HOME");
    if (home && home[0]) {
#ifdef __APPLE__
        return std::string(home) + "/Library/Application Support/peerdden";
#else
        return std::string(home) + "/.config/peerdden";
#endif
    }
#ifdef __APPLE__
    uid_t uid = getuid();
    if (uid == 0) {
        const char* sudo_user = getenv("SUDO_USER");
        if (sudo_user && sudo_user[0]) {
            struct passwd* pw = getpwnam(sudo_user);
            if (pw && pw->pw_dir) {
                return std::string(pw->pw_dir) + "/Library/Application Support/peerdden";
            }
        }
    }
    struct passwd* pw = getpwuid(uid);
    if (pw && pw->pw_dir) {
        return std::string(pw->pw_dir) + "/Library/Application Support/peerdden";
    }
#endif
    return ".";
#endif
}

bool EnsureDir(const std::string& path) {
#ifdef _WIN32
    struct _stat st;
    if (_stat(path.c_str(), &st) == 0) {
        return (st.st_mode & _S_IFDIR) != 0;
    }
    return _mkdir(path.c_str()) == 0;
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(path.c_str(), 0700) == 0;
#endif
}

}  // namespace

Config& Config::Instance() {
    static Config instance;
    return instance;
}

Config::Config() {
    config_path_ = GetDefaultConfigPath();
}

std::string Config::GetDefaultConfigPath() const {
    std::string dir = GetConfigDirFromEnv();
#ifdef _WIN32
    return dir + "\\config.ini";
#else
    return dir + "/config.ini";
#endif
}

std::string Config::GetConfigDir() const {
    size_t pos = config_path_.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return config_path_.substr(0, pos);
}

bool Config::EnsureConfigDirExists() const {
    std::string dir = GetConfigDir();
    if (dir.empty() || dir == ".") return true;
    return EnsureDir(dir);
}

void Config::SetConfigDir(const std::string& dir) {
#ifdef _WIN32
    config_path_ = dir + "\\config.ini";
#else
    config_path_ = dir + "/config.ini";
#endif
}

void Config::ParseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.find("--config-dir=") == 0) {
            SetConfigDir(arg.substr(13));
            break;
        }
    }
}

void Config::Load() {
    std::ifstream f(config_path_);
    if (!f.is_open()) {
        LOG_DEBUG("Config not found, using defaults");
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "auto_connect_tun") {
            auto_connect_tun_ = (val == "1" || val == "true" || val == "yes");
        } else if (key == "local_tun_ip") {
            if (!val.empty()) local_tun_ip_ = val;
        } else if (key == "peer_address") {
            peer_address_ = val;
        } else if (key == "peer_port") {
            int p = std::atoi(val.c_str());
            if (p > 0 && p < 65536) peer_port_ = static_cast<uint16_t>(p);
        } else if (key == "local_port") {
            int p = std::atoi(val.c_str());
            if (p >= 0 && p < 65536) local_port_ = static_cast<uint16_t>(p);
        } else if (key == "tun_unit") {
            unsigned long u = std::strtoul(val.c_str(), nullptr, 10);
            if (u < 16) tun_unit_ = static_cast<uint32_t>(u);
        } else if (key == "coord_server") {
            if (!val.empty()) coord_server_ = val;
        } else if (key == "api_server") {
            if (!val.empty()) api_server_ = val;
        } else if (key == "client_ip_override") {
            client_ip_override_ = val;
        } else if (key == "nickname") {
            if (!val.empty()) nickname_ = val;
        } else if (key == "auth_token") {
            auth_token_ = val;
        } else if (key == "auth_user_id") {
            auth_user_id_ = val;
        } else if (key == "auth_username") {
            auth_username_ = val;
        } else if (key == "auth_display_name") {
            auth_display_name_ = val;
        }
    }
    dirty_ = false;
    LOG_INFO("Config loaded");
}

void Config::Save() {
    if (!dirty_) return;
    ForceSave();
}

void Config::ForceSave() {
    std::string dir = GetConfigDir();
    if (!EnsureDir(dir)) {
        LOG_ERROR("Failed to create config dir");
        return;
    }
#ifndef _WIN32
    mode_t old_umask = umask(0077);
#endif
    std::ofstream f(config_path_);
#ifndef _WIN32
    umask(old_umask);
#endif
    if (!f.is_open()) {
        LOG_ERROR("Failed to save config");
        return;
    }
    f << "# PeerDen config\n";
    f << "auto_connect_tun=" << (auto_connect_tun_ ? "1" : "0") << "\n";
    f << "local_tun_ip=" << local_tun_ip_ << "\n";
    f << "peer_address=" << peer_address_ << "\n";
    f << "peer_port=" << peer_port_ << "\n";
    f << "local_port=" << local_port_ << "\n";
    f << "tun_unit=" << tun_unit_ << "\n";
    f << "coord_server=" << coord_server_ << "\n";
    f << "api_server=" << api_server_ << "\n";
    if (!client_ip_override_.empty()) f << "client_ip_override=" << client_ip_override_ << "\n";
    f << "nickname=" << nickname_ << "\n";
    if (!auth_token_.empty()) {
        f << "auth_token=" << auth_token_ << "\n";
        f << "auth_user_id=" << auth_user_id_ << "\n";
        f << "auth_username=" << auth_username_ << "\n";
        f << "auth_display_name=" << auth_display_name_ << "\n";
    }
    dirty_ = false;
    LOG_DEBUG("Config saved");
}

void Config::SetAutoConnectTun(bool v) {
    if (auto_connect_tun_ != v) {
        auto_connect_tun_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetLocalTunIP(const std::string& v) {
    if (local_tun_ip_ != v) {
        local_tun_ip_ = v.empty() ? "7.0.0.1" : v;
        dirty_ = true;
        Save();
    }
}

void Config::SetPeerAddress(const std::string& v) {
    if (peer_address_ != v) {
        peer_address_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetPeerPort(uint16_t v) {
    if (peer_port_ != v) {
        peer_port_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetLocalPort(uint16_t v) {
    if (local_port_ != v) {
        local_port_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetTunUnit(uint32_t v) {
    if (tun_unit_ != v) {
        tun_unit_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetApiServer(const std::string& v) {
    if (api_server_ != v) {
        api_server_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetCoordServer(const std::string& v) {
    if (coord_server_ != v) {
        coord_server_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetClientIPOverride(const std::string& v) {
    if (client_ip_override_ != v) {
        client_ip_override_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetNickname(const std::string& v) {
    if (nickname_ != v) {
        nickname_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetAuthToken(const std::string& v) {
    if (auth_token_ != v) {
        auth_token_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetAuthUserID(const std::string& v) {
    if (auth_user_id_ != v) {
        auth_user_id_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetAuthUsername(const std::string& v) {
    if (auth_username_ != v) {
        auth_username_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::SetAuthDisplayName(const std::string& v) {
    if (auth_display_name_ != v) {
        auth_display_name_ = v;
        dirty_ = true;
        Save();
    }
}

void Config::ClearAuth() {
    if (!auth_token_.empty() || !auth_username_.empty()) {
        auth_token_.clear();
        auth_user_id_.clear();
        auth_username_.clear();
        auth_display_name_.clear();
        dirty_ = true;
        Save();
    }
}

}  // namespace tunngle
