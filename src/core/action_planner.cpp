#include "dupli/action_planner.hpp"

#include "dupli/json.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <utility>

extern "C" {
#include "blake3.h"
}

namespace dupli {

namespace {

constexpr int kPlanFormatVersion = 1;
constexpr const char* kPlanFormat = "dupli-plan";

std::string hash_text(const std::string& text) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, text.data(), text.size());
    unsigned char bytes[32];
    blake3_hasher_finalize(&hasher, bytes, sizeof(bytes));
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (unsigned char byte : bytes) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0x0F]);
    }
    return hex;
}

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

/// Deterministic, never-overwriting destination naming. Two duplicates that
/// share a file name get "name_1.ext", "name_2.ext", ... in sorted order. When a
/// filesystem is supplied, names already present there are skipped too, so a
/// plan does not collide with an existing quarantine/collection folder.
std::filesystem::path unique_destination(const std::filesystem::path& directory,
                                         const std::filesystem::path& relative,
                                         std::set<std::string>& used, IFileSystem* fs) {
    auto available = [&used, fs](const std::filesystem::path& candidate) {
        if (!used.insert(candidate.generic_string()).second) return false;
        if (!fs) return true;
        auto exists = fs->exists(candidate);
        return exists.ok() && !exists.value();
    };

    std::filesystem::path candidate = (directory / relative).lexically_normal();
    if (available(candidate)) return candidate;

    const std::string stem = relative.stem().string();
    // Hidden files have no extension in the std::filesystem sense (.env -> .env),
    // so the suffix is appended without splitting them.
    const std::string extension = relative.extension().string();
    const auto parent = relative.parent_path();
    for (int suffix = 1; suffix < 100000; ++suffix) {
        std::filesystem::path renamed = parent / (stem + "_" + std::to_string(suffix) + extension);
        candidate = (directory / renamed).lexically_normal();
        if (available(candidate)) return candidate;
    }
    return candidate;
}

/// Detect dependency cycles. File-level plans normally have none, but plans can
/// be loaded from disk, so this must not be assumed.
bool detect_cycle(const std::vector<PlannedAction>& actions) {
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < actions.size(); ++i) index[actions[i].id] = i;

    enum class Mark { Unvisited, InProgress, Done };
    std::vector<Mark> marks(actions.size(), Mark::Unvisited);

    // Iterative DFS to avoid recursion depth issues on very large plans.
    for (size_t start = 0; start < actions.size(); ++start) {
        if (marks[start] != Mark::Unvisited) continue;
        std::vector<std::pair<size_t, size_t>> stack; // action index, next dependency index
        stack.emplace_back(start, 0);
        marks[start] = Mark::InProgress;
        while (!stack.empty()) {
            auto& [current, dependency_index] = stack.back();
            const auto& dependencies = actions[current].dependencies;
            if (dependency_index >= dependencies.size()) {
                marks[current] = Mark::Done;
                stack.pop_back();
                continue;
            }
            const std::string& dependency_id = dependencies[dependency_index++];
            auto it = index.find(dependency_id);
            if (it == index.end()) continue; // unknown dependency is checked elsewhere
            if (marks[it->second] == Mark::InProgress) return true;
            if (marks[it->second] == Mark::Done) continue;
            marks[it->second] = Mark::InProgress;
            stack.emplace_back(it->second, 0);
        }
    }
    return false;
}

std::string plan_signature(const ActionPlan& plan) {
    std::ostringstream out;
    out << kPlanFormat << '|' << kPlanFormatVersion << '|' << to_string(plan.mode) << '|'
        << path_to_utf8(plan.destination) << '|';
    for (const auto& action : plan.actions) {
        out << action.id << '|' << to_string(action.kind) << '|' << path_to_utf8(action.source)
            << '|' << path_to_utf8(action.destination) << '|' << action.expected_digest << '|'
            << action.expected_size << '|' << (action.requires_approval ? '1' : '0') << '|';
        for (const auto& dependency : action.dependencies) out << dependency << ',';
        out << ';';
    }
    for (const auto& conflict : plan.unresolved_conflicts) out << "conflict:" << conflict << ';';
    return out.str();
}

