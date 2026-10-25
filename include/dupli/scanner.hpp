#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/result.hpp"
#include <vector>

namespace dupli {

struct ScannedEntry {
    std::filesystem::path path;
    bool is_file = false;
    bool is_dir = false;
    bool is_symlink = false;
    std::string error;
};

class Scanner {
public:
    Scanner(IFileSystem& fs) : m_fs(fs) {}
    
    Result<std::vector<ScannedEntry>> scan(const std::vector<std::filesystem::path>& roots, bool follow_symlinks = false) {
        std::vector<ScannedEntry> entries;
        for (const auto& root : roots) {
            scan_recursive(root, entries, follow_symlinks);
        }
        return entries;
    }
    
private:
    void scan_recursive(const std::filesystem::path& path, std::vector<ScannedEntry>& entries, bool follow_symlinks) {
        ScannedEntry entry;
        entry.path = path;
        
        // Don't follow symlinks
        if (!follow_symlinks) {
            // In real impl, check for symlink
        }
        
        auto is_file = m_fs.is_regular_file(path);
        auto is_dir = m_fs.is_directory(path);
        auto exists = m_fs.exists(path);
        
        if (!exists.ok() || !exists.value()) {
            entry.error = "Not found";
            entries.push_back(entry);
            return;
        }
        
        if (is_file.ok() && is_file.value()) {
            entry.is_file = true;
            entries.push_back(entry);
            return;
        }
        
        if (is_dir.ok() && is_dir.value()) {
            entry.is_dir = true;
            entries.push_back(entry);
            
            auto list_result = m_fs.list_directory(path);
            if (!list_result.ok()) {
                return; // Continue scanning, record error elsewhere
            }
            
            for (const auto& child : list_result.value()) {
                scan_recursive(child, entries, follow_symlinks);
            }
        }
    }
    
    IFileSystem& m_fs;
};

} // namespace dupli
