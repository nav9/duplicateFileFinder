#pragma once

#include "dupli/action_planner.hpp"
#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/result.hpp"
#include <string>
#include <vector>

namespace dupli {

enum class ActionStatus {
    Valid,
    /// The source no longer matches what was planned (content changed, replaced,
    /// missing). The action must not run with the old decision.
    Stale,
    /// A human decision is required (destination collision, missing approval).
    NeedsReview,
    /// The action can never be valid as planned.
    Invalid
};

struct ActionValidation {
    std::string action_id;
    ActionStatus status = ActionStatus::Invalid;
    std::vector<std::string> problems;
};

struct ValidationResult {
    bool valid = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<ActionValidation> actions;
};

// Validator performs the "immediately before execution" checks. It is
// read-only: it re-hashes sources, inspects destinations and verifies approvals,
// but never mutates anything. Executor calls it again right before each action so
// a plan that was valid minutes ago cannot act on changed files.
class Validator {
public:
    Validator(IFileSystem& fs, IHasher& hasher) : m_fs(fs), m_hasher(hasher) {}

    Result<ValidationResult> validate_plan(const ActionPlan& plan);
    Result<ValidationResult> validate_plan(const ActionPlan& plan, const ApprovalSet& approvals);

    /// Validate a single action. Used by the executor immediately before it
    /// mutates anything.
    ActionValidation validate_action(const PlannedAction& action,
                                     const ApprovalSet& approvals) const;

private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
