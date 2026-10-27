#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include "dupli/gui/platform_paths.hpp"
#include "dupli/gui/settings.hpp"

namespace dupli {
namespace gui {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical
};

class Logger {
public:
    static Logger& getInstance();
    
    void log(LogLevel level, const std::string& message);
    void setEnabled(bool enabled);
    
private:
    Logger();
    std::string get_log_path();
    void rotate_if_needed();
    
    std::ofstream m_log_file;
    std::mutex m_mutex;
    bool m_enabled;
};

} // namespace gui
} // namespace dupli
