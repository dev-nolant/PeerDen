#define GL_SILENCE_DEPRECATION
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "texture_cache.h"

#include <httplib.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tunngle {

namespace {

struct DownloadRequest {
    std::string url;
};

struct DownloadResult {
    std::string url;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0, channels = 0;
};

constexpr int kMaxWorkers = 2;
constexpr size_t kMaxCacheEntries = 200;

constexpr size_t kMaxImageBytes = 8 * 1024 * 1024;   // 8 MB
constexpr int kMaxImageDimension = 4096;

std::string g_api_base;
std::mutex g_queue_mu;
std::condition_variable g_queue_cv;
std::deque<DownloadRequest> g_queue;
std::atomic<bool> g_shutdown{false};
std::vector<std::thread> g_workers;

std::mutex g_result_mu;
std::deque<DownloadResult> g_results;

std::unordered_map<std::string, TexID> g_cache;
std::unordered_map<std::string, bool> g_pending;

TexID g_logo_tex = 0;

static std::string GetExecutableDir() {
#ifdef _WIN32
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (len == 0) return ".";
    std::string s(path, len);
    size_t slash = s.find_last_of("\\/");
    return (slash != std::string::npos) ? s.substr(0, slash + 1) : ".";
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return ".";
    std::string s(path);
    size_t slash = s.find_last_of('/');
    return (slash != std::string::npos) ? s.substr(0, slash + 1) : ".";
#else
    char path[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n <= 0) return ".";
    path[n] = '\0';
    std::string s(path);
    size_t slash = s.find_last_of('/');
    return (slash != std::string::npos) ? s.substr(0, slash + 1) : ".";
#endif
}

void WorkerLoop() {
    while (!g_shutdown.load()) {
        DownloadRequest req;
        {
            std::unique_lock<std::mutex> lk(g_queue_mu);
            g_queue_cv.wait_for(lk, std::chrono::milliseconds(200), [] {
                return !g_queue.empty() || g_shutdown.load();
            });
            if (g_shutdown.load()) return;
            if (g_queue.empty()) continue;
            req = std::move(g_queue.front());
            g_queue.pop_front();
        }

        DownloadResult res;
        res.url = req.url;

        std::string fetch_url = g_api_base + "/api/games/image?url=" + req.url;

        httplib::Client cli(g_api_base);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(15);

        std::string path = "/api/games/image?url=" + req.url;
        auto http_res = cli.Get(path);

        if (http_res && http_res->status == 200 && !http_res->body.empty()) {
            const size_t body_size = http_res->body.size();
            if (body_size <= kMaxImageBytes) {
                int w = 0, h = 0, comp = 0;
                const unsigned char* buf = reinterpret_cast<const unsigned char*>(http_res->body.data());
                if (stbi_info_from_memory(buf, static_cast<int>(body_size), &w, &h, &comp) &&
                    w > 0 && h > 0 && w <= kMaxImageDimension && h <= kMaxImageDimension) {
                    res.pixels = stbi_load_from_memory(buf, static_cast<int>(body_size),
                                                       &res.w, &res.h, &res.channels, 4);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lk(g_result_mu);
            g_results.push_back(std::move(res));
        }
    }
}

}  // namespace

void TextureCacheInit(const std::string& api_base_url) {
    g_api_base = api_base_url;
    g_shutdown.store(false);
    for (int i = 0; i < kMaxWorkers; i++) {
        g_workers.emplace_back(WorkerLoop);
    }
}

void TextureCacheShutdown() {
    g_shutdown.store(true);
    g_queue_cv.notify_all();
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }
    g_workers.clear();

    if (g_logo_tex > 0) {
        GLuint id = static_cast<GLuint>(g_logo_tex);
        glDeleteTextures(1, &id);
        g_logo_tex = 0;
    }

    for (auto& [url, tex] : g_cache) {
        if (tex > 0) {
            GLuint id = static_cast<GLuint>(tex);
            glDeleteTextures(1, &id);
        }
    }
    g_cache.clear();
    g_pending.clear();

    std::lock_guard<std::mutex> lk(g_result_mu);
    for (auto& r : g_results) {
        if (r.pixels) stbi_image_free(r.pixels);
    }
    g_results.clear();
}

void TextureCachePump() {
    std::lock_guard<std::mutex> lk(g_result_mu);

    while (!g_results.empty()) {
        DownloadResult res = std::move(g_results.front());
        g_results.pop_front();

        if (res.pixels && res.w > 0 && res.h > 0) {
            GLuint tex;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, res.w, res.h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, res.pixels);
            glBindTexture(GL_TEXTURE_2D, 0);

            g_cache[res.url] = static_cast<TexID>(tex);
            stbi_image_free(res.pixels);
        } else {
            g_cache[res.url] = 0;
            if (res.pixels) stbi_image_free(res.pixels);
        }
        g_pending.erase(res.url);
    }
}

TexID TextureCacheGet(const std::string& rawg_thumb_url) {
    if (rawg_thumb_url.empty()) return 0;

    auto it = g_cache.find(rawg_thumb_url);
    if (it != g_cache.end()) return it->second;

    if (g_pending.count(rawg_thumb_url)) return 0;

    if (g_cache.size() >= kMaxCacheEntries) return 0;

    g_pending[rawg_thumb_url] = true;
    {
        std::lock_guard<std::mutex> lk(g_queue_mu);
        g_queue.push_back({rawg_thumb_url});
    }
    g_queue_cv.notify_one();

    return 0;
}

TexID TextureCacheLoadLocal(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (f && f.tellg() > static_cast<std::streampos>(kMaxImageBytes)) {
        return 0;
    }
    f.close();

    int w = 0, h = 0, channels = 0;
    if (stbi_info(path.c_str(), &w, &h, &channels)) {
        if (w <= 0 || h <= 0 || w > kMaxImageDimension || h > kMaxImageDimension) {
            return 0;
        }
    }
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return 0;
    }

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    return static_cast<TexID>(tex);
}

TexID TextureCacheGetLogo() {
    if (g_logo_tex > 0) return g_logo_tex;

    std::string exe_dir = GetExecutableDir();
#ifdef _WIN32
    std::string path = exe_dir + "logo.png";
#else
    std::string path = exe_dir + "logo.png";
#endif
    g_logo_tex = TextureCacheLoadLocal(path);
    return g_logo_tex;
}

}  // namespace tunngle
