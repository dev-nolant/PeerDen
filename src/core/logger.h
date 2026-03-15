#pragma once

#include <string>
#include <mutex>
#include <cstdarg>
#include <functional>
#include <vector>
#include <deque>

namespace tunngle {

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Off = 5
};

class Logger {
public:
    static Logger& Instance();

    void Init(const std::string& log_dir = "");
    void Shutdown();

    void SetLevel(LogLevel level) { level_ = level; }
    LogLevel GetLevel() const { return level_; }

    void SetMinFileLevel(LogLevel level) { file_level_ = level; }
    void SetMinConsoleLevel(LogLevel level) { console_level_ = level; }

    const std::string& GetLogPath() const { return log_path_; }
    std::string GetLogDir() const;

    void AddSink(std::function<void(LogLevel, const std::string&)> sink);

    /// In-memory ring buffer for UI. Max 500 lines.
    void GetRecentLogs(std::vector<std::pair<LogLevel, std::string>>& out) const;

    void Log(LogLevel level, const char* fmt, ...);
    void LogV(LogLevel level, const char* fmt, va_list args);

    void Trace(const char* fmt, ...);
    void Debug(const char* fmt, ...);
    void Info(const char* fmt, ...);
    void Warn(const char* fmt, ...);
    void Error(const char* fmt, ...);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Write(LogLevel level, const std::string& msg);
    std::string Format(LogLevel level, const char* fmt, va_list args);
    const char* LevelStr(LogLevel level) const;

    LogLevel level_ = LogLevel::Info;
    LogLevel file_level_ = LogLevel::Debug;
    LogLevel console_level_ = LogLevel::Info;
    std::string log_path_;
    FILE* log_file_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<std::function<void(LogLevel, const std::string&)>> sinks_;
    mutable std::deque<std::pair<LogLevel, std::string>> ring_buffer_;
    static constexpr size_t kMaxRingLines = 500;
};

#define LOG_TRACE(...) tunngle::Logger::Instance().Trace(__VA_ARGS__)
#define LOG_DEBUG(...) tunngle::Logger::Instance().Debug(__VA_ARGS__)
#define LOG_INFO(...)  tunngle::Logger::Instance().Info(__VA_ARGS__)
#define LOG_WARN(...)  tunngle::Logger::Instance().Warn(__VA_ARGS__)
#define LOG_ERROR(...) tunngle::Logger::Instance().Error(__VA_ARGS__)

}  // namespace tunngle
