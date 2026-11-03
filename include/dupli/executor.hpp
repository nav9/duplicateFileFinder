#pragma once

#include "dupli/action_planner.hpp"
#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/journal.hpp"
#include "dupli/result.hpp"
#include "dupli/validator.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace dupli {

struct ExecutionReport {
    bool success = false;
    /// True when some actions succeeded and others did not; the UI must not call
    /// this "completed".
    bool partial = false;
    std::vector<std::string> completed_actions;
    std::vector<std::string> failed_actions;
    std::vector<std::string> skipped_actions;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    /// Files that were copied instead of moved because the destination is on a
    /// different filesystem. The source is intentionally kept.
    std::vector<std::string> sources_retained;
    bool recovery_required = false;
    uintmax_t bytes_transferred = 0;

    size_t succeeded_count() const { return completed_actions.size(); }
    size_t failed_count() const { return failed_actions.size(); }
    size_t skipped_count() const { return skipped_actions.size(); }
};

// Executor is the only component allowed to mutate user data. It refuses to run
// unless the plan validates, approvals are bound to this exact plan, and each
// action still matches its expected content immediately before it runs.
class Executor {
public:
    Executor(IFileSystem& fs, IHasher& hasher, IJournal& journal)
        : m_fs(fs), m_hasher(hasher), m_journal(journal) {}

    Result<ExecutionReport> execute(const ActionPlan& plan, const ApprovalSet& approvals);
    Result<ExecutionReport> execute(const ActionPlan& plan,
                                    const std::vector<std::string>& approvals);

private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
    IJournal& m_journal;
};

} // namespace dupli