void finalize_plan(ActionPlan& plan) {
    plan.has_cycles = detect_cycle(plan.actions);
    plan.executable = plan.mode != ActionMode::PreviewOnly && !plan.has_cycles &&
                      plan.unresolved_conflicts.empty();
    plan.plan_id = hash_text(plan_signature(plan));
}

} // namespace

std::string to_string(ActionMode mode) {
    switch (mode) {
    case ActionMode::PreviewOnly: return "preview";
    case ActionMode::QuarantineDuplicates: return "quarantine";
    case ActionMode::CreateUniqueCollection: return "unique";
    case ActionMode::InPlaceMerge: return "in-place-merge";
    }
    return "preview";
}

bool parse_action_mode(std::string_view text, ActionMode& out) {
    if (text == "preview" || text == "preview-only") { out = ActionMode::PreviewOnly; return true; }
    if (text == "quarantine" || text == "duplicates") {
        out = ActionMode::QuarantineDuplicates;
        return true;
    }
    if (text == "unique" || text == "unique-collection") {
        out = ActionMode::CreateUniqueCollection;
        return true;
    }
    if (text == "in-place-merge" || text == "merge") { out = ActionMode::InPlaceMerge; return true; }
    return false;
}

std::string to_string(ActionKind kind) {
    return kind == ActionKind::Move ? "move" : "copy";
}

bool parse_action_kind(std::string_view text, ActionKind& out) {
    if (text == "move") { out = ActionKind::Move; return true; }
    if (text == "copy") { out = ActionKind::Copy; return true; }
    return false;
}

ActionPlan assemble_plan(std::vector<PlannedAction> actions, ActionMode mode,
                         const std::filesystem::path& destination,
                         const std::vector<std::string>& warnings) {
    ActionPlan plan;
    plan.mode = mode;
    plan.destination = path_to_utf8(destination);
    plan.warnings = warnings;

    for (size_t index = 0; index < actions.size(); ++index) {
        if (actions[index].id.empty()) actions[index].id = "a" + std::to_string(index);
    }
    plan.actions = std::move(actions);
    finalize_plan(plan);
    return plan;
}

