#include "logger.h"
#include "version.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace DynamicBanners {

void Logger::Initialize(const std::wstring& log_dir, LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) return;

    log_level_ = level;
    std::wstring log_path = log_dir + L"\\dynamic_banners.log";
    log_file_.open(log_path, std::ios::out | std::ios::trunc);

    if (log_file_.is_open()) {
        initialized_ = true;
        
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        localtime_s(&tm_buf, &now_c);

        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

        log_file_ << "======================================================================\n";
        log_file_ << "  Dynamic Banners-N-Flags v" DYNAMIC_BANNERS_VERSION " (built for " DYNAMIC_BANNERS_GAME_VERSION ")\n";
        log_file_ << "  Session Started: " << time_str << "\n";
        log_file_ << "======================================================================\n" << std::flush;
    }
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_ && log_file_.is_open()) {
        log_file_ << "----------------------------------------------------------------------\n";
        log_file_ << "  Session Ended cleanly.\n";
        log_file_ << "======================================================================\n" << std::flush;
        log_file_.close();
    }
    initialized_ = false;
}

void Logger::Log(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogV(level, format, args);
    va_end(args);
}

void Logger::LogV(LogLevel level, const char* format, va_list args) {
    if (level > log_level_) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !log_file_.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto now_c = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_s(&tm_buf, &now_c);

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &tm_buf);

    const char* level_str = "INFO ";
    switch (level) {
        case LogLevel::Error: level_str = "ERROR"; break;
        case LogLevel::Warn:  level_str = "WARN "; break;
        case LogLevel::Info:  level_str = "INFO "; break;
        case LogLevel::Debug: level_str = "DEBUG"; break;
        case LogLevel::Trace: level_str = "TRACE"; break;
        default: break;
    }

    char message_buf[2048];
    vsnprintf(message_buf, sizeof(message_buf), format, args);

    log_file_ << "[" << time_str << "." << std::setfill('0') << std::setw(3) << ms.count() 
              << "] [" << level_str << "] " << message_buf << "\n" << std::flush;

    // Also mirror to Windows debug output (visible in DebugView)
    char dbg_buf[2200];
    snprintf(dbg_buf, sizeof(dbg_buf), "[DynamicBanners] [%s] %s\n", level_str, message_buf);
    OutputDebugStringA(dbg_buf);
}

} // namespace DynamicBanners
