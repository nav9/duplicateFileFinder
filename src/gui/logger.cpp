#include "dupli/gui/logger.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace dupli {
namespace gui {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() : m_enabled(false) {
    m_enabled = Settings::getInstance().logging_enabled;
    if (m_enabled) {
        PlatformPaths::ensure_directories_exist();
        m_log_file.open(get_log_path(), std::ios::app);
    }
}

std::string Logger::get_log_path() {
    auto path = PlatformPaths::get_data_dir() / "app.log";
    return path.string();
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!m_enabled) return;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_log_file.is_open()) return;
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << " ";
    
    switch (level) {
        case LogLevel::Trace: ss << "[TRACE] "; break;
        case LogLevel::Debug: ss << "[DEBUG] "; break;
        case LogLevel::Info: ss << "[INFO] "; break;
        case LogLevel::Warning: ss << "[WARN] "; break;
        case LogLevel::Error: ss << "[ERROR] "; break;
        case LogLevel::Critical: ss << "[CRITICAL] "; break;
    }
    
    ss << message << std::endl;
    m_log_file << ss.str();
    m_log_file.flush();
}

void Logger::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (enabled && !m_log_file.is_open()) {
        m_log_file.open(get_log_path(), std::ios::app);
    }
}

} // namespace gui
} // namespace dupli
