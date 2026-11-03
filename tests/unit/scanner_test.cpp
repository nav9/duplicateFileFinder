#include "dupli/fake_filesystem.hpp"
#include "dupli/scanner.hpp"
#include "test_utils.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

using namespace dupli;

namespace {

void test_hidden_entries() {
    FakeFileSystem fs;
    fs.add_directory("/root");
    fs.add_file("/root/visible.txt", "a");
    fs.add_file("/root/.hidden.txt", "b");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/root"};
    auto included = scanner.scan(options);
    CHECK(included.ok());
    CHECK_EQ(included.value().file_count(), size_t(2));

    options.include_hidden = false;
    auto excluded = scanner.scan(options);
    CHECK(excluded.ok());
    CHECK_EQ(excluded.value().file_count(), size_t(1));
    bool found_excluded_hidden = false;
    for (const auto& entry : excluded.value().entries) {
        if (entry.is_hidden && entry.excluded) found_excluded_hidden = true;
    }
    CHECK(found_excluded_hidden);
    std::cout << "scanner: hidden entries\n";
}

void test_symlinks_are_not_followed() {
    FakeFileSystem fs;
    fs.add_directory("/root");
    fs.add_file("/root/real.txt", "hello");
    fs.add_directory("/root/sub");
    fs.add_file("/root/sub/nested.txt", "nested");
    fs.add_symlink("/root/link_to_sub");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/root"};
    auto result = scanner.scan(options);
    CHECK(result.ok());
    CHECK(result.value().has_skipped_links);
    bool saw_link = false;
    for (const auto& entry : result.value().entries) {
        if (entry.path == std::filesystem::path("/root/link_to_sub")) {
            saw_link = true;
            CHECK(entry.is_symlink);
            CHECK(!entry.error.empty());
        }
    }
    CHECK(saw_link);
    // The link target must not be scanned twice through the link.
    size_t nested_count = 0;
    for (const auto& entry : result.value().entries) {
        if (entry.path == std::filesystem::path("/root/sub/nested.txt")) ++nested_count;
    }
    CHECK_EQ(nested_count, size_t(1));

    options.follow_symlinks = true;
    auto rejected = scanner.scan(options);
    CHECK(rejected.has_error());
    CHECK_EQ(rejected.error().code, ErrorCode::InvalidArgument);
    std::cout << "scanner: symlinks skipped\n";
}

void test_overlapping_roots() {
    FakeFileSystem fs;
    fs.add_directory("/root");
    fs.add_file("/root/a.txt", "a");
    fs.add_directory("/root/sub");
    fs.add_file("/root/sub/b.txt", "b");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/root", "/root/sub"};
    auto result = scanner.scan(options);
    CHECK(result.ok());
    CHECK_EQ(result.value().roots.size(), size_t(1));
    CHECK_EQ(result.value().file_count(), size_t(2));
    // Deduplicating an overlapping root is not a scan failure.
    CHECK(result.value().complete);
    std::cout << "scanner: overlapping roots\n";
}

void test_permission_denied_directory() {
    FakeFileSystem fs;
    fs.add_directory("/root");
    fs.add_directory("/root/secret");
    fs.add_file("/root/secret/blocked.txt", "x");
    fs.set_permission_denied("/root/secret", true);

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/root"};
    auto result = scanner.scan(options);
    CHECK(result.ok());
    CHECK(!result.value().complete);
    bool permission_issue = false;
    for (const auto& issue : result.value().issues) {
        if (issue.code == ErrorCode::PermissionDenied) permission_issue = true;
    }
    CHECK(permission_issue);
    std::cout << "scanner: permission denied reported\n";
}

void test_filesystem_boundary() {
    FakeFileSystem fs;
    fs.add_directory("/root");
    fs.add_file("/root/a.txt", "a");
    fs.add_directory("/root/other_mount");
    fs.add_file("/root/other_mount/b.txt", "b");
    fs.set_same_device(false);

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/root"};
    auto result = scanner.scan(options);
    CHECK(result.ok());
    CHECK_EQ(result.value().file_count(), size_t(0));
    CHECK(!result.value().complete);

    options.cross_filesystem_boundaries = true;
    auto crossed = scanner.scan(options);
    CHECK(crossed.ok());
    CHECK_EQ(crossed.value().file_count(), size_t(2));
    std::cout << "scanner: filesystem boundary policy\n";
}

void test_exclusions_and_limits() {
    FakeFileSystem fs;
    fs.add_directory("/root");
    fs.add_file("/root/a.txt", "a");
    fs.add_directory("/root/skip");
    fs.add_file("/root/skip/b.txt", "b");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/root"};
    options.excludes = {"/root/skip"};
    auto result = scanner.scan(options);
    CHECK(result.ok());
    CHECK_EQ(result.value().file_count(), size_t(1));
    bool excluded = false;
    for (const auto& entry : result.value().entries) {
        if (entry.path == std::filesystem::path("/root/skip") && entry.excluded) excluded = true;
    }
    CHECK(excluded);

    options.excludes.clear();
    options.max_entries = 2;
    auto limited = scanner.scan(options);
    CHECK(limited.ok());
    CHECK(limited.value().truncated);
    std::cout << "scanner: exclusions and entry limit\n";
}

void test_missing_root_and_empty_directory() {
    FakeFileSystem fs;
    fs.add_directory("/empty");

    Scanner scanner(fs);
    ScanOptions options;
    options.roots = {"/empty"};
    auto empty = scanner.scan(options);
    CHECK(empty.ok());
    CHECK_EQ(empty.value().file_count(), size_t(0));
    CHECK_EQ(empty.value().directory_count(), size_t(1));
    CHECK(empty.value().complete);

    options.roots = {"/does-not-exist"};
    auto missing = scanner.scan(options);
    CHECK(missing.ok());
    CHECK(!missing.value().complete);
    CHECK(!missing.value().issues.empty());
    std::cout << "scanner: missing root and empty directory\n";
}

} // namespace

int main() {
    test_hidden_entries();
    test_symlinks_are_not_followed();
    test_overlapping_roots();
    test_permission_denied_directory();
    test_filesystem_boundary();
    test_exclusions_and_limits();
    test_missing_root_and_empty_directory();
    std::cout << "All scanner tests passed\n";
    return 0;
}
