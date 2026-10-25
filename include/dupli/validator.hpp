#pragma once

#include "dupli/action_planner.hpp"
#include "dupli/hasher.hpp"
#include "dupli/filesystem.hpp"
#include "dupli/result.hpp"

namespace dupli {

class Validator {
public:
    Validator(IFileSystem& fs, IHasher& hasher) : m_fs(fs), m_hasher(hasher) {}
    
    struct ValidationResult {
        bool valid = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    
    Result<ValidationResult> validate_plan(const ActionPlanner::ActionPlan& plan) {
        ValidationResult result;
        result.valid = true;
        
        // Check for cycles
        if (plan.has_cycles) {
            result.valid = false;
            result.errors.push_back("Plan contains cycles");
        }
        
        // Check if preview only
        if (plan.mode == ActionMode::PreviewOnly) {
            result.valid = false;
            result.errors.push_back("Preview only mode cannot be executed");
        }
        
        // Validate sources exist
        for (const auto& action : plan.actions) {
            auto exists = m_fs.exists(action.source);
            if (!exists.ok() || !exists.value()) {
                result.valid = false;
                result.errors.push_back("Source does not exist: " + action.source.string());
            }
        }
        
        return result;
    }
    
private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
