#include "dupli/gui/platform_paths.hpp"

#ifdef _WIN32
    #include <windows.h>
    #include <shlobj.h>
#elif defined(__APPLE__)
    #include <pwd.h>
    #include <unistd.h>
    #include <sys/types.h>
#else // Linux
    #include <pwd.h>
    #include <unistd.h>
    #include <sys/types.h>
#endif

namespace dupli {
namespace gui {

std::filesystem::path PlatformPaths::get_config_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return std::filesystem::path(path) / "DuplicateManager";
    }
    return std::filesystem::current_path() / "config";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    return std::filesystem::path(home) / "Library" / "Application Support" / "DuplicateManager";
#else // Linux
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config) {
        return std::filesystem::path(xdg_config) / "DuplicateManager";
    }
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    return std::filesystem::path(home) / ".config" / "DuplicateManager";
#endif
}

std::filesystem::path PlatformPaths::get_data_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return std::filesystem::path(path) / "DuplicateManager";
    }
    return std::filesystem::current_path() / "data";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    return std::filesystem::path(home) / "Library" / "Application Support" / "DuplicateManager";
#else // Linux
    const char* xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data) {
        return std::filesystem::path(xdg_data) / "DuplicateManager";
    }
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    return std::filesystem::path(home) / ".local" / "share" / "DuplicateManager";
#endif
}

std::filesystem::path PlatformPaths::get_cache_dir() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_INTERNET_CACHE, NULL, 0, path))) {
        return std::filesystem::path(path) / "DuplicateManager";
    }
    return std::filesystem::current_path() / "cache";
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    return std::filesystem::path(home) / "Library" / "Caches" / "DuplicateManager";
#else // Linux
    const char* xdg_cache = getenv("XDG_CACHE_HOME");
    if (xdg_cache) {
        return std::filesystem::path(xdg_cache) / "DuplicateManager";
    }
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : ".";
    }
    return std::filesystem::path(home) / ".cache" / "DuplicateManager";
#endif
}

void PlatformPaths::ensure_directories_exist() {
    try {
        std::filesystem::create_directories(get_config_dir());
        std::filesystem::create_directories(get_data_dir());
        std::filesystem::create_directories(get_cache_dir());
    } catch (...) {
        // Ignore errors
    }
}

} // namespace gui
} // namespace dupli