Result<ActionPlan> ActionPlanner::create_plan(const PlanRequest& request) {
    ActionPlan plan;
    plan.mode = request.mode;
    plan.destination = path_to_utf8(request.destination);

    // Legacy mode: explicit sources with no classification available. Kept for
    // API compatibility; the CLI always supplies grouped/unique entries so that
    // every action carries a verified digest.
    if (request.duplicate_groups.empty() && request.unique_files.empty()) {
        return create_plan(request.mode, request.paths);
    }

    if (request.mode == ActionMode::QuarantineDuplicates ||
        request.mode == ActionMode::CreateUniqueCollection ||
        request.mode == ActionMode::InPlaceMerge) {
        if (request.destination.empty()) {
            return Error{ErrorCode::InvalidArgument,
                         "A destination folder is required for this operation", "", "create_plan"};
        }
    }

    if (request.mode == ActionMode::PreviewOnly) {
        plan.warnings.push_back("Preview only - no mutations will occur");
        finalize_plan(plan);
        return plan;
    }

    if (request.mode == ActionMode::InPlaceMerge && !request.in_place) {
        // In-place merge is an advanced, explicitly-requested operation; a
        // caller that did not ask for it gets an empty preview instead of a
        // mutation plan.
        plan.mode = ActionMode::PreviewOnly;
        plan.warnings.push_back(
            "In-place merge was requested without the advanced in-place approval; no mutations "
            "were planned");
        finalize_plan(plan);
        return plan;
    }

    std::set<std::string> used_destinations;
    size_t counter = 0;

    auto make_approval_flags = [&request](PlannedAction& action) {
        for (const auto& git_root : request.git_roots) {
            if (is_within(git_root, action.source) || is_within(git_root, action.destination)) {
                action.requires_approval = true;
                action.approval_reason =
                    "Touches a Git repository; repository moves/merges need per-operation approval";
                return;
            }
        }
    };

    if (request.mode == ActionMode::QuarantineDuplicates) {
        for (const auto& group : request.duplicate_groups) {
            if (group.files.size() < 2) continue;
            // One file is kept and every other copy is quarantined. The keeper is
            // the shallowest, lowest-root copy (entry_sort_key_less), which is
            // also the copy the report suggests, so the two never disagree.
            const auto keeper_it =
                std::min_element(group.files.begin(), group.files.end(), entry_sort_key_less);
            const auto& keeper = *keeper_it;
            for (auto it = group.files.begin(); it != group.files.end(); ++it) {
                if (it == keeper_it) continue;
                const auto& duplicate = *it;
                if (request.destination.empty()) {
                    plan.unresolved_conflicts.push_back(
                        "No destination folder given for quarantining: " +
                        path_to_utf8(duplicate.path));
                    continue;
                }
                PlannedAction action;
                action.id = "a" + std::to_string(counter++);
                action.kind = ActionKind::Move;
                action.source = duplicate.path;
                action.destination = unique_destination(request.destination,
                                                        duplicate.path.filename(),
                                                        used_destinations, request.fs);
                action.expected_digest = group.digest_hex;
                action.expected_size = static_cast<uintmax_t>(group.size);
                action.rationale = "Exact byte-for-byte duplicate of " + path_to_utf8(keeper.path);
                make_approval_flags(action);
                plan.actions.push_back(std::move(action));
            }
        }
        if (plan.actions.empty() && request.duplicate_groups.empty()) {
            plan.warnings.push_back("No duplicate files were found; nothing to quarantine");
        }
    }

    if (request.mode == ActionMode::CreateUniqueCollection) {
        for (const auto& file : request.unique_files) {
            if (file.excluded || !file.error.empty() || !file.is_file) continue;
            if (request.destination.empty()) {
                plan.unresolved_conflicts.push_back(
                    "No destination folder given for the unique collection: " +
                    path_to_utf8(file.path));
                continue;
            }
            PlannedAction action;
            action.id = "a" + std::to_string(counter++);
            action.kind = ActionKind::Copy;
            action.source = file.path;
            // Preserve the relative layout so the collection is browsable and
            // files with equal names in different projects do not clash.
            std::filesystem::path relative = file.relative_path;
            if (relative.empty() || relative == ".") relative = file.path.filename();
            action.destination =
                unique_destination(request.destination, relative, used_destinations, request.fs);
            auto digest_it = request.digest_by_id.find(file.id);
            action.expected_digest = digest_it == request.digest_by_id.end() ? "" : digest_it->second;
            action.expected_size = file.size;
            action.rationale =
                "Content appears exactly once; copied so the original stays untouched";
            make_approval_flags(action);
            plan.actions.push_back(std::move(action));
        }
        if (plan.actions.empty() && request.unique_files.empty()) {
            plan.warnings.push_back("No unique files were found; the collection would be empty");
        }
    }

    if (request.mode == ActionMode::InPlaceMerge) {
        // Advanced merge: pull unique files that live outside the surviving root
        // into it, preserving relative paths. Duplicates are left alone so the
        // user can quarantine them in a separate, reviewed step.
        for (const auto& file : request.unique_files) {
            if (file.excluded || !file.error.empty() || !file.is_file) continue;
            if (is_within(request.destination, file.path)) continue; // already inside
            PlannedAction action;
            action.id = "a" + std::to_string(counter++);
            action.kind = ActionKind::Move;
            action.source = file.path;
            std::filesystem::path relative = file.relative_path;
            if (relative.empty() || relative == ".") relative = file.path.filename();
            action.destination =
                unique_destination(request.destination, relative, used_destinations, request.fs);
            auto digest_it = request.digest_by_id.find(file.id);
            action.expected_digest = digest_it == request.digest_by_id.end() ? "" : digest_it->second;
            action.expected_size = file.size;
            action.requires_approval = true;
            action.approval_reason =
                "In-place merge is an advanced operation; each moved file needs approval";
            action.rationale = "Unique file is merged into the surviving folder tree";
            make_approval_flags(action);
            plan.actions.push_back(std::move(action));
        }
        if (plan.actions.empty()) {
            plan.warnings.push_back("Nothing outside the destination needs to be merged");
        }
    }

    finalize_plan(plan);
    return plan;
}

