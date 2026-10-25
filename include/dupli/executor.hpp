#pragma once

#include "dupli/action_planner.hpp"
#include "dupli/validator.hpp"
#include "dupli/journal.hpp"
#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/result.hpp"

namespace dupli {

struct ExecutionReport {
    bool success = false;
    std::vector<std::string> completed_actions;
    std::vector<std::string> failed_actions;
    std::vector<std::string> errors;
};

class Executor {
public:
    Executor(IFileSystem& fs, IHasher& hasher, IJournal& journal) 
        : m_fs(fs), m_hasher(hasher), m_journal(journal) {}
    
    Result<ExecutionReport> execute(const ActionPlanner::ActionPlan& plan, 
                                     const std::vector<std::string>& approvals) {
        ExecutionReport report;
        
        // Validate first
        Validator validator(m_fs, m_hasher);
        auto val_result = validator.validate_plan(plan);
        if (!val_result.ok() || !val_result.value().valid) {
            report.success = false;
            report.errors.push_back("Plan validation failed");
            return report;
        }
        
        // Check approvals
        ActionPlanner ap;
        if (!ap.validate_approvals(plan, approvals)) {
            report.success = false;
            report.errors.push_back("Missing required approvals");
            return report;
        }
        
        // Execute actions
        for (const auto& action : plan.actions) {
            auto journal_id = m_journal.start_action(action.id, action.source, action.destination);
            
            // For same filesystem copy/move (simplified)
            auto copy_result = m_fs.copy_file(action.source, action.destination);
            if (!copy_result.ok()) {
                m_journal.fail_action(journal_id, copy_result.error().message);
                report.failed_actions.push_back(action.id);
                report.errors.push_back(copy_result.error().message);
                continue;
            }
            
            // Verify - hash destination
            auto read_dest = m_fs.open_read(action.destination);
            if (read_dest.ok()) {
                auto digest = m_hasher.hash_file(*read_dest.value());
                if (digest.ok() && digest.value().complete) {
                    m_journal.complete_action(journal_id);
                    report.completed_actions.push_back(action.id);
                } else {
                    m_journal.fail_action(journal_id, "Verification failed");
                    report.failed_actions.push_back(action.id);
                }
            } else {
                m_journal.fail_action(journal_id, "Could not verify destination");
                report.failed_actions.push_back(action.id);
            }
        }
        
        report.success = report.failed_actions.empty() && !report.completed_actions.empty();
        return report;
    }
    
private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
    IJournal& m_journal;
};

} // namespace dupli
