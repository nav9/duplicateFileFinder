// Integration tests use the real filesystem in a unique temporary directory that
// is removed by RAII. They exercise the same service API the CLI and a future GUI
// use, so the end-to-end behavior (not just unit logic) is proven.

#include "dupli/journal.hpp"
#include "dupli/real_filesystem.hpp"
#include "dupli/service.hpp"
#include "test_utils.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

using namespace dupli;

namespace {

ScanOptions options_for(const std::vector<std::filesystem::path>& roots, bool include_hidden = true) {
    ScanOptions options;
    options.roots = roots;
    options.include_hidden = include_hidden;
    return options;
}

ApprovalSet approvals_for(const ActionPlan& plan) {
    ApprovalSet approvals;
    approvals.plan_id = plan.plan_id;
    for (const auto& action : plan.actions) {
        if (!action.requires_approval) continue;
        approvals.approvals.push_back(
            Approval{action.id, path_to_utf8(action.source), action.expected_digest});
    }
    return approvals;
}

void test_quarantine_end_to_end() {
    testutil::TempDir temp("integration");
    const auto root = temp.get() / "project";
    testutil::write_file(root / "keep.txt", "same-content");
    testutil::write_file(root / "copy" / "keep.txt", "same-content");
    testutil::write_file(root / "docs" / "unique.txt", "unique-content");
    testutil::write_file(root / ".hidden.txt", "hidden-content");
    std::filesystem::create_directories(root / "empty");

    const auto destination = temp.get() / "quarantine";
    const auto journal_path = temp.get() / "journal.jsonl";

    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(journal_path, clock);
    DuplicateManagerService service(fs, hasher, journal, clock);

    auto scan = service.scan(options_for({root}));
    CHECK(scan.ok());
    CHECK(scan.value().complete);
    CHECK_EQ(scan.value().file_count(), size_t(4));

    auto classification = service.classify(scan.value());
    CHECK(classification.has_error() == false);
    CHECK_EQ(classification.value().grouping.groups.size(), size_t(1));
    CHECK_EQ(classification.value().grouping.unique_files.size(), size_t(2));

    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = destination;
    request.duplicate_groups = classification.value().grouping.groups;
    request.unique_files = classification.value().grouping.unique_files;
    request.digest_by_id = classification.value().grouping.digest_by_id;

    auto plan = service.create_plan(request);
    CHECK(plan.ok());
    CHECK(plan.value().executable);
    CHECK_EQ(plan.value().actions.size(), size_t(1));

    const ApprovalSet approvals = approvals_for(plan.value());
    auto validation = service.validate(plan.value(), approvals);
    CHECK(validation.ok());
    CHECK(validation.value().valid);

    auto execution = service.execute(plan.value(), approvals);
    CHECK(execution.ok());
    CHECK(execution.value().success);
    CHECK(std::filesystem::exists(destination / "keep.txt"));
    CHECK(!std::filesystem::exists(root / "copy" / "keep.txt"));
    CHECK(std::filesystem::exists(root / "keep.txt"));
    CHECK(std::filesystem::exists(root / "docs" / "unique.txt"));

    auto records = journal.entries();
    CHECK(records.ok());
    CHECK(!records.value().empty());

    // Undo restores the quarantined duplicate exactly where it was.
    RecoveryEngine recovery(fs, hasher);
    auto undone = recovery.undo(journal, clock);
    CHECK(undone.ok());
    CHECK_EQ(undone.value().undone, size_t(1));
    CHECK(std::filesystem::exists(root / "copy" / "keep.txt"));
    CHECK_EQ(testutil::read_file(root / "copy" / "keep.txt"), std::string("same-content"));
    std::cout << "integration: quarantine and undo\n";
}

void test_unique_collection_end_to_end() {
    testutil::TempDir temp("integration-unique");
    const auto root = temp.get() / "project";
    testutil::write_file(root / "a" / "one.txt", "one-content");
    testutil::write_file(root / "b" / "two.txt", "two-content");
    const auto destination = temp.get() / "collection";

    // Metadata that must survive a copy: permissions and modification time.
    std::error_code meta_error;
    std::filesystem::permissions(
        root / "a" / "one.txt",
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::group_read,
        std::filesystem::perm_options::replace, meta_error);
    CHECK(!meta_error);
    const auto source_time = std::filesystem::file_time_type::clock::now() -
                             std::chrono::hours(48);
    std::filesystem::last_write_time(root / "a" / "one.txt", source_time, meta_error);
    CHECK(!meta_error);

    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(temp.get() / "journal.jsonl", clock);
    DuplicateManagerService service(fs, hasher, journal, clock);

    auto scan = service.scan(options_for({root}));
    CHECK(scan.ok());
    auto classification = service.classify(scan.value());
    CHECK(classification.has_error() == false);
    CHECK_EQ(classification.value().grouping.unique_files.size(), size_t(2));

    PlanRequest request;
    request.mode = ActionMode::CreateUniqueCollection;
    request.destination = destination;
    request.unique_files = classification.value().grouping.unique_files;
    request.digest_by_id = classification.value().grouping.digest_by_id;
    auto plan = service.create_plan(request);
    CHECK(plan.ok());
    CHECK_EQ(plan.value().actions.size(), size_t(2));

    auto execution = service.execute(plan.value(), approvals_for(plan.value()));
    CHECK(execution.ok());
    CHECK(execution.value().success);
    CHECK(std::filesystem::exists(destination / "a" / "one.txt"));
    CHECK(std::filesystem::exists(destination / "b" / "two.txt"));
    CHECK_EQ(testutil::read_file(destination / "a" / "one.txt"), std::string("one-content"));
    CHECK(std::filesystem::status(destination / "a" / "one.txt").permissions() ==
          std::filesystem::status(root / "a" / "one.txt").permissions());
    CHECK(std::filesystem::last_write_time(destination / "a" / "one.txt") == source_time);
    // The originals are untouched: a unique collection is a copy, never a move.
    CHECK(std::filesystem::exists(root / "a" / "one.txt"));
    CHECK(std::filesystem::exists(root / "b" / "two.txt"));

    // Running it again must not overwrite the existing collection.
    auto second = service.execute(plan.value(), approvals_for(plan.value()));
    CHECK(second.ok());
    CHECK(!second.value().success);
    CHECK_EQ(testutil::read_file(destination / "a" / "one.txt"), std::string("one-content"));
    std::cout << "integration: unique collection copy and no-overwrite\n";
}

void test_hidden_option_and_symlink_skip() {
    testutil::TempDir temp("integration-links");
    const auto root = temp.get() / "project";
    testutil::write_file(root / "visible.txt", "visible");
    testutil::write_file(root / ".hidden.txt", "hidden");
    testutil::write_file(root / "target" / "nested.txt", "nested");

    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(temp.get() / "journal.jsonl", clock);
    DuplicateManagerService service(fs, hasher, journal, clock);

    auto without_hidden = service.scan(options_for({root}, false));
    CHECK(without_hidden.ok());
    size_t hidden_count = 0;
    for (const auto& entry : without_hidden.value().entries) {
        if (entry.path.filename() == ".hidden.txt") ++hidden_count;
    }
    CHECK_EQ(hidden_count, size_t(1)); // Still reported, but excluded.

#ifndef _WIN32
    std::error_code link_error;
    std::filesystem::create_directory_symlink(root / "target", root / "link", link_error);
    if (!link_error) {
        auto scanned = service.scan(options_for({root}));
        CHECK(scanned.ok());
        CHECK(scanned.value().has_skipped_links);
        size_t nested = 0;
        for (const auto& entry : scanned.value().entries) {
            if (entry.path.filename() == "nested.txt") ++nested;
        }
        CHECK_EQ(nested, size_t(1)); // Not scanned again through the link.
    }
#endif
    std::cout << "integration: hidden option and symlink skipping\n";
}

void test_real_cross_device_quarantine_retains_source() {
#ifndef _WIN32
    // /dev/shm is usually a different filesystem from the temp dir. If it is not,
    // this scenario cannot be exercised and is reported rather than faked.
    const std::filesystem::path shared_memory = "/dev/shm";
    std::error_code error;
    if (!std::filesystem::is_directory(shared_memory, error)) {
        std::cout << "integration: cross-device test skipped (no /dev/shm)\n";
        return;
    }

    testutil::TempDir temp("integration-xdev");
    RealFileSystem fs;
    auto same = fs.same_device(temp.get(), shared_memory);
    if (!same.ok() || same.value()) {
        std::cout << "integration: cross-device test skipped (same filesystem)\n";
        return;
    }

    const auto root = temp.get() / "project";
    testutil::write_file(root / "a" / "f.txt", "cross-device-content");
    testutil::write_file(root / "b" / "f.txt", "cross-device-content");
    const auto destination = shared_memory / ("dupli-xdev-" + testutil::unique_suffix());

    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{destination};

    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(temp.get() / "journal.jsonl", clock);
    DuplicateManagerService service(fs, hasher, journal, clock);

    auto scan = service.scan(options_for({root}));
    CHECK(scan.ok());
    auto classification = service.classify(scan.value());
    CHECK(classification.has_error() == false);
    CHECK_EQ(classification.value().grouping.groups.size(), size_t(1));

    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = destination;
    request.duplicate_groups = classification.value().grouping.groups;
    request.digest_by_id = classification.value().grouping.digest_by_id;
    auto plan = service.create_plan(request);
    CHECK(plan.ok());

    auto execution = service.execute(plan.value(), approvals_for(plan.value()));
    CHECK(execution.ok());
    CHECK(execution.value().success);
    // Cross-filesystem work copies; the source must be retained by policy.
    CHECK_EQ(execution.value().sources_retained.size(), size_t(1));
    CHECK(std::filesystem::exists(root / "a" / "f.txt"));
    CHECK(std::filesystem::exists(root / "b" / "f.txt"));
    CHECK(std::filesystem::exists(destination / "f.txt"));

    auto records = journal.entries();
    CHECK(records.ok());
    bool saw_copy = false;
    for (const auto& record : latest_records(records.value())) {
        if (record.kind == "copy" && record.state == JournalState::Completed) saw_copy = true;
    }
    CHECK(saw_copy);
    std::cout << "integration: real cross-device copy retains source\n";
#else
    std::cout << "integration: cross-device test skipped on Windows\n";
#endif
}

void test_plan_file_roundtrip_on_disk() {
    testutil::TempDir temp("integration-plan");
    const auto root = temp.get() / "project";
    testutil::write_file(root / "a.txt", "duplicate");
    testutil::write_file(root / "sub" / "a.txt", "duplicate");

    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(temp.get() / "journal.jsonl", clock);
    DuplicateManagerService service(fs, hasher, journal, clock);

    auto scan = service.scan(options_for({root}));
    CHECK(scan.ok());
    auto classification = service.classify(scan.value());
    CHECK(classification.has_error() == false);

    PlanRequest request;
    request.mode = ActionMode::QuarantineDuplicates;
    request.destination = temp.get() / "quarantine";
    request.duplicate_groups = classification.value().grouping.groups;
    request.digest_by_id = classification.value().grouping.digest_by_id;
    auto plan = service.create_plan(request);
    CHECK(plan.ok());

    const auto plan_path = temp.get() / "plan.json";
    {
        std::filesystem::path parent = plan_path.parent_path();
        (void)parent;
        testutil::write_file(plan_path, ActionPlanner::serialize_plan(plan.value()));
    }
    auto parsed = ActionPlanner::parse_plan(testutil::read_file(plan_path));
    CHECK(parsed.ok());
    CHECK_EQ(parsed.value().plan_id, plan.value().plan_id);

    auto execution = service.execute(parsed.value(), approvals_for(parsed.value()));
    CHECK(execution.ok());
    CHECK(execution.value().success);
    CHECK(std::filesystem::exists(temp.get() / "quarantine" / "a.txt"));
    std::cout << "integration: saved plan roundtrip and execution\n";
}

} // namespace

int main() {
    test_quarantine_end_to_end();
    test_unique_collection_end_to_end();
    test_hidden_option_and_symlink_skip();
    test_real_cross_device_quarantine_retains_source();
    test_plan_file_roundtrip_on_disk();
    std::cout << "All integration tests passed\n";
    return 0;
}
