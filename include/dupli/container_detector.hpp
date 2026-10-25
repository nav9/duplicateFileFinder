#pragma once

#include "dupli/git_detector.hpp"
#include "dupli/web_archive_detector.hpp"
#include "dupli/filesystem.hpp"
#include <vector>

namespace dupli {

class ContainerDetector {
public:
    ContainerDetector(IFileSystem& fs) 
        : m_git(fs), m_web(fs), m_fs(fs) {}
    
    struct ContainerInfo {
        bool is_container = false;
        std::string type; // "git", "web_archive"
        std::vector<std::string> warnings;
        bool requires_explicit_approval = false;
    };
    
    Result<std::vector<ContainerInfo>> detect(const std::filesystem::path& path) {
        std::vector<ContainerInfo> containers;
        
        auto git_info = m_git.detect(path);
        if (git_info.ok() && git_info.value().is_git_repo) {
            ContainerInfo ci;
            ci.is_container = true;
            ci.type = "git";
            ci.warnings = git_info.value().warnings;
            ci.requires_explicit_approval = true;
            containers.push_back(ci);
        }
        
        auto web_info = m_web.detect(path);
        if (web_info.ok() && web_info.value().is_web_archive) {
            ContainerInfo ci;
            ci.is_container = true;
            ci.type = "web_archive";
            ci.warnings = web_info.value().warnings;
            ci.requires_explicit_approval = true;
            containers.push_back(ci);
        }
        
        return containers;
    }
    
private:
    GitDetector m_git;
    WebArchiveDetector m_web;
    IFileSystem& m_fs;
};

} // namespace dupli
