#include "dupli/gui/settings.hpp"

#include "dupli/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace dupli {
namespace gui {

namespace {

constexpr int kSettingsVersion = 1;

double clampNumber(double value, double minimum, double maximum, double fallback) {
    if (value < minimum || value > maximum) return fallback;
    return value;
}

} // namespace

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

std::string Settings::journalPath() const {
    return (PlatformPaths::get_data_dir() / "operations.jsonl").string();
}

bool Settings::load() {
    m_loadError.clear();
    const std::filesystem::path path(get_settings_path());
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // No settings yet is normal: keep the safe defaults shown above.
        return true;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    json::ParseError parseError;
    auto parsed = json::parse(text, &parseError);
    if (!parsed) {
        // Preserve the unusable file for inspection instead of overwriting it,
        // then continue with safe defaults. The UI surfaces loadError().
        std::error_code ignored;
        std::filesystem::rename(path, path.string() + ".corrupt", ignored);
        m_loadError = "Settings were unreadable (" + parseError.message +
                      "); defaults restored. The old file was kept as settings.json.corrupt.";
        resetToDefaults();
        return false;
    }

    const int version = static_cast<int>((*parsed)["version"].as_int());
    if (version > kSettingsVersion) {
        m_loadError = "Settings were written by a newer version; defaults are used.";
        resetToDefaults();
        return false;
    }

    // Every assignment is validated; a bad value leaves the default in place.
    dark_theme = (*parsed)["general"]["dark_theme"].as_bool(dark_theme);
    confirmations_enabled =
        (*parsed)["general"]["confirmations"].as_bool(confirmations_enabled);

    show_hidden_entries =
        (*parsed)["scanning"]["include_hidden"].as_bool(show_hidden_entries);
    follow_filesystem_boundaries =
        (*parsed)["scanning"]["cross_boundaries"].as_bool(follow_filesystem_boundaries);
    compare_whole_trees =
        (*parsed)["scanning"]["compare_trees"].as_bool(compare_whole_trees);

    const double bufferValue = (*parsed)["scanning"]["hash_buffer_size"].as_double(
        static_cast<double>(hash_buffer_size));
    hash_buffer_size = static_cast<size_t>(
        clampNumber(bufferValue, 4096.0, 16.0 * 1024 * 1024, 65536.0));

    const std::string mode = (*parsed)["duplicates"]["default_mode"].as_string();
    if (mode == "preview" || mode == "quarantine" || mode == "unique" || mode == "merge") {
        default_mode = mode;
    }

    sentences_trim_whitespace =
        (*parsed)["sentences"]["trim_whitespace"].as_bool(sentences_trim_whitespace);
    sentences_collapse_whitespace =
        (*parsed)["sentences"]["collapse_whitespace"].as_bool(sentences_collapse_whitespace);
    sentences_case_insensitive =
        (*parsed)["sentences"]["case_insensitive"].as_bool(sentences_case_insensitive);
    useless_custom_patterns = (*parsed)["useless"]["custom_patterns"].as_string();

    const std::string resizeMode = (*parsed)["resize"]["mode"].as_string();
    if (resizeMode == "copies" || resizeMode == "inplace") resize_mode = resizeMode;
    resize_max_width = static_cast<int>(clampNumber(
        (*parsed)["resize"]["max_width"].as_double(resize_max_width), 1.0, 20000.0, 1920.0));
    resize_max_height = static_cast<int>(clampNumber(
        (*parsed)["resize"]["max_height"].as_double(resize_max_height), 1.0, 20000.0, 1080.0));
    resize_percent = clampNumber(
        (*parsed)["resize"]["percent"].as_double(resize_percent), 1.0, 1000.0, 50.0);
    resize_quality = static_cast<int>(clampNumber(
        (*parsed)["resize"]["quality"].as_double(resize_quality), 1.0, 100.0, 90.0));
    resize_allow_upscale =
        (*parsed)["resize"]["allow_upscale"].as_bool(resize_allow_upscale);

    logging_enabled = (*parsed)["logging"]["enabled"].as_bool(logging_enabled);
    const std::string level = (*parsed)["logging"]["level"].as_string();
    if (level == "Trace" || level == "Debug" || level == "Info" || level == "Warning" ||
        level == "Error" || level == "Critical") {
        log_level = level;
    }
    const double maxSize =
        (*parsed)["logging"]["max_file_size"].as_double(static_cast<double>(log_max_file_size));
    log_max_file_size = static_cast<size_t>(
        clampNumber(maxSize, 64.0 * 1024, 1024.0 * 1024 * 1024, 10.0 * 1024 * 1024));
    const double maxFiles = (*parsed)["logging"]["max_files"].as_double(log_max_files);
    log_max_files = static_cast<int>(clampNumber(maxFiles, 1.0, 50.0, 5.0));
    return true;
}

bool Settings::save() {
    m_saveError.clear();
    PlatformPaths::ensure_directories_exist();
    const std::filesystem::path path(get_settings_path());
    const std::filesystem::path temporary(path.string() + ".tmp");

    json::Value logging;
    logging.set("enabled", logging_enabled);
    logging.set("level", log_level);
    logging.set("max_file_size", log_max_file_size);
    logging.set("max_files", log_max_files);

    json::Value scanning;
    scanning.set("include_hidden", show_hidden_entries);
    scanning.set("cross_boundaries", follow_filesystem_boundaries);
    scanning.set("compare_trees", compare_whole_trees);
    scanning.set("hash_buffer_size", hash_buffer_size);

    json::Value duplicates;
    duplicates.set("default_mode", default_mode);

    json::Value general;
    general.set("dark_theme", dark_theme);
    general.set("confirmations", confirmations_enabled);

    json::Value sentences;
    sentences.set("trim_whitespace", sentences_trim_whitespace);
    sentences.set("collapse_whitespace", sentences_collapse_whitespace);
    sentences.set("case_insensitive", sentences_case_insensitive);

    json::Value useless;
    useless.set("custom_patterns", useless_custom_patterns);

    json::Value resize;
    resize.set("mode", resize_mode);
    resize.set("max_width", resize_max_width);
    resize.set("max_height", resize_max_height);
    resize.set("percent", resize_percent);
    resize.set("quality", resize_quality);
    resize.set("allow_upscale", resize_allow_upscale);

    json::Value root;
    root.set("version", kSettingsVersion);
    root.set("general", general);
    root.set("scanning", scanning);
    root.set("duplicates", duplicates);
    root.set("sentences", sentences);
    root.set("useless", useless);
    root.set("resize", resize);
    root.set("logging", logging);

    const std::string text = json::dump(root, true) + "\n";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            m_saveError = "Could not create " + temporary.string();
            return false;
        }
        file << text;
        file.flush();
        if (!file) {
            m_saveError = "Could not write " + temporary.string();
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        // On platforms where rename cannot replace an existing file, remove the
        // old one only after the new one is fully written.
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        m_saveError = "Could not replace settings file: " + error.message();
        return false;
    }
    return true;
}

void Settings::resetToDefaults() {
    dark_theme = true;
    confirmations_enabled = true;
    show_hidden_entries = true;
    follow_filesystem_boundaries = false;
    compare_whole_trees = true;
    hash_buffer_size = 65536;
    default_mode = "quarantine";
    sentences_trim_whitespace = true;
    sentences_collapse_whitespace = false;
    sentences_case_insensitive = false;
    useless_custom_patterns.clear();
    resize_mode = "copies";
    resize_max_width = 1920;
    resize_max_height = 1080;
    resize_percent = 50.0;
    resize_quality = 90;
    resize_allow_upscale = false;
    logging_enabled = false;
    log_level = "Info";
    log_max_file_size = 10485760;
    log_max_files = 5;
}

} // namespace gui
} // namespace dupli
