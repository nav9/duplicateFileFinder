#include "dupli/gui/logger.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dupli {
namespace gui {

namespace {

constexpr const char* kLogFileName = "app.log";

int level_rank(LogLevel level) { return static_cast<int>(level); }

} // namespace

std::string to_string(LogLevel level) {
    switch (level) {
    case LogLevel::Trace: return "Trace";
    case LogLevel::Debug: return "Debug";
    case LogLevel::Info: return "Info";
    case LogLevel::Warning: return "Warning";
    case LogLevel::Error: return "Error";
    case LogLevel::Critical: return "Critical";
    }
    return "Info";
}

bool parse_log_level(const std::string& text, LogLevel& out) {
    if (text == "Trace") { out = LogLevel::Trace; return true; }
    if (text == "Debug") { out = LogLevel::Debug; return true; }
    if (text == "Info") { out = LogLevel::Info; return true; }
    if (text == "Warning") { out = LogLevel::Warning; return true; }
    if (text == "Error") { out = LogLevel::Error; return true; }
    if (text == "Critical") { out = LogLevel::Critical; return true; }
    return false;
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() { reload_settings(); }

std::string Logger::log_path() const {
    return (PlatformPaths::get_data_dir() / kLogFileName).string();
}

void Logger::reload_settings() {
    std::lock_guard<std::mutex> lock(m_mutex);
    Settings& settings = Settings::getInstance();
    const bool was_enabled = m_enabled;
    m_enabled = settings.logging_enabled;
    LogLevel level = LogLevel::Info;
    if (parse_log_level(settings.log_level, level)) m_minimumLevel = level;
    m_maxFileSize = settings.log_max_file_size == 0 ? 1 : settings.log_max_file_size;
    m_maxFiles = settings.log_max_files < 1 ? 1 : settings.log_max_files;

    if (m_enabled && !m_log_file.is_open()) open_locked();
    if (!m_enabled && was_enabled && m_log_file.is_open()) {
        // Turning logging off closes the file; the operation journal is a
        // separate mechanism and is unaffected.
        m_log_file.close();
    }
}

void Logger::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_enabled = enabled;
    if (enabled && !m_log_file.is_open()) open_locked();
    if (!enabled && m_log_file.is_open()) m_log_file.close();
}

void Logger::open_locked() {
    PlatformPaths::ensure_directories_exist();
    m_log_file.open(log_path(), std::ios::app);
}

void Logger::rotate_locked() {
    m_log_file.close();
    const std::filesystem::path base(log_path());
    std::error_code error;
    // Shift the oldest file out first so app.log.1 is always free for the
    // current file. A failed rename is not fatal: logging simply continues in a
    // new file and the old one keeps its name.
    const std::filesystem::path oldest = base.string() + "." + std::to_string(m_maxFiles);
    std::filesystem::remove(oldest, error);
    for (int index = m_maxFiles - 1; index >= 1; --index) {
        const std::filesystem::path from = base.string() + "." + std::to_string(index);
        const std::filesystem::path to = base.string() + "." + std::to_string(index + 1);
        error.clear();
        if (!std::filesystem::exists(from, error)) continue;
        std::filesystem::rename(from, to, error);
    }
    error.clear();
    if (std::filesystem::exists(base, error)) {
        std::filesystem::rename(base, base.string() + ".1", error);
    }
    open_locked();
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!m_enabled) return;
    // Filtering happens before taking the lock and before touching the disk.
    if (level_rank(level) < level_rank(m_minimumLevel)) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_enabled) return;
    if (!m_log_file.is_open()) open_locked();
    if (!m_log_file.is_open()) return;

    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::ostringstream line;
    line << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << " ["
         << to_string(level) << "] " << message << '\n';
    const std::string text = line.str();

    // Check the projected size before writing so a single large entry can never
    // blow past the configured limit by much.
    std::error_code error;
    const auto current_size = std::filesystem::file_size(log_path(), error);
    if (!error && current_size + text.size() > m_maxFileSize) rotate_locked();

    if (m_log_file.is_open()) {
        m_log_file << text;
        m_log_file.flush();
    }
}

bool Logger::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_log_file.is_open()) m_log_file.close();
    std::error_code error;
    // Truncate only the current file; rotated history is left alone so a clear
    // action does not hide earlier diagnostics by accident.
    std::filesystem::resize_file(log_path(), 0, error);
    if (m_enabled) open_locked();
    return !error;
}

std::string read_log_tail(const std::string& path, size_t max_bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::string();

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0) return std::string();
    const std::streamoff start =
        size > static_cast<std::streamoff>(max_bytes) ? size - static_cast<std::streamoff>(max_bytes) : 0;
    file.seekg(start, std::ios::beg);

    std::string content;
    content.resize(static_cast<size_t>(size - start));
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<size_t>(file.gcount()));

    // A partial first line is dropped: it would look like a truncated record.
    if (start > 0) {
        const size_t newline = content.find('\n');
        if (newline == std::string::npos) return std::string();
        content.erase(0, newline + 1);
    }
    return content;
}

} // namespace gui
} // namespace dupli
