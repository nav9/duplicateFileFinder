#include "dupli/validator.hpp"

#include <algorithm>
#include <filesystem>
#include <map>

namespace dupli {

namespace {

bool is_within(const std::filesystem::path& prefix, const std::filesystem::path& candidate) {
    const auto normalized_prefix = prefix.lexically_normal();
    const auto normalized_candidate = candidate.lexically_normal();
    auto prefix_it = normalized_prefix.begin();
    auto candidate_it = normalized_candidate.begin();
    for (; prefix_it != normalized_prefix.end(); ++prefix_it, ++candidate_it) {
        if (candidate_it == normalized_candidate.end()) return false;
        if (candidate_it->native() != prefix_it->native()) return false;
    }
    return true;
}

bool has_approval(const PlannedAction& action, const ApprovalSet& approvals) {
    for (const auto& approval : approvals.approvals) {
        if (approval.action_id != action.id) continue;
        if (approval.source != path_to_utf8(action.source)) continue;
        if (!approval.expected_digest.empty() &&
            approval.expected_digest != action.expected_digest) {
            continue;
        }
        return true;
    }
    return false;
}

const char* status_text(ActionStatus status) {
    switch (status) {
    case ActionStatus::Valid: return "valid";
    case ActionStatus::Stale: return "stale";
    case ActionStatus::NeedsReview: return "needs review";
    case ActionStatus::Invalid: return "invalid";
    }
    return "invalid";
}

} // namespace

ActionValidation Validator::validate_action(const PlannedAction& action,
                                            const ApprovalSet& approvals) const {
    ActionValidation result;
    result.action_id = action.id;
    result.status = ActionStatus::Valid;

    auto fail = [&result](ActionStatus status, std::string problem) {
        result.status = status;
        result.problems.push_back(std::move(problem));
    };

    if (action.source.empty() || action.destination.empty()) {
        fail(ActionStatus::Invalid, "Action has an empty source or destination");
        return result;
    }

    // A source can never be moved onto itself, and a directory must not be moved
    // into its own subtree (that would be a self-referential move).
    if (action.source.lexically_normal() == action.destination.lexically_normal()) {
        fail(ActionStatus::Invalid, "Source and destination are the same path");
        return result;
    }
    if (is_within(action.source, action.destination)) {
        fail(ActionStatus::Invalid, "Destination is inside the source path");
        return result;
    }

    auto exists = m_fs.exists(action.source);
    if (exists.has_error()) {
        fail(ActionStatus::Stale, "Cannot inspect source: " + exists.error().message);
        return result;
    }
    if (!exists.value()) {
        fail(ActionStatus::Stale, "Source no longer exists");
        return result;
    }

    auto is_file = m_fs.is_regular_file(action.source);
    if (is_file.has_error() || !is_file.value()) {
        fail(ActionStatus::Stale, "Source is no longer a regular file");
        return result;
    }

    auto is_link = m_fs.is_symlink(action.source);
    if (is_link.ok() && is_link.value()) {
        fail(ActionStatus::Invalid, "Source is a symbolic link and must not be moved or copied");
        return result;
    }

    if (action.expected_size != 0) {
        auto size = m_fs.file_size(action.source);
        if (size.has_error()) {
            fail(ActionStatus::Stale, "Cannot read source size: " + size.error().message);
            return result;
        }
        if (size.value() != action.expected_size) {
            fail(ActionStatus::Stale, "Source size changed since planning");
            return result;
        }
    }

    // Content identity is established by a full BLAKE3 re-hash. Skipping this when
    // a digest is missing would let an unverified file be moved as if it were a
    // known duplicate.
    if (action.expected_digest.empty()) {
        fail(ActionStatus::NeedsReview, "Action has no expected content digest to verify");
    } else {
        auto opened = m_fs.open_read(action.source);
        if (opened.has_error()) {
            fail(ActionStatus::Stale, "Cannot re-read source: " + opened.error().message);
        } else {
            auto digest = m_hasher.hash_file(*opened.value());
            if (digest.has_error()) {
                fail(ActionStatus::Stale, "Re-hashing the source failed: " + digest.error().message);
            } else if (!digest.value().complete || digest.value().hex != action.expected_digest) {
                fail(ActionStatus::Stale, "Source content changed since planning");
            }
        }
    }

    // Never overwrite. A destination that appeared after planning is a conflict
    // for a human to resolve, not something to silently replace.
    auto dest_exists = m_fs.exists(action.destination);
    if (dest_exists.has_error()) {
        fail(ActionStatus::NeedsReview,
             "Cannot inspect destination: " + dest_exists.error().message);
    } else if (dest_exists.value()) {
        fail(ActionStatus::NeedsReview, "Destination already exists and will not be overwritten");
    }

    // The parent must either not exist yet (the executor creates it) or be a
    // directory. If it exists as a file the action can never succeed.
    auto parent = action.destination.parent_path();
    if (!parent.empty()) {
        auto parent_exists = m_fs.exists(parent);
        if (parent_exists.ok() && parent_exists.value()) {
            auto parent_is_dir = m_fs.is_directory(parent);
            if (parent_is_dir.has_error() || !parent_is_dir.value()) {
                fail(ActionStatus::Invalid, "Destination parent exists but is not a directory");
            }
        }
    }

    if (action.requires_approval && !has_approval(action, approvals)) {
        fail(ActionStatus::NeedsReview,
             "Missing explicit approval: " +
                 (action.approval_reason.empty() ? std::string("review required")
                                                 : action.approval_reason));
    }

    return result;
}

Result<ValidationResult> Validator::validate_plan(const ActionPlan& plan) {
    return validate_plan(plan, ApprovalSet{});
}

Result<ValidationResult> Validator::validate_plan(const ActionPlan& plan,
                                                  const ApprovalSet& approvals) {
    ValidationResult result;
    result.valid = true;

    if (plan.has_cycles) {
        result.valid = false;
        result.errors.push_back("Plan contains dependency cycles");
    }
    if (plan.mode == ActionMode::PreviewOnly) {
        result.valid = false;
        result.errors.push_back("Preview only mode cannot be executed");
    }
    if (!plan.unresolved_conflicts.empty()) {
        result.valid = false;
        result.errors.push_back("Plan has unresolved conflicts; resolve them before executing");
    }
    if (!approvals.plan_id.empty() && !plan.plan_id.empty() &&
        approvals.plan_id != plan.plan_id) {
        result.valid = false;
        result.errors.push_back("Approvals were given for a different version of this plan");
    }

    // Two actions must never act on the same source or publish to the same
    // destination; that would make the outcome depend on execution order.
    std::map<std::string, std::string> source_owner;
    std::map<std::string, std::string> destination_owner;
    for (const auto& action : plan.actions) {
        const std::string source_key = path_to_utf8(action.source.lexically_normal());
        const std::string destination_key = path_to_utf8(action.destination.lexically_normal());
        auto source_it = source_owner.find(source_key);
        if (source_it != source_owner.end()) {
            result.valid = false;
            result.errors.push_back("Actions " + source_it->second + " and " + action.id +
                                    " use the same source: " + source_key);
        } else {
            source_owner[source_key] = action.id;
        }
        auto destination_it = destination_owner.find(destination_key);
        if (destination_it != destination_owner.end()) {
            result.valid = false;
            result.errors.push_back("Actions " + destination_it->second + " and " + action.id +
                                    " use the same destination: " + destination_key);
        } else {
            destination_owner[destination_key] = action.id;
        }
    }

    for (const auto& action : plan.actions) {
        ActionValidation validation = validate_action(action, approvals);
        if (validation.status != ActionStatus::Valid) {
            std::string message = "Action " + action.id + " (" + status_text(validation.status) +
                                  "): " + path_to_utf8(action.source);
            for (const auto& problem : validation.problems) message += "; " + problem;
            result.errors.push_back(message);
            result.valid = false;
        }
        result.actions.push_back(std::move(validation));
    }

    if (plan.actions.empty()) {
        result.warnings.push_back("Plan contains no actions");
    }
    return result;
}

} // namespace dupli