Result<ActionPlan> ActionPlanner::create_plan(ActionMode mode,
                                              const std::vector<std::filesystem::path>& sources) {
    ActionPlan plan;
    plan.mode = mode;

    if (mode == ActionMode::PreviewOnly) {
        plan.warnings.push_back("Preview only - no mutations will occur");
        finalize_plan(plan);
        return plan;
    }
    if (mode == ActionMode::InPlaceMerge) {
        plan.warnings.push_back("In-place merge requires explicit approval");
    }

    size_t counter = 0;
    for (const auto& source : sources) {
        PlannedAction action;
        action.id = "action_" + std::to_string(counter++);
        action.kind = ActionKind::Copy;
        action.source = source;
        action.requires_approval = (mode == ActionMode::InPlaceMerge);
        if (action.requires_approval) {
            action.approval_reason = "In-place operations require explicit approval";
        }
        plan.actions.push_back(std::move(action));
    }
    finalize_plan(plan);
    return plan;
}

bool ActionPlanner::validate_approvals(const ActionPlan& plan, const ApprovalSet& approvals) const {
    for (const auto& action : plan.actions) {
        if (!action.requires_approval) continue;

        // A global "approve all" must not slide past a gated action: the approval
        // has to name this action, this source and this expected content, for
        // this exact plan.
        bool matched = false;
        for (const auto& approval : approvals.approvals) {
            if (approval.action_id != action.id) continue;
            if (approval.source != path_to_utf8(action.source)) continue;
            if (!approval.expected_digest.empty() &&
                approval.expected_digest != action.expected_digest) {
                continue;
            }
            matched = true;
            break;
        }
        if (!matched) return false;
    }
    return plan.plan_id.empty() || approvals.plan_id.empty() || approvals.plan_id == plan.plan_id;
}

bool ActionPlanner::validate_approvals(const ActionPlan& plan,
                                       const std::vector<std::string>& approved_ids) const {
    for (const auto& action : plan.actions) {
        if (!action.requires_approval) continue;
        if (std::find(approved_ids.begin(), approved_ids.end(), action.id) == approved_ids.end()) {
            return false;
        }
    }
    return true;
}

bool ActionPlanner::validate_approvals(const ActionPlan& plan,
                                       std::initializer_list<std::string> approved_ids) const {
    return validate_approvals(plan, std::vector<std::string>(approved_ids));
}

std::string ActionPlanner::serialize_plan(const ActionPlan& plan) {
    json::Value root;
    root.set("format", kPlanFormat);
    root.set("version", kPlanFormatVersion);
    root.set("plan_id", plan.plan_id);
    root.set("mode", to_string(plan.mode));
    root.set("destination", plan.destination);
    root.set("executable", plan.executable);
    root.set("has_cycles", plan.has_cycles);

    json::Value warnings(json::Value::Array{});
    for (const auto& warning : plan.warnings) warnings.push(warning);
    root.set("warnings", warnings);

    json::Value conflicts(json::Value::Array{});
    for (const auto& conflict : plan.unresolved_conflicts) conflicts.push(conflict);
    root.set("unresolved_conflicts", conflicts);

    json::Value actions(json::Value::Array{});
    for (const auto& action : plan.actions) {
        json::Value item;
        item.set("id", action.id);
        item.set("kind", to_string(action.kind));
        item.set("source", path_to_utf8(action.source));
        item.set("destination", path_to_utf8(action.destination));
        item.set("expected_digest", action.expected_digest);
        item.set("expected_size", action.expected_size);
        item.set("requires_approval", action.requires_approval);
        item.set("approval_reason", action.approval_reason);
        item.set("rationale", action.rationale);
        json::Value dependencies(json::Value::Array{});
        for (const auto& dependency : action.dependencies) dependencies.push(dependency);
        item.set("dependencies", dependencies);
        actions.push(std::move(item));
    }
    root.set("actions", actions);
    return json::dump(root) + "\n";
}

