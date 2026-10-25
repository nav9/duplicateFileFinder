#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/result.hpp"
#include <string>

namespace dupli {

class WebArchiveDetector {
public:
    WebArchiveDetector(IFileSystem& fs) : m_fs(fs) {}
    
    struct WebArchiveInfo {
        bool is_web_archive = false;
        std::string html_file;
        std::vector<std::string> resource_dirs;
        std::vector<std::string> warnings;
    };
    
    Result<WebArchiveInfo> detect(const std::filesystem::path& path) {
        WebArchiveInfo info;
        
        auto list_result = m_fs.list_directory(path);
        if (!list_result.ok()) {
            return info;
        }
        
        for (const auto& entry : list_result.value()) {
            std::string name = entry.filename().string();
            if (name.size() > 5 && name.substr(name.size()-5) == ".html") {
                // Check for associated folder
                std::string base = name.substr(0, name.size()-5) + "_files";
                std::filesystem::path files_dir = path / base;
                auto files_exists = m_fs.exists(files_dir);
                if (files_exists.ok() && files_exists.value()) {
                    auto files_is_dir = m_fs.is_directory(files_dir);
                    if (files_is_dir.ok() && files_is_dir.value()) {
                        info.is_web_archive = true;
                        info.html_file = entry.string();
                        info.resource_dirs.push_back(files_dir.string());
                        info.warnings.push_back("Saved web page with resources - shared resources must not be moved away");
                    }
                }
            }
        }
        
        return info;
    }
    
private:
    IFileSystem& m_fs;
};

} // namespace dupli
