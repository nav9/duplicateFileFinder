#include "dupli/gui/settings.hpp"
#include <fstream>
#include <sstream>

namespace dupli {
namespace gui {

Settings& Settings::getInstance() {
    static Settings instance;
    return instance;
}

Settings::Settings() {
    load();
}

std::string Settings::get_settings_path() {
    return (PlatformPaths::get_config_dir() / "settings.json").string();
}

void Settings::load() {
    try {
        std::ifstream file(get_settings_path());
        if (!file.is_open()) {
            resetToDefaults();
            return;
        }
        // Simple key-value parsing
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("dark_theme") != std::string::npos && line.find("false") != std::string::npos) {
                dark_theme = false;
            }
            if (line.find("show_hidden_entries") != std::string::npos && line.find("true") != std::string::npos) {
                show_hidden_entries = true;
            }
            if (line.find("logging_enabled") != std::string::npos && line.find("true") != std::string::npos) {
                logging_enabled = true;
            }
        }
    } catch (...) {
        resetToDefaults();
    }
}

void Settings::save() {
    try {
        PlatformPaths::ensure_directories_exist();
        std::ofstream file(get_settings_path());
        if (file.is_open()) {
            file << "{\n";
            file << "  \"dark_theme\": " << (dark_theme ? "true" : "false") << ",\n";
            file << "  \"show_hidden_entries\": " << (show_hidden_entries ? "true" : "false") << ",\n";
            file << "  \"follow_filesystem_boundaries\": " << (follow_filesystem_boundaries ? "true" : "false") << ",\n";
            file << "  \"hash_buffer_size\": " << hash_buffer_size << ",\n";
            file << "  \"logging_enabled\": " << (logging_enabled ? "true" : "false") << ",\n";
            file << "  \"log_level\": \"" << log_level << "\"\n";
            file << "}\n";
        }
    } catch (...) {
    }
}

void Settings::resetToDefaults() {
    dark_theme = true;
    concurrency_limit = 1;
    confirmations_enabled = true;
    show_hidden_entries = false;
    follow_filesystem_boundaries = false;
    hash_buffer_size = 65536;
    digest_verification = true;
    logging_enabled = false;
    log_level = "Info";
    log_max_file_size = 10485760;
    log_max_files = 5;
}

} // namespace gui
} // namespace dupli
