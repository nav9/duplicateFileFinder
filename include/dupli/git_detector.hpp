#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/result.hpp"
#include <string>
#include <vector>

namespace dupli {

class GitDetector {
public:
    GitDetector(IFileSystem& fs) : m_fs(fs) {}
    
    struct GitInfo {
        bool is_git_repo = false;
        bool is_bare = false;
        std::string path;
        std::vector<std::string> warnings;
    };
    
    Result<GitInfo> detect(const std::filesystem::path& path) {
        GitInfo info;
        std::filesystem::path git_path = path / ".git";
        
        auto exists = m_fs.exists(git_path);
        if (!exists.ok() || !exists.value()) {
            return info;
        }
        
        auto is_dir = m_fs.is_directory(git_path);
        if (is_dir.ok() && is_dir.value()) {
            info.is_git_repo = true;
            info.path = path.string();
            info.warnings.push_back("Git repository detected - requires explicit approval for moves/merges");
            return info;
        }
        
        auto is_file = m_fs.is_regular_file(git_path);
        if (is_file.ok() && is_file.value()) {
            // Could be .git file pointing to worktree
            info.is_git_repo = true;
            info.path = path.string();
            info.warnings.push_back("Git worktree/submodule detected - requires explicit approval");
            return info;
        }
        
        return info;
    }
    
private:
    IFileSystem& m_fs;
};

} // namespace dupli
