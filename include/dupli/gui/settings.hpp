#pragma once

#include <string>
#include <fstream>

#include "dupli/gui/platform_paths.hpp"

namespace dupli {
namespace gui {

// Versioned, atomically saved user settings. Values are validated on load so a
// hand-edited or corrupted file can never produce an unsafe configuration (for
// example a zero hash buffer or a negative retention count).
class Settings {
public:
    static Settings& getInstance();

    bool load();
    bool save();
    void resetToDefaults();

    const std::string& loadError() const { return m_loadError; }
    const std::string& saveError() const { return m_saveError; }

    /// Durable operation journal. Kept in application data (not cache, not the
    /// source tree) because it is required to undo operations.
    std::string journalPath() const;

    // General
    bool dark_theme = true;
    bool confirmations_enabled = true;

    // Scanning defaults (applied to new scans only)
    bool show_hidden_entries = true;
    bool follow_filesystem_boundaries = false;
    bool compare_whole_trees = true;
    size_t hash_buffer_size = 65536;

    // Duplicate handling defaults
    std::string default_mode = "quarantine"; // preview | quarantine | unique | merge

    // Unique Sentences defaults. These change the comparison only; the output
    // always keeps the first occurrence's exact text.
    bool sentences_trim_whitespace = true;
    bool sentences_collapse_whitespace = false;
    bool sentences_case_insensitive = false;

    // Useless Files defaults. Presets stay opt-in per scan; only the custom
    // pattern list is remembered as a default.
    std::string useless_custom_patterns;

    // Resize Images defaults.
    std::string resize_mode = "copies"; // copies | inplace
    int resize_max_width = 1920;
    int resize_max_height = 1080;
    double resize_percent = 50.0;
    int resize_quality = 90;
    bool resize_allow_upscale = false;

    // Logging
    bool logging_enabled = false;
    std::string log_level = "Info";
    size_t log_max_file_size = 10485760; // 10 MB
    int log_max_files = 5;

private:
    Settings();
    std::string get_settings_path();

    std::string m_loadError;
    std::string m_saveError;
};

} // namespace gui
} // namespace dupli
