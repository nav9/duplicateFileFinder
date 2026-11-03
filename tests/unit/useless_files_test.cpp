#include "dupli/executor.hpp"
#include "dupli/fake_filesystem.hpp"
#include "dupli/useless_files.hpp"
#include "test_utils.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace dupli;

namespace {

const UselessPattern* preset_for(const std::string& pattern) {
    for (const auto& preset : builtin_useless_patterns()) {
        if (preset.pattern == pattern) return &preset;
    }
    return nullptr;
}

void test_presets() {
    const auto& presets = builtin_useless_patterns();
    CHECK(!presets.empty());

    const UselessPattern* thumbs = preset_for("Thumbs.db");
    CHECK(thumbs != nullptr);
    CHECK(thumbs->default_enabled);
    CHECK_EQ(thumbs->group, UselessPatternGroup::Windows);

    // Folder configuration and resource forks are offered but never enabled by
    // default, and always explain the risk.
    const UselessPattern* desktop = preset_for("Desktop.ini");
    CHECK(desktop != nullptr);
    CHECK(!desktop->default_enabled);
    CHECK(!desktop->warning.empty());

    const UselessPattern* apple_double = preset_for("._*");
    CHECK(apple_double != nullptr);
    CHECK(!apple_double->default_enabled);
    CHECK(!apple_double->warning.empty());

    const UselessPattern* ds_store = preset_for(".DS_Store");
    CHECK(ds_store != nullptr);
    CHECK(ds_store->default_enabled);
    CHECK_EQ(ds_store->group, UselessPatternGroup::MacOs);

    // The presets cover all three OS labels plus the cross-platform group.
    bool windows = false, macos = false, linux_group = false, cross = false;
    for (const auto& preset : presets) {
        if (preset.group == UselessPatternGroup::Windows) windows = true;
        if (preset.group == UselessPatternGroup::MacOs) macos = true;
        if (preset.group == UselessPatternGroup::Linux) linux_group = true;
        if (preset.group == UselessPatternGroup::CrossPlatform) cross = true;
    }
    CHECK(windows && macos && linux_group && cross);
    std::cout << "useless: presets\n";
}

void test_glob_matching() {
    CHECK(glob_matches_filename("Thumbs.db", "Thumbs.db"));
    CHECK(glob_matches_filename("*.db", "Thumbs.db"));
    CHECK(glob_matches_filename("Thumbs*", "Thumbs.db"));
    CHECK(glob_matches_filename("*_cache", "image_cache"));
    CHECK(glob_matches_filename("a?c", "abc"));
    CHECK(!glob_matches_filename("a?c", "abbc"));
    CHECK(glob_matches_filename("*", "anything"));

    // Case-insensitive by default, because the same presets must work on
    // Windows/macOS-style filesystems.
    CHECK(glob_matches_filename("thumbs.db", "Thumbs.db"));
    CHECK(!glob_matches_filename("thumbs.db", "Thumbs.db", true));
    CHECK(glob_matches_filename("THUMBS.DB", "Thumbs.db", true) == false);

    // Patterns only ever match a name component.
    std::string problem;
    CHECK(!is_valid_useless_glob("dir/Thumbs.db", &problem));
    CHECK(!problem.empty());
    CHECK(!is_valid_useless_glob("", &problem));
    CHECK(!is_valid_useless_glob("   ", &problem));
    CHECK(!is_valid_useless_glob("a\\b", &problem));
    CHECK(is_valid_useless_glob("*.tmp", &problem));

    auto parsed = parse_useless_pattern_list("Thumbs.db, .DS_Store, , bad/name, *.part");
    CHECK_EQ(parsed.patterns.size(), size_t(3));
    CHECK_EQ(parsed.problems.size(), size_t(1));
    std::cout << "useless: glob matching\n";
}

void test_scanning() {
    FakeFileSystem fs;
    fs.add_file("/root/Thumbs.db", "cache");
    fs.add_file("/root/photos/.DS_Store", "finder");
    fs.add_file("/root/photos/note.txt", "user data");
    fs.add_file("/root/photos/Thumbs.db", "cache2");
    fs.add_symlink("/root/link.db");

    UselessFileScanner scanner(fs);
    ScanOptions options;
    options.roots = {"/root"};

    auto result = scanner.scan(options, {"Thumbs.db", "*.db"});
    CHECK(result.ok());
    CHECK_EQ(result.value().matches.size(), size_t(2));
    // Each file appears once and reports every pattern that matched it.
    for (const auto& match : result.value().matches) {
        CHECK_EQ(match.matched_patterns.size(), size_t(2));
        CHECK(match.reason.find("Thumbs.db") != std::string::npos);
        CHECK(match.entry.path.filename() == "Thumbs.db");
    }
    CHECK_EQ(result.value().total_bytes, size_t(11));
    CHECK(result.value().complete);

    auto finder = scanner.scan(options, {".DS_Store"});
    CHECK(finder.ok());
    CHECK_EQ(finder.value().matches.size(), size_t(1));

    // Desktop.ini is only found when the user explicitly asks for it.
    auto desktop = scanner.scan(options, {"Desktop.ini"});
    CHECK(desktop.ok());
    CHECK_EQ(desktop.value().matches.size(), size_t(0));

    auto missing = scanner.scan(options, {});
    CHECK(missing.has_error());
    CHECK_EQ(missing.error().code, ErrorCode::InvalidArgument);

    auto malformed = scanner.scan(options, {"dir/*.db"});
    CHECK(malformed.has_error());
    std::cout << "useless: scanning\n";
}

void test_plan_and_execute() {
    FakeFileSystem fs;
    fs.add_file("/root/project/Thumbs.db", "thumb-bytes");
    fs.add_file("/root/Thumbs.db", "root-bytes");
    fs.add_file("/root/notes.txt", "keep me");

    Blake3Hasher hasher;
    UselessFilePlanner planner(fs, hasher);

    UselessMoveRequest request;
    request.sources = {"/root/project/Thumbs.db", "/root/Thumbs.db"};
    request.destination = "/quarantine";
    request.roots = {"/root"};
    request.scan_roots = {"/root"};

    auto planned = planner.plan_moves(request);
    CHECK(planned.ok());
    CHECK(planned.value().executable);
    CHECK_EQ(planned.value().actions.size(), size_t(2));
    CHECK_EQ(planned.value().actions[0].destination,
             std::filesystem::path("/quarantine/Thumbs.db"));
    CHECK_EQ(planned.value().actions[1].destination,
             std::filesystem::path("/quarantine/project/Thumbs.db"));
    for (const auto& action : planned.value().actions) {
        CHECK_EQ(action.kind, ActionKind::Move);
        CHECK_EQ(action.expected_digest.size(), size_t(64));
    }

    SimpleJournal journal;
    Executor executor(fs, hasher, journal);
    ApprovalSet approvals;
    approvals.plan_id = planned.value().plan_id;
    auto executed = executor.execute(planned.value(), approvals);
    CHECK(executed.ok());
    CHECK(executed.value().success);

    auto moved = fs.exists("/quarantine/project/Thumbs.db");
    CHECK(moved.ok() && moved.value());
    auto gone = fs.exists("/root/project/Thumbs.db");
    CHECK(gone.ok() && !gone.value());
    auto kept = fs.exists("/root/notes.txt");
    CHECK(kept.ok() && kept.value());

    auto entries = journal.entries();
    CHECK(entries.ok());
    CHECK_EQ(entries.value().size(), size_t(4)); // 2 started + 2 completed
    size_t started = 0;
    size_t completed = 0;
    for (const auto& record : entries.value()) {
        CHECK_EQ(record.kind, std::string("move"));
        if (record.state == JournalState::Started) ++started;
        if (record.state == JournalState::Completed) ++completed;
    }
    CHECK_EQ(started, size_t(2));
    CHECK_EQ(completed, size_t(2));
    std::cout << "useless: plan and execute\n";
}

void test_collisions_and_warnings() {
    FakeFileSystem fs;
    fs.add_file("/root/a/Thumbs.db", "one");
    fs.add_file("/root/b/Thumbs.db", "two");
    fs.add_file("/quarantine/a/Thumbs.db", "already there");

    Blake3Hasher hasher;
    UselessFilePlanner planner(fs, hasher);
    UselessMoveRequest request;
    request.sources = {"/root/a/Thumbs.db", "/root/b/Thumbs.db"};
    request.destination = "/quarantine";
    request.roots = {"/root"};
    request.scan_roots = {"/root"};

    auto planned = planner.plan_moves(request);
    CHECK(planned.ok());
    std::vector<std::string> destinations;
    for (const auto& action : planned.value().actions) {
        destinations.push_back(action.destination.generic_string());
    }
    std::sort(destinations.begin(), destinations.end());
    // Never overwrite: the existing file keeps its name and the new one gets a
    // deterministic suffix.
    CHECK_EQ(destinations[0], std::string("/quarantine/a/Thumbs_1.db"));
    CHECK_EQ(destinations[1], std::string("/quarantine/b/Thumbs.db"));

    fs.add_file("/quarantine/b/Thumbs.db", "existing");
    auto replanned = planner.plan_moves(request);
    CHECK(replanned.ok());
    bool suffixed = false;
    for (const auto& action : replanned.value().actions) {
        if (action.destination.generic_string() == "/quarantine/b/Thumbs_1.db") suffixed = true;
    }
    CHECK(suffixed);

    // Destination inside a scan root must produce a warning, not silent data
    // churn.
    UselessMoveRequest inside;
    inside.sources = {"/root/a/Thumbs.db"};
    inside.destination = "/root/quarantine";
    inside.roots = {"/root"};
    inside.scan_roots = {"/root"};
    auto warned = planner.plan_moves(inside);
    CHECK(warned.ok());
    CHECK(!warned.value().warnings.empty());
    std::cout << "useless: collisions and warnings\n";
}

void test_unverifiable_sources_are_skipped() {
    FakeFileSystem fs;
    fs.add_file("/root/Thumbs.db", "cache");
    fs.add_file("/root/other.db", "cache");
    fs.set_read_failure("/root/Thumbs.db", true);

    Blake3Hasher hasher;
    UselessFilePlanner planner(fs, hasher);
    UselessMoveRequest request;
    request.sources = {"/root/Thumbs.db"};
    request.destination = "/quarantine";
    request.roots = {"/root"};

    auto planned = planner.plan_moves(request);
    CHECK(planned.has_error());

    request.sources = {"/root/Thumbs.db", "/root/other.db"};
    auto partial = planner.plan_moves(request);
    CHECK(partial.ok());
    CHECK_EQ(partial.value().actions.size(), size_t(1));
    CHECK_EQ(partial.value().actions[0].source, std::filesystem::path("/root/other.db"));
    CHECK(!partial.value().warnings.empty());

    // A missing destination is refused before anything is planned.
    request.destination.clear();
    auto no_destination = planner.plan_moves(request);
    CHECK(no_destination.has_error());
    std::cout << "useless: unverifiable sources\n";
}

} // namespace

int main() {
    test_presets();
    test_glob_matching();
    test_scanning();
    test_plan_and_execute();
    test_collisions_and_warnings();
    test_unverifiable_sources_are_skipped();
    std::cout << "All useless-file tests passed\n";
    return 0;
}
