#pragma once

#include <string>
#include <memory>
#include <mutex>
#include <sstream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

#if __has_include(<spdlog/spdlog.h>)
#define HAIHUNHOU_HAVE_SPDLOG 1
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#endif

namespace haihunhou {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    CRITICAL = 5
};

class Logger {
public:
    static Logger& instance();

    void init(const std::string& log_file = "", LogLevel level = LogLevel::INFO,
              size_t max_file_size_mb = 100, size_t max_files = 10);

    void setLevel(LogLevel level);
    LogLevel getLevel() const { return level_; }

    template<typename... Args>
    void log(LogLevel level, const char* fmt, Args&&... args) {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        logImpl(level, fmt, std::forward<Args>(args)...);
    }

    void logRaw(LogLevel level, const std::string& msg);

private:
    Logger() = default;
    ~Logger();

    void logImpl(LogLevel level, const char* fmt);

    template<typename T, typename... Rest>
    void logImpl(LogLevel level, const char* fmt, T&& val, Rest&&... rest) {
        while (*fmt) {
            if (*fmt == '{' && *(fmt + 1) == '}') {
                ss_ << val;
                logImpl(level, fmt + 2, std::forward<Rest>(rest)...);
                return;
            }
            ss_ << *fmt++;
        }
    }

    static std::string levelToString(LogLevel level);
    static std::string timestamp();

    std::mutex mutex_;
    LogLevel level_ = LogLevel::INFO;
    std::ofstream file_stream_;
    std::ostringstream ss_;
    bool initialized_ = false;

#ifdef HAIHUNHOU_HAVE_SPDLOG
    std::shared_ptr<spdlog::logger> spd_logger_;
#endif
};

#define LOG_TRACE(...)    ::haihunhou::Logger::instance().log(::haihunhou::LogLevel::TRACE, __VA_ARGS__)
#define LOG_DEBUG(...)    ::haihunhou::Logger::instance().log(::haihunhou::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)     ::haihunhou::Logger::instance().log(::haihunhou::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARN(...)     ::haihunhou::Logger::instance().log(::haihunhou::LogLevel::WARN, __VA_ARGS__)
#define LOG_ERROR(...)    ::haihunhou::Logger::instance().log(::haihunhou::LogLevel::ERROR, __VA_ARGS__)
#define LOG_CRITICAL(...) ::haihunhou::Logger::instance().log(::haihunhou::LogLevel::CRITICAL, __VA_ARGS__)

}
