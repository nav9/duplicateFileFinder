#include "dupli/executor.hpp"
#include "dupli/fake_filesystem.hpp"
#include "dupli/grouping.hpp"
#include "dupli/journal.hpp"
#include "dupli/scanner.hpp"
#include "dupli/validator.hpp"
#include "test_utils.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace dupli;

namespace {

struct PlannedSetup {
    ActionPlan plan;
    ApprovalSet approvals;
};

/// Scan, group and plan a quarantine operation for a fake filesystem.
PlannedSetup make_quarantine_plan(FakeFileSystem& fs, const std::string& root,
                                  const std::string& destination) {
    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {root};
    auto scanned = scanner.scan(options);
    CHECK(scanned.ok());

    Blake3Hasher hasher;
    DuplicateGrouper grouper(fs, hasher);
    GroupingResult grouping = grouper.group(scanned.value().files());

    ActionPlanner planner;
    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = destination;
    request.duplicate_groups = grouping.groups;
    request.unique_files = grouping.unique_files;
    request.digest_by_id = grouping.digest_by_id;

    auto plan = planner.create_plan(request);
    CHECK(plan.ok());

    PlannedSetup setup;
    setup.plan = plan.value();
    setup.approvals.plan_id = setup.plan.plan_id;
    for (const auto& action : setup.plan.actions) {
        if (!action.requires_approval) continue;
        setup.approvals.approvals.push_back(
            Approval{action.id, path_to_utf8(action.source), action.expected_digest});
    }
    return setup;
}

void test_quarantine_move_and_undo() {
    FakeFileSystem fs;
    fs.add_file("/data/a/x.txt", "same-content");
    fs.add_file("/data/b/x.txt", "same-content");

    Blake3Hasher hasher;
    SimpleJournal journal;
    auto setup = make_quarantine_plan(fs, "/data", "/quarantine");
    CHECK_EQ(setup.plan.actions.size(), size_t(1));
    CHECK_EQ(setup.plan.actions[0].source, std::filesystem::path("/data/b/x.txt"));

    Validator validator(fs, hasher);
    auto validation = validator.validate_plan(setup.plan, setup.approvals);
    CHECK(validation.ok());
    CHECK(validation.value().valid);

    Executor executor(fs, hasher, journal);
    auto execution = executor.execute(setup.plan, setup.approvals);
    CHECK(execution.ok());
    CHECK(execution.value().success);

    auto moved = fs.exists("/quarantine/x.txt");
    CHECK(moved.ok() && moved.value());
    auto original = fs.exists("/data/b/x.txt");
    CHECK(original.ok() && !original.value());
    auto keeper = fs.exists("/data/a/x.txt");
    CHECK(keeper.ok() && keeper.value());

    auto records = journal.entries();
    CHECK(records.ok());
    CHECK_EQ(latest_records(records.value()).front().state, JournalState::Completed);

    FakeClock clock("2026-11-01T00:00:00Z");
    RecoveryEngine recovery(fs, hasher);
    auto undone = recovery.undo(journal, clock);
    CHECK(undone.ok());
    CHECK_EQ(undone.value().undone, size_t(1));
    CHECK(fs.exists("/data/b/x.txt").value());
    CHECK(!fs.exists("/quarantine/x.txt").value());
    std::cout << "execution: quarantine move and undo\n";
}

void test_destination_collision_never_overwrites() {
    FakeFileSystem fs;
    fs.add_file("/data/a/x.txt", "same-content");
    fs.add_file("/data/b/x.txt", "same-content");
    fs.add_file("/quarantine/x.txt", "unrelated-content");

    Blake3Hasher hasher;
    SimpleJournal journal;
    auto setup = make_quarantine_plan(fs, "/data", "/quarantine");

    Validator validator(fs, hasher);
    auto validation = validator.validate_plan(setup.plan, setup.approvals);
    CHECK(validation.ok());
    CHECK(!validation.value().valid);

    Executor executor(fs, hasher, journal);
    auto execution = executor.execute(setup.plan, setup.approvals);
    CHECK(execution.ok());
    CHECK(!execution.value().success);
    CHECK_EQ(execution.value().completed_actions.size(), size_t(0));

    auto opened = fs.open_read("/quarantine/x.txt");
    CHECK(opened.ok());
    auto content = opened.value()->read_all();
    CHECK(content.ok());
    std::string text(reinterpret_cast<const char*>(content.value().data()),
                     content.value().size());
    CHECK_EQ(text, std::string("unrelated-content"));
    CHECK(fs.exists("/data/b/x.txt").value());
    std::cout << "execution: destination collision is never overwritten\n";
}

void test_stale_source_blocks_execution() {
    FakeFileSystem fs;
    fs.add_file("/data/a/x.txt", "same-content");
    fs.add_file("/data/b/x.txt", "same-content");

    Blake3Hasher hasher;
    SimpleJournal journal;
    auto setup = make_quarantine_plan(fs, "/data", "/quarantine");

    // Simulate a file that changed after the plan was produced.
    fs.add_file("/data/b/x.txt", "changed-content");

    Executor executor(fs, hasher, journal);
    auto execution = executor.execute(setup.plan, setup.approvals);
    CHECK(execution.ok());
    CHECK(!execution.value().success);
    CHECK_EQ(execution.value().completed_actions.size(), size_t(0));
    CHECK(!fs.exists("/quarantine/x.txt").value());
    std::cout << "execution: stale source blocks mutation\n";
}

void test_missing_approval_blocks_git_action() {
    FakeFileSystem fs;
    fs.add_file("/repo/a/x.txt", "same-content");
    fs.add_file("/repo/b/x.txt", "same-content");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/repo"};
    auto scanned = scanner.scan(options);
    CHECK(scanned.ok());

    Blake3Hasher hasher;
    DuplicateGrouper grouper(fs, hasher);
    GroupingResult grouping = grouper.group(scanned.value().files());

    ActionPlanner planner;
    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = "/quarantine";
    request.duplicate_groups = grouping.groups;
    request.git_roots = {"/repo"};
    auto plan = planner.create_plan(request);
    CHECK(plan.ok());
    CHECK(plan.value().actions[0].requires_approval);

    SimpleJournal journal;
    Executor executor(fs, hasher, journal);
    ApprovalSet no_approvals;
    no_approvals.plan_id = plan.value().plan_id;
    auto blocked = executor.execute(plan.value(), no_approvals);
    CHECK(blocked.ok());
    CHECK(!blocked.value().success);
    CHECK(fs.exists("/repo/b/x.txt").value());
    std::cout << "execution: missing approval blocks gated action\n";
}

void test_cross_device_copy_retains_source_and_undo() {
    FakeFileSystem fs;
    fs.add_file("/data/a/x.txt", "same-content");
    fs.add_file("/data/b/x.txt", "same-content");

    Blake3Hasher hasher;
    SimpleJournal journal;
    auto setup = make_quarantine_plan(fs, "/data", "/quarantine");
    fs.set_same_device(false); // After planning: force the copy path, as on another volume.

    Executor executor(fs, hasher, journal);
    auto execution = executor.execute(setup.plan, setup.approvals);
    CHECK(execution.ok());
    CHECK(execution.value().success);
    CHECK_EQ(execution.value().sources_retained.size(), size_t(1));

    // Source retained by policy; destination verified copy exists.
    CHECK(fs.exists("/data/b/x.txt").value());
    auto copied = fs.open_read("/quarantine/x.txt");
    CHECK(copied.ok());
    auto content = copied.value()->read_all();
    CHECK(content.ok());
    CHECK_EQ(content.value().size(), std::string("same-content").size());

    auto records = journal.entries();
    CHECK(records.ok());
    CHECK_EQ(latest_records(records.value()).front().kind, std::string("copy"));

    FakeClock clock("2026-11-01T00:00:00Z");
    RecoveryEngine recovery(fs, hasher);
    auto undone = recovery.undo(journal, clock);
    CHECK(undone.ok());
    CHECK_EQ(undone.value().undone, size_t(1));
    CHECK(!fs.exists("/quarantine/x.txt").value());
    CHECK(fs.exists("/data/b/x.txt").value()); // Original untouched throughout.
    std::cout << "execution: cross-device copy retains source, undo removes copy\n";
}

void test_corrupt_copy_is_rejected() {
    FakeFileSystem fs;
    fs.add_file("/data/a/x.txt", "same-content");
    fs.add_file("/data/b/x.txt", "same-content");

    Blake3Hasher hasher;
    SimpleJournal journal;
    auto setup = make_quarantine_plan(fs, "/data", "/quarantine");
    fs.set_same_device(false);
    fs.set_corrupt_copy(true);

    Executor executor(fs, hasher, journal);
    auto execution = executor.execute(setup.plan, setup.approvals);
    CHECK(execution.ok());
    CHECK(!execution.value().success);
    CHECK_EQ(execution.value().failed_actions.size(), size_t(1));
    CHECK(!fs.exists("/quarantine/x.txt").value());
    CHECK(fs.exists("/data/b/x.txt").value());
    std::cout << "execution: corrupt copy rejected and cleaned up\n";
}

void test_file_journal_durability_and_recovery() {
    testutil::TempDir temp("journal");
    const auto journal_path = temp.get() / "ops.jsonl";
    FakeClock clock("2026-11-01T10:00:00Z");

    {
        FileJournal journal(journal_path, clock);
        JournalRecord started;
        started.id = "j_1";
        started.action_id = "a0";
        started.kind = "move";
        started.source = "/data/b/x.txt";
        started.destination = "/quarantine/x.txt";
        started.expected_digest = "abcd";
        started.state = JournalState::Started;
        CHECK(journal.append(started).ok());
    }

    // A torn final line (as after a crash) must not break reading.
    {
        std::ofstream file(journal_path, std::ios::app);
        file << "{\"format\":\"dupli-journal\",\"version\":1,\"id\":\"j_2\"";
    }

    {
        FileJournal reopened(journal_path, clock);
        auto records = reopened.entries();
        CHECK(records.ok());
        CHECK_EQ(records.value().size(), size_t(1));
        CHECK_EQ(records.value()[0].timestamp, std::string("2026-11-01T10:00:00Z"));

        auto pending = reopened.recover_pending();
        CHECK(pending.ok());
        CHECK_EQ(pending.value().size(), size_t(1));

        CHECK(reopened.complete_action("j_1").ok());
        auto updated = reopened.entries();
        CHECK(updated.ok());
        CHECK_EQ(updated.value().size(), size_t(2));
        CHECK_EQ(latest_records(updated.value())[0].state, JournalState::Completed);
    }

    // Recovery inspects the filesystem instead of trusting a missing flag.
    FakeFileSystem fs;
    fs.add_file("/data/b/x.txt", "content");
    fs.add_directory("/quarantine");
    Blake3Hasher hasher;
    FileJournal journal(journal_path, clock);
    RecoveryEngine recovery(fs, hasher);

    {
        // Destination absent, source present -> safe to retry.
        JournalRecord started;
        started.id = "j_3";
        started.action_id = "a1";
        started.kind = "move";
        started.source = "/data/b/x.txt";
        started.destination = "/quarantine/x.txt";
        started.state = JournalState::Started;
        CHECK(journal.append(started).ok());
    }

    auto report = recovery.inspect(journal);
    CHECK(report.ok());
    bool retry_safe = false;
    for (const auto& finding : report.value().findings) {
        if (finding.record.id == "j_3") {
            CHECK_EQ(finding.action, RecoveryAction::RetrySafe);
            retry_safe = true;
        }
    }
    CHECK(retry_safe);

    // Now pretend the move completed before the journal was flushed.
    fs.add_file("/quarantine/x.txt", "content");
    fs.remove("/data/b/x.txt");
    auto after_crash = recovery.inspect(journal);
    CHECK(after_crash.ok());
    bool already_done = false;
    for (const auto& finding : after_crash.value().findings) {
        if (finding.record.id == "j_3") {
            CHECK_EQ(finding.action, RecoveryAction::AlreadyCompleted);
            already_done = true;
        }
    }
    CHECK(already_done);
    std::cout << "execution: durable journal and recovery inspection\n";
}

std::string digest_of(const std::string& text) {
    Blake3Hasher hasher;
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (char c : text) bytes.push_back(static_cast<std::byte>(c));
    auto digest = hasher.hash_data(bytes);
    CHECK(digest.ok());
    return digest.value().hex;
}

void test_duplicate_source_actions_rejected() {
    FakeFileSystem fs;
    fs.add_file("/data/a.txt", "content");
    Blake3Hasher hasher;

    ActionPlan plan;
    plan.mode = ActionMode::QuarantineDuplicates;
    plan.plan_id = "plan";
    const std::string digest = digest_of("content");
    PlannedAction first;
    first.id = "a0";
    first.kind = ActionKind::Move;
    first.source = "/data/a.txt";
    first.destination = "/q/a.txt";
    first.expected_digest = digest;
    first.expected_size = 7;
    PlannedAction second = first;
    second.id = "a1";
    second.destination = "/q/b.txt";
    plan.actions = {first, second};

    Validator validator(fs, hasher);
    auto validation = validator.validate_plan(plan);
    CHECK(validation.ok());
    CHECK(!validation.value().valid);
    bool reported = false;
    for (const auto& error : validation.value().errors) {
        if (error.find("same source") != std::string::npos) reported = true;
    }
    CHECK(reported);

    SimpleJournal journal;
    Executor executor(fs, hasher, journal);
    auto execution = executor.execute(plan, ApprovalSet{});
    CHECK(execution.ok());
    CHECK(!execution.value().success);
    CHECK(fs.exists("/data/a.txt").value()); // Nothing was moved.
    std::cout << "execution: duplicate-source actions rejected\n";
}

void test_journal_ids_are_unique_across_runs() {
    testutil::TempDir temp("journal-ids");
    const auto journal_path = temp.get() / "runs.jsonl";
    FakeClock clock("2026-11-01T10:00:00Z");

    std::string first_id;
    {
        FileJournal journal(journal_path, clock);
        JournalRecord record;
        record.id = journal.next_id();
        first_id = record.id;
        record.action_id = "a0";
        record.kind = "copy";
        record.source = "/one";
        record.destination = "/two";
        CHECK(journal.append(record).ok());
    }
    {
        // A second program run must not reuse ids, or recovery would fold two
        // unrelated operations into one record.
        FileJournal journal(journal_path, clock);
        JournalRecord record;
        record.id = journal.next_id();
        CHECK(record.id != first_id);
        record.action_id = "a1";
        record.kind = "copy";
        record.source = "/three";
        record.destination = "/four";
        CHECK(journal.append(record).ok());

        auto records = journal.entries();
        CHECK(records.ok());
        CHECK_EQ(latest_records(records.value()).size(), size_t(2));
    }
    std::cout << "execution: journal ids unique across runs\n";
}

void test_undo_refuses_changed_destination() {
    FakeFileSystem fs;
    fs.add_file("/data/a/x.txt", "same-content");
    fs.add_file("/data/b/x.txt", "same-content");

    Blake3Hasher hasher;
    SimpleJournal journal;
    auto setup = make_quarantine_plan(fs, "/data", "/quarantine");
    Executor executor(fs, hasher, journal);
    auto execution = executor.execute(setup.plan, setup.approvals);
    CHECK(execution.ok());
    CHECK(execution.value().success);

    // Someone edits the quarantined copy after the operation.
    fs.add_file("/quarantine/x.txt", "edited");
    FakeClock clock("2026-11-01T00:00:00Z");
    RecoveryEngine recovery(fs, hasher);
    auto undone = recovery.undo(journal, clock);
    CHECK(undone.ok());
    CHECK_EQ(undone.value().failed, size_t(1));
    CHECK(fs.exists("/quarantine/x.txt").value());
    std::cout << "execution: undo refuses changed destination\n";
}

} // namespace

int main() {
    test_quarantine_move_and_undo();
    test_destination_collision_never_overwrites();
    test_stale_source_blocks_execution();
    test_missing_approval_blocks_git_action();
    test_cross_device_copy_retains_source_and_undo();
    test_corrupt_copy_is_rejected();
    test_file_journal_durability_and_recovery();
    test_duplicate_source_actions_rejected();
    test_journal_ids_are_unique_across_runs();
    test_undo_refuses_changed_destination();
    std::cout << "All execution tests passed\n";
    return 0;
}
