#pragma once

#include <string>
#include <filesystem>

namespace dupli {
namespace gui {

class PlatformPaths {
public:
    // Get user config directory
    static std::filesystem::path get_config_dir();
    
    // Get user data directory (for durable journals/manifests)
    static std::filesystem::path get_data_dir();
    
    // Get user cache directory (for thumbnails/temp files)
    static std::filesystem::path get_cache_dir();
    
    // Ensure directories exist
    static void ensure_directories_exist();
};

} // namespace gui
} // namespace dupli
