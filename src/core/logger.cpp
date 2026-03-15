#include "logger.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
#include <direct.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

namespace tunngle {

namespace {

std::string GetConfigDir() {
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
        return std::string(home) + "/.config/peerdden";
    }
#ifdef __APPLE__
    struct passwd* pw = getpwuid(getuid());
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
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

}  // namespace

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Init(const std::string& log_dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_) return;

    std::string dir = log_dir.empty() ? GetConfigDir() : log_dir;
    if (!EnsureDir(dir)) {
        fprintf(stderr, "[Logger] Failed to create log dir: %s\n", dir.c_str());
        return;
    }

    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char name[128];
    snprintf(name, sizeof(name), "peerdden_%04d-%02d-%02d.log",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

    log_path_ = dir + "/" + name;
    log_file_ = fopen(log_path_.c_str(), "a");
    if (!log_file_) {
        fprintf(stderr, "[Logger] Failed to open log file: %s\n", log_path_.c_str());
        return;
    }
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);
#ifdef _WIN32
    fprintf(log_file_, "=== PeerDen v1.1.0 [Windows] %s ===\n", time_buf);
#elif defined(__APPLE__)
    fprintf(log_file_, "=== PeerDen v1.1.0 [macOS] %s ===\n", time_buf);
#else
    fprintf(log_file_, "=== PeerDen v1.1.0 [Linux] %s ===\n", time_buf);
#endif
    fflush(log_file_);
}

std::string Logger::GetLogDir() const {
    if (log_path_.empty()) return "";
    size_t pos = log_path_.find_last_of("/\\");
    if (pos == std::string::npos) return "";
    return log_path_.substr(0, pos);
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (log_file_) {
        fclose(log_file_);
        log_file_ = nullptr;
    }
}

void Logger::AddSink(std::function<void(LogLevel, const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

const char* Logger::LevelStr(LogLevel level) const {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        default: return "?????";
    }
}

std::string Logger::Format(LogLevel level, const char* fmt, va_list args) {
    char buf[4096];
    va_list args_copy;
    va_copy(args_copy, args);
    int n = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);
    if (n < 0) return "";
    if (n >= static_cast<int>(sizeof(buf))) n = sizeof(buf) - 1;

    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);

    char out[4192];
    snprintf(out, sizeof(out), "[%s] [%s] %s", time_buf, LevelStr(level), buf);
    return out;
}

void Logger::Write(LogLevel level, const std::string& msg) {
    ring_buffer_.emplace_back(level, msg);
    if (ring_buffer_.size() > kMaxRingLines) {
        ring_buffer_.pop_front();
    }

    if (level < level_) return;

    if (level >= console_level_) {
        fprintf(stderr, "%s\n", msg.c_str());
    }
    if (log_file_ && level >= file_level_) {
        fprintf(log_file_, "%s\n", msg.c_str());
        fflush(log_file_);
    }
    for (auto& sink : sinks_) {
        sink(level, msg);
    }
}

void Logger::GetRecentLogs(std::vector<std::pair<LogLevel, std::string>>& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    out.assign(ring_buffer_.begin(), ring_buffer_.end());
}

void Logger::LogV(LogLevel level, const char* fmt, va_list args) {
    std::string msg = Format(level, fmt, args);
    if (msg.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    Write(level, msg);
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(level, fmt, args);
    va_end(args);
}

void Logger::Trace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Trace, fmt, args);
    va_end(args);
}

void Logger::Debug(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Debug, fmt, args);
    va_end(args);
}

void Logger::Info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Info, fmt, args);
    va_end(args);
}

void Logger::Warn(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Warn, fmt, args);
    va_end(args);
}

void Logger::Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Error, fmt, args);
    va_end(args);
}

}  // namespace tunngle
