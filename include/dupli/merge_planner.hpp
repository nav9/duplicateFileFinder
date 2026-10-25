#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/result.hpp"
#include <vector>
#include <string>

namespace dupli {

enum class ConflictPolicy {
    RequireDecision,
    KeepBoth,
    PreferSource,
    PreferDestination
};

struct MergeConflict {
    std::string description;
    std::filesystem::path path;
    bool resolvable = false;
};

class MergePlanner {
public:
    MergePlanner(IFileSystem& fs) : m_fs(fs) {}
    
    struct MergePlan {
        std::vector<std::string> actions;
        std::vector<MergeConflict> conflicts;
        bool executable = false;
    };
    
    Result<MergePlan> plan_merge(const std::vector<std::filesystem::path>& sources, 
                                  const std::filesystem::path& dest,
                                  bool in_place = false) {
        MergePlan plan;
        
        if (in_place) {
            plan.actions.push_back("WARNING: In-place merge is advanced mode - requires explicit approval");
        }
        
        // Check for conflicts
        for (const auto& src : sources) {
            std::string src_name = src.filename().string();
            std::filesystem::path dest_path = dest / src_name;
            
            auto dest_exists = m_fs.exists(dest_path);
            if (dest_exists.ok() && dest_exists.value()) {
                auto src_is_file = m_fs.is_regular_file(src);
                auto dest_is_file = m_fs.is_regular_file(dest_path);
                
                if (src_is_file.ok() && dest_is_file.ok() && 
                    src_is_file.value() && dest_is_file.value()) {
                    // Same path different content - conflict
                    MergeConflict conflict;
                    conflict.path = dest_path;
                    conflict.description = "File exists with different content";
                    conflict.resolvable = true;
                    plan.conflicts.push_back(conflict);
                } else if (src_is_file.ok() && dest_is_dir_check(dest_path)) {
                    MergeConflict conflict;
                    conflict.path = dest_path;
                    conflict.description = "File vs directory conflict";
                    conflict.resolvable = false;
                    plan.conflicts.push_back(conflict);
                }
            } else {
                plan.actions.push_back("COPY: " + src.string() + " -> " + dest_path.string());
            }
        }
        
        // Must have no unresolved conflicts to be executable
        bool has_unresolved = false;
        for (const auto& c : plan.conflicts) {
            if (!c.resolvable) has_unresolved = true;
        }
        plan.executable = !has_unresolved && !plan.conflicts.empty() ? false : true;
        // If there are resolvable conflicts, still need decision
        if (!plan.conflicts.empty()) {
            bool all_resolvable = true;
            for (const auto& c : plan.conflicts) {
                if (!c.resolvable) all_resolvable = false;
            }
            if (!all_resolvable) plan.executable = false;
            else plan.executable = false; // Require decision even if resolvable
        }
        
        return plan;
    }
    
private:
    bool dest_is_dir_check(const std::filesystem::path& path) {
        auto r = m_fs.is_directory(path);
        return r.ok() && r.value();
    }
    
    IFileSystem& m_fs;
};

} // namespace dupli
