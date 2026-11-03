#include "dupli/executor.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace dupli {

namespace {

Result<void> ensure_directory(IFileSystem& fs, const std::filesystem::path& directory) {
    if (directory.empty()) return Result<void>();
    auto exists = fs.exists(directory);
    if (exists.has_error()) return exists.error();
    if (exists.value()) {
        auto is_dir = fs.is_directory(directory);
        if (is_dir.has_error()) return is_dir.error();
        if (!is_dir.value()) {
            return Error{ErrorCode::Conflict, "A file occupies the destination folder path",
                         path_to_utf8(directory), "create_directory"};
        }
        return Result<void>();
    }
    return fs.create_directory(directory);
}

/// Executes in dependency order. Unknown dependencies are ignored here because
/// the validator rejects them; this keeps the ordering step total.
std::vector<const PlannedAction*> order_actions(const std::vector<PlannedAction>& actions) {
    std::map<std::string, const PlannedAction*> by_id;
    for (const auto& action : actions) by_id[action.id] = &action;

    std::vector<const PlannedAction*> ordered;
    std::map<std::string, bool> done;

    for (const auto& start : actions) {
        if (done[start.id]) continue;
        std::vector<std::pair<const PlannedAction*, size_t>> stack;
        stack.emplace_back(&start, 0);
        done[start.id] = true;
        while (!stack.empty()) {
            auto& [action, dependency_index] = stack.back();
            if (dependency_index >= action->dependencies.size()) {
                ordered.push_back(action);
                stack.pop_back();
                continue;
            }
            const std::string& dependency_id = action->dependencies[dependency_index++];
            auto it = by_id.find(dependency_id);
            if (it == by_id.end()) continue;
            if (done[dependency_id]) continue;
            done[dependency_id] = true;
            stack.emplace_back(it->second, 0);
        }
    }
    return ordered;
}

/// Nearest ancestor that exists. Device comparison must work even when the
/// destination folder has not been created yet (the executor journals before it
/// creates folders), otherwise a cross-device move would be mistaken for a
/// same-device rename and fail with EXDEV.
std::filesystem::path nearest_existing_ancestor(IFileSystem& fs,
                                                std::filesystem::path path) {
    while (!path.empty()) {
        auto exists = fs.exists(path);
        if (exists.ok() && exists.value()) return path;
        const auto parent = path.parent_path();
        if (parent == path) break;
        path = parent;
    }
    return path;
}

std::string verify_destination(IFileSystem& fs, IHasher& hasher,
                               const std::filesystem::path& destination,
                               const std::string& expected_digest) {
    auto opened = fs.open_read(destination);
    if (opened.has_error()) return "Cannot open the destination for verification";
    auto digest = hasher.hash_file(*opened.value());
    if (digest.has_error()) return "Cannot hash the destination for verification";
    if (!digest.value().complete) return "Destination hashing did not complete";
    if (digest.value().hex != expected_digest) return "Destination content does not match the source";
    return "";
}

void remove_quietly(IFileSystem& fs, const std::filesystem::path& path) {
    // Only called for a destination this action just created; a failure here is
    // surfaced through recovery_required by the caller.
    (void)fs.remove(path);
}

} // namespace

Result<ExecutionReport> Executor::execute(const ActionPlan& plan,
                                          const std::vector<std::string>& approved_ids) {
    ApprovalSet approvals;
    for (const auto& id : approved_ids) {
        for (const auto& action : plan.actions) {
            if (action.id != id) continue;
            approvals.approvals.push_back(
                Approval{id, path_to_utf8(action.source), std::string()});
            break;
        }
    }
    return execute(plan, approvals);
}

