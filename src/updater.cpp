#include "updater.h"
#include "core/version.h"
#include <httplib.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace tunngle {

namespace {

std::atomic<UpdateStatus> g_status{UpdateStatus::Idle};
std::atomic<float> g_progress{0.0f};
std::string g_error;
UpdateInfo g_info;
std::mutex g_mutex;

bool ParseVersion(const std::string& s, int& major, int& minor, int& patch) {
    major = minor = patch = 0;
    size_t i = 0;
    if (i < s.size() && s[i] == 'v') i++;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    major = std::atoi(s.c_str() + i);
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
    if (i >= s.size() || s[i] != '.') return false;
    i++;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    minor = std::atoi(s.c_str() + i);
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) i++;
    if (i >= s.size() || s[i] != '.') return false;
    i++;
    if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    patch = std::atoi(s.c_str() + i);
    return true;
}

bool IsNewer(const std::string& latest, const std::string& current) {
    int lmaj, lmin, lpat, cmaj, cmin, cpat;
    if (!ParseVersion(latest, lmaj, lmin, lpat) || !ParseVersion(current, cmaj, cmin, cpat))
        return false;
    if (lmaj > cmaj) return true;
    if (lmaj < cmaj) return false;
    if (lmin > cmin) return true;
    if (lmin < cmin) return false;
    return lpat > cpat;
}

std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    size_t start = pos + 1;
    pos = start;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\') pos++;
        pos++;
    }
    if (pos >= json.size()) return "";
    std::string out;
    for (size_t i = start; i < pos; i++) {
        if (json[i] == '\\' && i + 1 < pos) { i++; out += json[i]; }
        else out += json[i];
    }
    return out;
}

std::string FindExeDownloadUrl(const std::string& json) {
    size_t assets_pos = json.find("\"assets\"");
    if (assets_pos == std::string::npos) return "";
    size_t arr_start = json.find('[', assets_pos);
    if (arr_start == std::string::npos) return "";
    int depth = 0;
    size_t i = arr_start + 1;
    while (i < json.size()) {
        if (json[i] == '{') {
            depth++;
            if (depth == 1) {
                size_t obj_end = i;
                int d = 0;
                for (size_t j = i; j < json.size(); j++) {
                    if (json[j] == '{') d++;
                    else if (json[j] == '}') { d--; if (d == 0) { obj_end = j; break; } }
                }
                std::string obj = json.substr(i, obj_end - i + 1);
                std::string name = ExtractJsonString(obj, "name");
                if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".exe") == 0) {
                    std::string url = ExtractJsonString(obj, "browser_download_url");
                    if (!url.empty()) return url;
                }
            }
        } else if (json[i] == '}') depth--;
        else if (json[i] == ']' && depth == 0) break;
        i++;
    }
    return "";
}

void ParseHostPath(const std::string& url, std::string& host, int& port, std::string& path, bool& use_ssl) {
    host.clear();
    path.clear();
    port = 443;
    use_ssl = true;
    size_t i = 0;
    if (url.compare(0, 8, "https://") == 0) { i = 8; use_ssl = true; port = 443; }
    else if (url.compare(0, 7, "http://") == 0) { i = 7; use_ssl = false; port = 80; }
    else return;
    size_t slash = url.find('/', i);
    if (slash == std::string::npos) {
        host = url.substr(i);
        path = "/";
        return;
    }
    host = url.substr(i, slash - i);
    path = url.substr(slash);
    size_t colon = host.find(':');
    if (colon != std::string::npos) {
        port = std::atoi(host.substr(colon + 1).c_str());
        host = host.substr(0, colon);
    }
}

