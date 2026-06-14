#include "logger.h"

namespace haihunhou {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

void Logger::init(const std::string& log_file, LogLevel level,
                  size_t max_file_size_mb, size_t max_files) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    initialized_ = true;

#ifdef HAIHUNHOU_HAVE_SPDLOG
    try {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        if (!log_file.empty()) {
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file, max_file_size_mb * 1024 * 1024, max_files));
        }
        spd_logger_ = std::make_shared<spdlog::logger>("haihunhou", sinks.begin(), sinks.end());
        spd_logger_->set_level(toSpdlogLevel(level));
        spd_logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [t:%t] %v");
        spd_logger_->flush_on(spdlog::level::warn);
        return;
    } catch (...) {
        // fall through to fallback
    }
#else
    (void)max_file_size_mb;
    (void)max_files;
#endif

    if (!log_file.empty()) {
        file_stream_.open(log_file, std::ios::app);
    }
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
#ifdef HAIHUNHOU_HAVE_SPDLOG
    if (spd_logger_) {
        spd_logger_->set_level(toSpdlogLevel(level));
    }
#endif
}

void Logger::logRaw(LogLevel level, const std::string& msg) {
    if (level < level_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    logImpl(level, "%s", msg.c_str());
}

void Logger::logImpl(LogLevel level, const char* fmt) {
    while (*fmt) {
        ss_ << *fmt++;
    }
    std::string msg = ss_.str();
    ss_.str("");

    std::string line = "[" + timestamp() + "] [" + levelToString(level) + "] " + msg;

    std::cout << line << std::endl;
    if (file_stream_.is_open()) {
        file_stream_ << line << std::endl;
    }

#ifdef HAIHUNHOU_HAVE_SPDLOG
    if (spd_logger_) {
        spd_logger_->log(toSpdlogLevel(level), msg);
    }
#endif
}

std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return "TRACE";
        case LogLevel::DEBUG:    return "DEBUG";
        case LogLevel::INFO:     return "INFO ";
        case LogLevel::WARN:     return "WARN ";
        case LogLevel::ERROR:    return "ERROR";
        case LogLevel::CRITICAL: return "CRIT ";
        default:                 return "?????";
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
       << "." << std::setw(3) << std::setfill('0') << ms.count();
    return ss.str();
}

#ifdef HAIHUNHOU_HAVE_SPDLOG
spdlog::level::level_enum toSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE:    return spdlog::level::trace;
        case LogLevel::DEBUG:    return spdlog::level::debug;
        case LogLevel::INFO:     return spdlog::level::info;
        case LogLevel::WARN:     return spdlog::level::warn;
        case LogLevel::ERROR:    return spdlog::level::err;
        case LogLevel::CRITICAL: return spdlog::level::critical;
        default:                 return spdlog::level::info;
    }
}
#endif

}
