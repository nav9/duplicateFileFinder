#include "dupli/action_planner.hpp"
#include "dupli/fake_filesystem.hpp"
#include "dupli/json.hpp"
#include "test_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace dupli;

namespace {

ScannedEntry make_entry(const std::string& path, const std::string& relative,
                        size_t root_index = 0, uintmax_t size = 10) {
    ScannedEntry entry;
    entry.path = path;
    entry.relative_path = relative;
    entry.root_index = root_index;
    entry.is_file = true;
    entry.size = size;
    entry.id = std::to_string(root_index) + ":" + relative;
    return entry;
}

DuplicateGroup make_group(const std::string& digest,
                          const std::vector<std::string>& paths) {
    DuplicateGroup group;
    group.digest_hex = digest;
    group.size = 5;
    for (const auto& path : paths) {
        group.files.push_back(make_entry(path, std::filesystem::path(path).filename().string()));
    }
    return group;
}

void test_quarantine_plan() {
    ActionPlanner planner;
    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = "/quarantine";
    request.duplicate_groups = {
        make_group("aaaa", {"/a/x.txt", "/b/x.txt", "/c/x.txt"}),
    };

    auto plan = planner.create_plan(request);
    CHECK(plan.ok());
    CHECK(plan.value().executable);
    CHECK_EQ(plan.value().plan_id.size(), size_t(64));
    CHECK_EQ(plan.value().actions.size(), size_t(2));

    // The first copy is kept; only the later copies are moved.
    for (const auto& action : plan.value().actions) {
        CHECK_EQ(action.kind, ActionKind::Move);
        CHECK(action.source != std::filesystem::path("/a/x.txt"));
        CHECK_EQ(action.expected_digest, std::string("aaaa"));
        CHECK(!action.requires_approval);
    }
    CHECK_EQ(plan.value().actions[0].destination, std::filesystem::path("/quarantine/x.txt"));
    CHECK_EQ(plan.value().actions[1].destination, std::filesystem::path("/quarantine/x_1.txt"));

    // Deterministic: building the same plan twice yields the same id.
    auto again = planner.create_plan(request);
    CHECK(again.ok());
    CHECK_EQ(again.value().plan_id, plan.value().plan_id);
    std::cout << "planner: quarantine plan\n";
}

void test_unique_plan() {
    ActionPlanner planner;
    PlanRequest request;
    request.mode = ActionMode::CreateUniqueCollection;
    request.destination = "/collection";
    request.unique_files = {make_entry("/src/docs/readme.txt", "docs/readme.txt", 0, 7)};
    request.digest_by_id[request.unique_files[0].id] = "bbbb";

    auto plan = planner.create_plan(request);
    CHECK(plan.ok());
    CHECK(plan.value().executable);
    CHECK_EQ(plan.value().actions.size(), size_t(1));
    const auto& action = plan.value().actions[0];
    CHECK_EQ(action.kind, ActionKind::Copy);
    CHECK_EQ(action.destination,
             std::filesystem::path("/collection/docs/readme.txt"));
    CHECK_EQ(action.expected_digest, std::string("bbbb"));
    std::cout << "planner: unique collection plan\n";
}

void test_preview_and_merge() {
    ActionPlanner planner;
    PlanRequest preview;
    preview.mode = ActionMode::PreviewOnly;
    preview.duplicate_groups = {make_group("aaaa", {"/a/x.txt", "/b/x.txt"})};
    auto preview_plan = planner.create_plan(preview);
    CHECK(preview_plan.ok());
    CHECK(preview_plan.value().actions.empty());
    CHECK(!preview_plan.value().executable);

    PlanRequest merge;
    merge.mode = ActionMode::InPlaceMerge;
    merge.in_place = true;
    merge.destination = "/dest";
    merge.unique_files = {make_entry("/src/only.txt", "only.txt"),
                          make_entry("/dest/inside.txt", "inside.txt")};
    merge.digest_by_id[merge.unique_files[0].id] = "cccc";
    auto merge_plan = planner.create_plan(merge);
    CHECK(merge_plan.ok());
    CHECK_EQ(merge_plan.value().actions.size(), size_t(1));
    CHECK_EQ(merge_plan.value().actions[0].kind, ActionKind::Move);
    CHECK(merge_plan.value().actions[0].requires_approval);

    // Without the advanced in-place flag the planner must not plan mutations.
    merge.in_place = false;
    auto downgraded = planner.create_plan(merge);
    CHECK(downgraded.ok());
    CHECK(downgraded.value().actions.empty());
    CHECK(!downgraded.value().executable);
    std::cout << "planner: preview and merge modes\n";
}

void test_existing_destination_names_are_avoided() {
    FakeFileSystem fs;
    fs.add_file("/quarantine/x.txt", "already here");

    ActionPlanner planner;
    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = "/quarantine";
    request.duplicate_groups = {make_group("aaaa", {"/a/x.txt", "/b/x.txt"})};
    request.fs = &fs;

    auto plan = planner.create_plan(request);
    CHECK(plan.ok());
    CHECK_EQ(plan.value().actions.size(), size_t(1));
    // The existing file is never a destination, and no overwrite is planned.
    CHECK_EQ(plan.value().actions[0].destination,
             std::filesystem::path("/quarantine/x_1.txt"));
    std::cout << "planner: existing destination names avoided\n";
}

void test_git_actions_require_approval() {
    ActionPlanner planner;
    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = "/quarantine";
    request.duplicate_groups = {
        make_group("aaaa", {"/repo/keep.txt", "/repo/dup.txt"}),
    };
    request.git_roots = {"/repo"};

    auto plan = planner.create_plan(request);
    CHECK(plan.ok());
    CHECK_EQ(plan.value().actions.size(), size_t(1));
    CHECK(plan.value().actions[0].requires_approval);
    CHECK(plan.value().actions[0].approval_reason.find("Git") != std::string::npos);

    ApprovalSet empty;
    empty.plan_id = plan.value().plan_id;
    CHECK(!planner.validate_approvals(plan.value(), empty));

    ApprovalSet approved;
    approved.plan_id = plan.value().plan_id;
    approved.approvals.push_back(Approval{plan.value().actions[0].id,
                                          path_to_utf8(plan.value().actions[0].source),
                                          std::string()});
    CHECK(planner.validate_approvals(plan.value(), approved));

    // An approval bound to a different plan must not unlock this one.
    ApprovalSet stale = approved;
    stale.plan_id = "different-plan";
    CHECK(!planner.validate_approvals(plan.value(), stale));
    std::cout << "planner: git approval gating\n";
}

void test_plan_serialization() {
    ActionPlanner planner;
    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = "/quarantine";
    request.duplicate_groups = {make_group("aaaa", {"/a/x.txt", "/b/x.txt"})};

    auto plan = planner.create_plan(request);
    CHECK(plan.ok());

    const std::string text = ActionPlanner::serialize_plan(plan.value());
    auto parsed = ActionPlanner::parse_plan(text);
    CHECK(parsed.ok());
    CHECK_EQ(parsed.value().plan_id, plan.value().plan_id);
    CHECK_EQ(parsed.value().actions.size(), plan.value().actions.size());
    CHECK_EQ(parsed.value().actions[0].destination, plan.value().actions[0].destination);
    CHECK_EQ(parsed.value().mode, ActionMode::QuarantineDuplicates);

    // Editing a saved plan invalidates it instead of being silently accepted.
    auto value = json::parse(text);
    CHECK(value.has_value());
    json::Value tampered = *value;
    json::Value actions(json::Value::Array{});
    for (const auto& action : tampered["actions"].as_array()) {
        json::Value copy = action;
        copy.set("destination", "/elsewhere/x.txt");
        actions.push(std::move(copy));
    }
    tampered.set("actions", actions);
    auto rejected = ActionPlanner::parse_plan(json::dump(tampered));
    CHECK(rejected.has_error());
    CHECK_EQ(rejected.error().code, ErrorCode::Stale);

    CHECK(ActionPlanner::parse_plan("{not json}").has_error());
    CHECK(ActionPlanner::parse_plan("{}").has_error());
    std::cout << "planner: serialize/parse and tamper rejection\n";
}

void test_cycle_detection() {
    json::Value root;
    root.set("format", "dupli-plan");
    root.set("version", 1);
    root.set("plan_id", "");
    root.set("mode", "quarantine");
    root.set("destination", "/q");
    json::Value actions(json::Value::Array{});
    for (const std::string id : {"a0", "a1"}) {
        json::Value action;
        action.set("id", id);
        action.set("kind", "move");
        action.set("source", "/src/" + id);
        action.set("destination", "/q/" + id);
        action.set("expected_digest", "aaaa");
        action.set("expected_size", 5);
        json::Value dependencies(json::Value::Array{});
        dependencies.push(id == "a0" ? "a1" : "a0");
        action.set("dependencies", dependencies);
        actions.push(std::move(action));
    }
    root.set("actions", actions);

    auto plan = ActionPlanner::parse_plan(json::dump(root));
    CHECK(plan.ok());
    CHECK(plan.value().has_cycles);
    CHECK(!plan.value().executable);
    std::cout << "planner: dependency cycle rejected\n";
}

void test_legacy_plan_api() {
    ActionPlanner planner;
    auto preview = planner.create_plan(ActionMode::PreviewOnly, {"/a/file.txt"});
    CHECK(preview.ok());
    CHECK(!preview.value().executable);

    auto merge = planner.create_plan(ActionMode::InPlaceMerge, {"/a/file.txt"});
    CHECK(merge.ok());
    CHECK(merge.value().executable);
    CHECK(!planner.validate_approvals(merge.value(), {}));
    CHECK(planner.validate_approvals(merge.value(), {"action_0"}));
    std::cout << "planner: legacy API\n";
}

} // namespace

int main() {
    test_quarantine_plan();
    test_unique_plan();
    test_preview_and_merge();
    test_existing_destination_names_are_avoided();
    test_git_actions_require_approval();
    test_plan_serialization();
    test_cycle_detection();
    test_legacy_plan_api();
    std::cout << "All planner tests passed\n";
    return 0;
}