void CheckThread() {
    g_status = UpdateStatus::Checking;
    g_error.clear();
    g_progress = 0.0f;

    httplib::SSLClient cli("api.github.com", 443);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(10);
    cli.set_follow_location(true);
    httplib::Headers hdrs = {{"User-Agent", "PeerDen-Updater"}};
    auto res = cli.Get("/repos/dev-nolant/PeerDen/releases/latest", hdrs);

    if (!res) {
        g_status = UpdateStatus::Error;
        g_error = "Could not connect to GitHub";
        return;
    }
    if (res->status != 200) {
        g_status = UpdateStatus::Error;
        g_error = "GitHub API returned " + std::to_string(res->status);
        return;
    }

    std::string tag = ExtractJsonString(res->body, "tag_name");
    if (tag.empty()) {
        g_status = UpdateStatus::Error;
        g_error = "Could not parse release info";
        return;
    }
    std::string version = (tag.size() > 1 && tag[0] == 'v') ? tag.substr(1) : tag;

    if (!IsNewer(version, PEERDDEN_VERSION)) {
        g_status = UpdateStatus::UpToDate;
        return;
    }

    std::string download_url = FindExeDownloadUrl(res->body);
    if (download_url.empty()) {
        g_status = UpdateStatus::Error;
        g_error = "No Windows installer found in release";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_info.version = version;
        g_info.download_url = download_url;
        g_info.local_path.clear();
    }
    g_status = UpdateStatus::UpdateAvailable;
}

void DownloadThread() {
    g_status = UpdateStatus::Downloading;
    g_error.clear();
    g_progress = 0.0f;

    std::string url;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        url = g_info.download_url;
    }
    if (url.empty()) {
        g_status = UpdateStatus::Error;
        g_error = "No download URL";
        return;
    }

    std::string host, path;
    int port;
    bool use_ssl;
    ParseHostPath(url, host, port, path, use_ssl);
    if (host.empty()) {
        g_status = UpdateStatus::Error;
        g_error = "Invalid download URL";
        return;
    }

#ifdef _WIN32
    char temp_path[MAX_PATH];
    if (GetTempPathA(sizeof(temp_path), temp_path) == 0) {
        g_status = UpdateStatus::Error;
        g_error = "Could not get temp path";
        return;
    }
    std::string local = std::string(temp_path) + "PeerDen-Setup-Update.exe";
#else
    std::string local = "/tmp/PeerDen-Setup-Update.exe";
#endif

    httplib::Result res;
    if (use_ssl) {
        httplib::SSLClient scli(host, port);
        scli.set_connection_timeout(10);
        scli.set_read_timeout(120);
        scli.set_follow_location(true);
        res = scli.Get(path);
    } else {
        httplib::Client cli(host.c_str(), port);
        cli.set_connection_timeout(10);
        cli.set_read_timeout(120);
        cli.set_follow_location(true);
        res = cli.Get(path);
    }
    if (!res || res->status != 200) {
        g_status = UpdateStatus::Error;
        g_error = res ? ("Download failed: " + std::to_string(res->status)) : "Download failed";
        return;
    }

    std::ofstream out(local, std::ios::binary);
    if (!out) {
        g_status = UpdateStatus::Error;
        g_error = "Could not create temp file";
        return;
    }
    out.write(res->body.data(), res->body.size());
    out.close();

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_info.local_path = local;
    }
    g_progress = 1.0f;
    g_status = UpdateStatus::DownloadComplete;
}

}  // namespace

void UpdaterCheckAsync() {
    if (g_status != UpdateStatus::Idle && g_status != UpdateStatus::UpToDate &&
        g_status != UpdateStatus::UpdateAvailable && g_status != UpdateStatus::Error)
        return;
    std::thread(CheckThread).detach();
}

void UpdaterDownloadAsync() {
    if (g_status != UpdateStatus::UpdateAvailable) return;
    std::thread(DownloadThread).detach();
}

void UpdaterInstallAndExit() {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        path = g_info.local_path;
    }
    if (path.empty()) return;

#ifdef _WIN32
    ShellExecuteA(nullptr, "open", path.c_str(), "/VERYSILENT", nullptr, SW_SHOWNORMAL);
#endif
    std::exit(0);
}

UpdateStatus UpdaterGetStatus() { return g_status.load(); }
std::string UpdaterGetError() { return g_error; }
UpdateInfo UpdaterGetInfo() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_info;
}
float UpdaterGetProgress() { return g_progress.load(); }

}  // namespace tunngle
