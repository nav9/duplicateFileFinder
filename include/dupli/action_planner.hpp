#pragma once

#include "dupli/merge_planner.hpp"
#include "dupli/result.hpp"
#include <vector>

namespace dupli {

enum class ActionMode {
    PreviewOnly,
    QuarantineDuplicates,
    CreateUniqueCollection,
    InPlaceMerge
};

struct PlannedAction {
    std::string id;
    std::string kind;
    std::vector<std::string> dependencies;
    std::filesystem::path source;
    std::filesystem::path destination;
    bool requires_approval = false;
    std::string approval_reason;
};

class ActionPlanner {
public:
    ActionPlanner() = default;
    
    struct ActionPlan {
        std::vector<PlannedAction> actions;
        ActionMode mode;
        bool has_cycles = false;
        bool executable = false;
        std::vector<std::string> warnings;
    };
    
    Result<ActionPlan> create_plan(ActionMode mode, const std::vector<std::filesystem::path>& sources) {
        ActionPlan plan;
        plan.mode = mode;
        
        if (mode == ActionMode::PreviewOnly) {
            plan.executable = false;
            plan.warnings.push_back("Preview only - no mutations will occur");
            return plan;
        }
        
        if (mode == ActionMode::InPlaceMerge) {
            plan.warnings.push_back("In-place merge requires explicit approval");
        }
        
        // Create simple actions
        int id_counter = 0;
        for (const auto& src : sources) {
            PlannedAction action;
            action.id = "action_" + std::to_string(id_counter++);
            action.kind = "copy";
            action.source = src;
            action.requires_approval = (mode == ActionMode::InPlaceMerge);
            if (action.requires_approval) {
                action.approval_reason = "In-place operations require explicit approval";
            }
            plan.actions.push_back(action);
        }
        
        // Check for cycles (simple - none in this basic impl)
        plan.has_cycles = false;
        plan.executable = true;
        
        return plan;
    }
    
    bool validate_approvals(const ActionPlan& plan, const std::vector<std::string>& approved_ids) {
        for (const auto& action : plan.actions) {
            if (action.requires_approval) {
                bool found = false;
                for (const auto& aid : approved_ids) {
                    if (aid == action.id) { found = true; break; }
                }
                if (!found) return false;
            }
        }
        return true;
    }
};

} // namespace dupli
