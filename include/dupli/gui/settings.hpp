#pragma once

#include <string>
#include <map>
#include <fstream>
#include "dupli/gui/platform_paths.hpp"

namespace dupli {
namespace gui {

class Settings {
public:
    static Settings& getInstance();
    
    void load();
    void save();
    void resetToDefaults();
    
    // General
    bool dark_theme = true;
    int concurrency_limit = 1; // Single task per tab
    bool confirmations_enabled = true;
    
    // Scanning
    bool show_hidden_entries = false;
    bool follow_filesystem_boundaries = false;
    size_t hash_buffer_size = 65536;
    bool digest_verification = true;
    
    // Logging
    bool logging_enabled = false;
    std::string log_level = "Info";
    size_t log_max_file_size = 10485760; // 10MB
    int log_max_files = 5;
    
private:
    Settings();
    std::string get_settings_path();
};

} // namespace gui
} // namespace dupli
