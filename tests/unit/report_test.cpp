#include "dupli/fake_filesystem.hpp"
#include "dupli/grouping.hpp"
#include "dupli/json.hpp"
#include "dupli/report.hpp"
#include "dupli/scanner.hpp"
#include "test_utils.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace dupli;

namespace {

const ReportCategory* find_category(const Report& report, const std::string& id) {
    for (const auto& category : report.categories) {
        if (category.id == id) return &category;
    }
    return nullptr;
}

Report build_report(FakeFileSystem& fs, const std::string& root) {
    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {root};
    auto scanned = scanner.scan(options);
    CHECK(scanned.ok());

    Blake3Hasher hasher;
    DuplicateGrouper grouper(fs, hasher);
    GroupingResult grouping = grouper.group(scanned.value().files());

    FakeClock clock("2026-11-01T12:00:00Z");
    ReportBuilder builder(fs, hasher, clock, scanned.value(), grouping);
    return builder.build("scan");
}

void test_categories_and_summary() {
    FakeFileSystem fs;
    fs.add_file("/tree/one/x.txt", "duplicate-content");
    fs.add_file("/tree/two/x.txt", "duplicate-content");
    fs.add_file("/tree/one/unique.txt", "only-here");
    fs.add_file("/tree/one/.hidden.txt", "hidden");
    fs.add_directory("/tree/one/empty");
    fs.add_symlink("/tree/one/link");

    const Report report = build_report(fs, "/tree");
    CHECK_EQ(report.summary.duplicate_groups, size_t(1));
    CHECK_EQ(report.summary.duplicate_files, size_t(2));
    CHECK_EQ(report.summary.unique_files, size_t(2));
    CHECK_EQ(report.summary.skipped_links, size_t(1));
    CHECK(report.summary.empty_directories >= 1);
    CHECK(report.summary.reclaimable_bytes > 0);
    CHECK_EQ(report.generated_at, std::string("2026-11-01T12:00:00Z"));

    const auto* duplicates = find_category(report, "duplicate_files");
    CHECK(duplicates != nullptr);
    CHECK_EQ(duplicates->groups.size(), size_t(1));
    CHECK_EQ(duplicates->groups[0].items.size(), size_t(2));
    CHECK_EQ(duplicates->groups[0].digest.size(), size_t(64));
    CHECK(duplicates->groups[0].evidence.find("BLAKE3") != std::string::npos);

    const auto* unique = find_category(report, "unique_files");
    CHECK(unique != nullptr);
    CHECK_EQ(unique->groups.size(), size_t(1));
    CHECK_EQ(unique->groups[0].items.size(), size_t(2));

    const auto* links = find_category(report, "skipped_links");
    CHECK(links != nullptr);
    CHECK_EQ(links->groups.size(), size_t(1));

    const auto* empty = find_category(report, "empty_directories");
    CHECK(empty != nullptr);
    CHECK_EQ(empty->groups.size(), size_t(1));
    std::cout << "report: categories and summary\n";
}

void test_content_set_relationship() {
    FakeFileSystem fs;
    fs.add_file("/left/one.txt", "content-a");
    fs.add_file("/left/two.txt", "content-b");
    fs.add_file("/right/nested/one.txt", "content-a");
    fs.add_file("/right/other.txt", "content-b");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/left", "/right"};
    auto scanned = scanner.scan(options);
    CHECK(scanned.ok());

    Blake3Hasher hasher;
    DuplicateGrouper grouper(fs, hasher);
    GroupingResult grouping = grouper.group(scanned.value().files());

    FakeClock clock("2026-11-01T12:00:00Z");
    ReportBuilder builder(fs, hasher, clock, scanned.value(), grouping);
    const Report report = builder.build("scan");

    const auto* content_sets = find_category(report, "content_sets");
    CHECK(content_sets != nullptr);
    CHECK_EQ(content_sets->groups.size(), size_t(1));
    CHECK(content_sets->groups[0].evidence.find("Same content multiset") != std::string::npos);

    // Layout differs, so this is not an exact tree duplicate.
    const auto* trees = find_category(report, "tree_duplicates");
    CHECK(trees != nullptr);
    CHECK_EQ(trees->groups.size(), size_t(0));
    std::cout << "report: content-set relationship\n";
}

void test_relative_path_conflict() {
    FakeFileSystem fs;
    fs.add_file("/a/same.txt", "content-one");
    fs.add_file("/b/same.txt", "content-two");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/a", "/b"};
    auto scanned = scanner.scan(options);
    CHECK(scanned.ok());

    Blake3Hasher hasher;
    DuplicateGrouper grouper(fs, hasher);
    GroupingResult grouping = grouper.group(scanned.value().files());

    FakeClock clock("2026-11-01T12:00:00Z");
    ReportBuilder builder(fs, hasher, clock, scanned.value(), grouping);
    const Report report = builder.build("scan");

    const auto* conflicts = find_category(report, "relative_path_conflicts");
    CHECK(conflicts != nullptr);
    CHECK_EQ(conflicts->groups.size(), size_t(1));
    CHECK(conflicts->groups[0].requires_review);
    std::cout << "report: relative-path conflict\n";
}

void test_json_serialization_and_save() {
    FakeFileSystem fs;
    fs.add_file("/tree/a.txt", "dup");
    fs.add_file("/tree/b.txt", "dup");
    const Report report = build_report(fs, "/tree");

    const std::string text = report.to_json(true);
    auto parsed = json::parse(text);
    CHECK(parsed.has_value());
    CHECK_EQ((*parsed)["format"].as_string(), std::string("dupli-report"));
    CHECK_EQ((*parsed)["version"].as_int(), int64_t(1));
    CHECK_EQ((*parsed)["summary"]["duplicate_groups"].as_int(), int64_t(1));
    CHECK(!(*parsed)["categories"].as_array().empty());

    auto saved = report.save(fs, "/out/report.json");
    CHECK(saved.ok());
    auto opened = fs.open_read("/out/report.json");
    CHECK(opened.ok());
    auto content = opened.value()->read_all();
    CHECK(content.ok());
    const std::string saved_text(reinterpret_cast<const char*>(content.value().data()),
                                 content.value().size());
    auto reparsed = json::parse(saved_text);
    CHECK(reparsed.has_value());
    CHECK_EQ((*reparsed)["summary"]["duplicate_files"].as_int(), int64_t(2));

    // Compact output must also be valid.
    CHECK(json::parse(report.to_json(false)).has_value());
    std::cout << "report: JSON serialization and save\n";
}

void test_error_entries_are_not_unique() {
    FakeFileSystem fs;
    Blake3Hasher hasher;
    DuplicateGrouper grouper(fs, hasher);

    ScannedEntry broken;
    broken.is_file = true;
    broken.id = "0:broken.txt";
    broken.path = "/tree/broken.txt";
    broken.relative_path = "broken.txt";
    broken.error = "size could not be read";

    GroupingResult grouping = grouper.group({broken});
    CHECK(grouping.unique_files.empty());
    CHECK_EQ(grouping.unreadable_files.size(), size_t(1));
    CHECK(!grouping.complete());
    std::cout << "report: errored entries are never unique\n";
}

void test_container_category() {
    FakeFileSystem fs;
    fs.add_file("/repo/.git/config", "x");
    fs.add_file("/repo/a.txt", "a");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/repo"};
    auto scanned = scanner.scan(options);
    CHECK(scanned.ok());

    Blake3Hasher hasher;
    DuplicateGrouper grouper(fs, hasher);
    GroupingResult grouping = grouper.group(scanned.value().files());

    FakeClock clock("2026-11-01T12:00:00Z");
    ReportBuilder builder(fs, hasher, clock, scanned.value(), grouping);
    DetectedContainer container;
    container.path = "/repo";
    container.type = "git";
    container.warnings = {"Git repository detected"};
    builder.set_containers({container});
    const Report report = builder.build("scan");

    const auto* containers = find_category(report, "containers");
    CHECK(containers != nullptr);
    CHECK_EQ(containers->groups.size(), size_t(1));
    CHECK(containers->groups[0].requires_review);
    CHECK_EQ(report.summary.containers, size_t(1));
    std::cout << "report: container category\n";
}

} // namespace

int main() {
    test_categories_and_summary();
    test_content_set_relationship();
    test_relative_path_conflict();
    test_json_serialization_and_save();
    test_error_entries_are_not_unique();
    test_container_category();
    std::cout << "All report tests passed\n";
    return 0;
}
