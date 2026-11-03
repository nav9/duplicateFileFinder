#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include "dupli/gui/platform_paths.hpp"
#include "dupli/gui/settings.hpp"

namespace dupli {
namespace gui {

enum class LogLevel { Trace, Debug, Info, Warning, Error, Critical };

std::string to_string(LogLevel level);
bool parse_log_level(const std::string& text, LogLevel& out);

// Diagnostic application log.
//
// This is NOT the operation journal. The journal is what makes undo possible and
// is always written; this log can be disabled entirely without affecting safety.
// It never records file contents, secrets or repository credentials.
//
// Writes are serialised by a mutex because tasks run on worker threads. Rotation
// is size based: when app.log would exceed the configured size it is renamed to
// app.log.1 (older files shift up) and a new file is started, so the log cannot
// grow without bound.
class Logger {
public:
    static Logger& getInstance();

    void log(LogLevel level, const std::string& message);
    void setEnabled(bool enabled);

    /// Re-read every logging setting (enabled, level, size, retention) from the
    /// settings file. Called after the Settings tab saves.
    void reload_settings();

    /// Truncate the current log file. The UI asks for confirmation first and
    /// explains that this is unrelated to the operation journal.
    bool clear();

    bool enabled() const { return m_enabled; }
    LogLevel minimum_level() const { return m_minimumLevel; }
    std::string log_path() const;

private:
    Logger();
    void open_locked();
    void rotate_locked();

    std::ofstream m_log_file;
    std::mutex m_mutex;
    bool m_enabled = false;
    LogLevel m_minimumLevel = LogLevel::Info;
    size_t m_maxFileSize = 10 * 1024 * 1024;
    int m_maxFiles = 5;
};

/// Read at most the last `max_bytes` of a log file, as complete lines. Bounded
/// so a multi-gigabyte log can never be loaded into the viewer by accident.
std::string read_log_tail(const std::string& path, size_t max_bytes);

} // namespace gui
} // namespace dupli