Result<ActionPlan> ActionPlanner::parse_plan(std::string_view json_text) {
    json::ParseError parse_error;
    auto parsed = json::parse(json_text, &parse_error);
    if (!parsed) {
        return Error{ErrorCode::InvalidArgument,
                     "Plan file is not valid JSON at offset " + std::to_string(parse_error.offset) +
                         ": " + parse_error.message,
                     "", "parse_plan"};
    }
    const auto& root = *parsed;
    if (root["format"].as_string() != kPlanFormat) {
        return Error{ErrorCode::Unsupported, "Not a dupli plan file", "", "parse_plan"};
    }
    if (root["version"].as_int() != kPlanFormatVersion) {
        return Error{ErrorCode::Unsupported,
                     "Unsupported plan version " + std::to_string(root["version"].as_int()),
                     "", "parse_plan"};
    }

    ActionPlan plan;
    plan.mode = ActionMode::PreviewOnly;
    if (!parse_action_mode(root["mode"].as_string(), plan.mode)) {
        return Error{ErrorCode::InvalidArgument, "Unknown plan mode", "", "parse_plan"};
    }
    plan.destination = root["destination"].as_string();
    for (const auto& warning : root["warnings"].as_array()) {
        plan.warnings.push_back(warning.as_string());
    }
    for (const auto& conflict : root["unresolved_conflicts"].as_array()) {
        plan.unresolved_conflicts.push_back(conflict.as_string());
    }

    for (const auto& item : root["actions"].as_array()) {
        PlannedAction action;
        action.id = item["id"].as_string();
        if (action.id.empty()) {
            return Error{ErrorCode::InvalidArgument, "Plan action has no id", "", "parse_plan"};
        }
        if (!parse_action_kind(item["kind"].as_string(), action.kind)) {
            return Error{ErrorCode::InvalidArgument,
                         "Unknown action kind for " + action.id, "", "parse_plan"};
        }
        action.source = path_from_utf8(item["source"].as_string());
        action.destination = path_from_utf8(item["destination"].as_string());
        if (action.source.empty() || action.destination.empty()) {
            return Error{ErrorCode::InvalidArgument,
                         "Plan action " + action.id + " has an empty source or destination",
                         "", "parse_plan"};
        }
        action.expected_digest = item["expected_digest"].as_string();
        action.expected_size = static_cast<uintmax_t>(item["expected_size"].as_int());
        action.requires_approval = item["requires_approval"].as_bool();
        action.approval_reason = item["approval_reason"].as_string();
        action.rationale = item["rationale"].as_string();
        for (const auto& dependency : item["dependencies"].as_array()) {
            action.dependencies.push_back(dependency.as_string());
        }
        plan.actions.push_back(std::move(action));
    }

    // Recompute and compare: a hand-edited or corrupted plan must not be
    // executed with approvals that were granted for different contents.
    const std::string stored_id = root["plan_id"].as_string();
    finalize_plan(plan);
    if (!stored_id.empty() && stored_id != plan.plan_id) {
        return Error{ErrorCode::Stale,
                     "Plan contents changed since it was saved; re-create the plan and re-approve",
                     "", "parse_plan"};
    }
    return plan;
}

} // namespace dupli
