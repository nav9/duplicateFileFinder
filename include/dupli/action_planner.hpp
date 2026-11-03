#pragma once

#include "dupli/grouping.hpp"
#include "dupli/result.hpp"
#include "dupli/scanner.hpp"
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dupli {

// The four user-selectable outcomes. PreviewOnly never mutates anything; the
// others are mutations and must pass validation + explicit approval.
enum class ActionMode {
    PreviewOnly,
    QuarantineDuplicates,
    CreateUniqueCollection,
    InPlaceMerge
};

std::string to_string(ActionMode mode);
bool parse_action_mode(std::string_view text, ActionMode& out);

enum class ActionKind { Move, Copy };
std::string to_string(ActionKind kind);
bool parse_action_kind(std::string_view text, ActionKind& out);

/// A single filesystem mutation. `expected_digest` is re-checked immediately
/// before execution: if the source changed since planning the action is stale.
struct PlannedAction {
    std::string id;
    ActionKind kind = ActionKind::Copy;
    std::vector<std::string> dependencies;
    std::filesystem::path source;
    std::filesystem::path destination;
    std::string expected_digest;
    uintmax_t expected_size = 0;
    bool requires_approval = false;
    std::string approval_reason;
    std::string rationale;
};

struct ActionPlan {
    /// Deterministic hash over the plan contents. Approvals are bound to it, so
    /// editing a saved plan invalidates previously given approvals.
    std::string plan_id;
    ActionMode mode = ActionMode::PreviewOnly;
    std::string destination;
    std::vector<PlannedAction> actions;
    std::vector<std::string> warnings;
    std::vector<std::string> unresolved_conflicts;
    bool has_cycles = false;
    bool executable = false;
};

struct PlanRequest {
    ActionMode mode = ActionMode::PreviewOnly;
    /// Legacy explicit sources; when provided with no groups they become copies.
    std::vector<std::filesystem::path> paths;
    std::vector<DuplicateGroup> duplicate_groups;
    std::vector<ScannedEntry> unique_files;
    /// Content digest per entry id, produced by the grouping step, so every
    /// planned action carries the digest that must still hold at execution time.
    std::map<std::string, std::string> digest_by_id;
    std::filesystem::path destination;
    bool in_place = false;
    /// Directories detected as Git repositories. Any action touching them needs
    /// a per-action approval; a global "approve all" never covers them.
    std::vector<std::filesystem::path> git_roots;
    /// Optional filesystem used only to pick destination names that do not
    /// already exist. Never used to overwrite; when null, names are chosen
    /// purely from the plan and the validator reports collisions for review.
    IFileSystem* fs = nullptr;
};

struct Approval {
    std::string action_id;
    std::string source;
    std::string expected_digest;
};

struct ApprovalSet {
    std::string plan_id;
    std::vector<Approval> approvals;
};

/// Assemble a plan from already-built actions. Action ids are assigned "a0",
/// "a1", ... in the supplied order (callers that need different ids set them
/// beforehand; an empty id is filled in). The plan id is derived from the final
/// contents, and the cycle/executability checks run exactly as for a planner-made
/// plan, so approvals stay bound to this exact plan.
///
/// This exists so specialised workflows (useless-file quarantine) reuse the same
/// validated, journaled execution path instead of growing their own executor.
ActionPlan assemble_plan(std::vector<PlannedAction> actions, ActionMode mode,
                         const std::filesystem::path& destination,
                         const std::vector<std::string>& warnings = {});

class ActionPlanner {
public:
    ActionPlanner() = default;

    /// Build a plan from scan results. Pure: performs no I/O beyond reading
    /// nothing at all, so callers can preview safely.
    Result<ActionPlan> create_plan(const PlanRequest& request);

    /// Legacy entry point (kept for existing tests/callers): builds copy actions
    /// for explicit paths without a destination.
    Result<ActionPlan> create_plan(ActionMode mode,
                                   const std::vector<std::filesystem::path>& sources);

    /// True only when every approval-requiring action has an approval bound to
    /// this exact plan, action, source and expected digest.
    bool validate_approvals(const ActionPlan& plan, const ApprovalSet& approvals) const;

    /// Legacy id-only approval check (no plan binding); used by old tests.
    bool validate_approvals(const ActionPlan& plan,
                            const std::vector<std::string>& approved_ids) const;

    /// Brace-initialized id lists (`{}`, `{"a0"}`) resolve here instead of being
    /// ambiguous between the two overloads above.
    bool validate_approvals(const ActionPlan& plan,
                            std::initializer_list<std::string> approved_ids) const;

    /// Versioned, machine-readable plan format.
    static std::string serialize_plan(const ActionPlan& plan);
    /// Rejects unknown versions and any plan whose contents no longer match its
    /// stored plan_id (i.e. the file was edited after it was produced).
    static Result<ActionPlan> parse_plan(std::string_view json_text);
};

} // namespace dupli
