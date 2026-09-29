#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <cstdarg>
#include <windows.h>

namespace DynamicBanners {

enum class LogLevel {
    None = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5
};

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    // Nothing is written (and no file is created) until a message passes the level; LogLevel::None never
    // creates dynamic_banners.log.
    void Initialize(const std::wstring& log_dir, LogLevel level = LogLevel::None);
    void Shutdown();

    void Log(LogLevel level, const char* format, ...);
    void LogV(LogLevel level, const char* format, va_list args);

    void SetLevel(LogLevel level) { log_level_ = level; }
    LogLevel GetLevel() const { return log_level_; }

private:
    Logger() : log_level_(LogLevel::None), initialized_(false) {}
    void OpenFile();
    ~Logger() { Shutdown(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::wstring log_dir_;
    std::ofstream log_file_;
    std::mutex mutex_;
    LogLevel log_level_;
    bool initialized_;
};

#define LOG_ERROR(fmt, ...) DynamicBanners::Logger::Instance().Log(DynamicBanners::LogLevel::Error, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  DynamicBanners::Logger::Instance().Log(DynamicBanners::LogLevel::Warn,  fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  DynamicBanners::Logger::Instance().Log(DynamicBanners::LogLevel::Info,  fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) DynamicBanners::Logger::Instance().Log(DynamicBanners::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) DynamicBanners::Logger::Instance().Log(DynamicBanners::LogLevel::Trace, fmt, ##__VA_ARGS__)

} // namespace DynamicBanners