Result<ExecutionReport> Executor::execute(const ActionPlan& plan, const ApprovalSet& approvals) {
    ExecutionReport report;

    if (plan.mode == ActionMode::PreviewOnly) {
        report.errors.push_back("Preview-only plans are never executed");
        return report;
    }
    if (plan.actions.empty()) {
        report.errors.push_back("Plan contains no actions");
        return report;
    }

    Validator validator(m_fs, m_hasher);
    auto validation = validator.validate_plan(plan, approvals);
    if (validation.has_error()) {
        report.errors.push_back(validation.error().message);
        return report;
    }
    if (!validation.value().valid) {
        // Nothing has been touched at this point; the caller must review.
        report.errors = validation.value().errors;
        report.recovery_required = false;
        return report;
    }

    for (const auto* action : order_actions(plan.actions)) {
        // Re-check this action against the live filesystem immediately before
        // mutating: a plan validated seconds ago can already be stale.
        ActionValidation current = validator.validate_action(*action, approvals);
        if (current.status != ActionStatus::Valid) {
            report.skipped_actions.push_back(action->id);
            std::string message = "Skipped " + action->id + " (" + path_to_utf8(action->source) + ")";
            for (const auto& problem : current.problems) message += ": " + problem;
            report.errors.push_back(message);
            continue;
        }

        // Choose the real operation now, before journaling, so undo always knows
        // what was actually done. Across filesystems a "move" becomes a copy and
        // the source is retained; that is a product requirement, not a failure.
        ActionKind actual_kind = action->kind;
        bool cross_device = false;
        auto same_device = m_fs.same_device(
            action->source,
            nearest_existing_ancestor(m_fs, action->destination.parent_path()));
        if (same_device.ok() && !same_device.value()) {
            cross_device = true;
            if (actual_kind == ActionKind::Move) actual_kind = ActionKind::Copy;
        }

        JournalRecord record;
        record.id = m_journal.next_id();
        record.action_id = action->id;
        record.kind = to_string(actual_kind);
        record.source = action->source;
        record.destination = action->destination;
        record.expected_digest = action->expected_digest;
        record.state = JournalState::Started;
        auto journal_started = m_journal.append(record);
        if (journal_started.has_error()) {
            // Durability comes first: no durable record means no mutation.
            report.failed_actions.push_back(action->id);
            report.errors.push_back("Journal write failed, so the action was not started: " +
                                    journal_started.error().message);
            report.recovery_required = true;
            continue;
        }

        std::string problem;
        bool mutated = false;

        // Creating the destination folder happens after the journal record so the
        // attempt is always recorded; only the folder itself (never user data) can
        // be left behind if the operation fails.
        auto ensure_parent = ensure_directory(m_fs, action->destination.parent_path());
        if (ensure_parent.has_error()) {
            problem = "Could not create the destination folder: " +
                      ensure_parent.error().message;
        } else if (actual_kind == ActionKind::Move) {
            auto destination_exists = m_fs.exists(action->destination);
            if (destination_exists.has_error()) {
                problem = "Cannot inspect the destination: " + destination_exists.error().message;
            } else if (destination_exists.value()) {
                problem = "Destination already exists and was not overwritten";
            } else {
                auto moved = m_fs.rename(action->source, action->destination);
                if (moved.has_error()) {
                    problem = "Move failed: " + moved.error().message;
                } else {
                    mutated = true;
                }
            }
        } else {
            auto destination_exists = m_fs.exists(action->destination);
            if (destination_exists.has_error()) {
                problem = "Cannot inspect the destination: " + destination_exists.error().message;
            } else if (destination_exists.value()) {
                problem = "Destination already exists and was not overwritten";
            } else {
                auto copied = m_fs.copy_file(action->source, action->destination);
                if (copied.has_error()) {
                    problem = "Copy failed: " + copied.error().message;
                } else {
                    mutated = true;
                    report.bytes_transferred += action->expected_size;
                    // Verify the bytes actually written. A partially written or
                    // truncated copy must never be reported as success.
                    const std::string verification =
                        verify_destination(m_fs, m_hasher, action->destination,
                                           action->expected_digest);
                    if (!verification.empty()) {
                        problem = verification;
                        remove_quietly(m_fs, action->destination);
                        mutated = false;
                    } else {
                        // Content is verified; metadata is copied separately and a
                        // failure is surfaced honestly instead of being ignored.
                        auto metadata = m_fs.copy_metadata(action->source, action->destination);
                        if (metadata.has_error()) {
                            report.warnings.push_back(
                                "Content copied and verified, but metadata could not be preserved for " +
                                path_to_utf8(action->destination) + ": " +
                                metadata.error().message);
                        }
                    }
                }
            }
        }

        if (!problem.empty()) {
            (void)m_journal.fail_action(record.id, problem);
            report.failed_actions.push_back(action->id);
            report.errors.push_back("Action " + action->id + " failed: " + problem);
            if (mutated) report.recovery_required = true;
            continue;
        }

        auto journal_done = m_journal.complete_action(record.id);
        if (journal_done.has_error()) {
            // The filesystem change already happened, so this is reported as
            // completed-but-unrecorded: undo may be unavailable.
            report.warnings.push_back(
                "The action completed but the journal could not record it: " +
                journal_done.error().message);
            report.recovery_required = true;
        }

        report.completed_actions.push_back(action->id);
        if (actual_kind == ActionKind::Copy && action->kind == ActionKind::Move) {
            report.sources_retained.push_back(path_to_utf8(action->source));
            report.warnings.push_back(
                "Kept the source at " + path_to_utf8(action->source) +
                " because the destination is on a different filesystem");
        }
        (void)cross_device;
    }

    report.success = report.failed_actions.empty() && !report.completed_actions.empty();
    report.partial = !report.failed_actions.empty() && !report.completed_actions.empty();
    return report;
}

} // namespace dupli
