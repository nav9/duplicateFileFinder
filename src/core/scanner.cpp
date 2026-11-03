#include "dupli/scanner.hpp"

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <utility>

namespace dupli {

namespace {

std::filesystem::path normalized(const std::filesystem::path& path) {
    std::filesystem::path result = path.lexically_normal();
    if (result.empty()) result = ".";
    // Strip a trailing separator so "/a/b/" and "/a/b" compare equal.
    if (result != result.root_path() && result.filename().empty()) {
        result = result.parent_path();
    }
    return result;
}

/// True when `candidate` is `prefix` itself or lives beneath it. Comparison is
/// component-wise, so "/data/abc" is not treated as being inside "/data/ab".
bool is_within(const std::filesystem::path& prefix, const std::filesystem::path& candidate) {
    const auto normalized_prefix = normalized(prefix);
    const auto normalized_candidate = normalized(candidate);
    auto prefix_it = normalized_prefix.begin();
    auto candidate_it = normalized_candidate.begin();
    for (; prefix_it != normalized_prefix.end(); ++prefix_it, ++candidate_it) {
        if (candidate_it == normalized_candidate.end()) return false;
        // Compare the native component representation: on Windows this is
        // case-insensitive through path comparison, on POSIX it is exact. We do
        // not assume one rule globally.
        if (*prefix_it != *candidate_it) return false;
    }
    return true;
}

bool excluded_by_options(const ScanOptions& options, const std::filesystem::path& path) {
    for (const auto& exclude : options.excludes) {
        if (is_within(exclude, path)) return true;
    }
    return false;
}

} // namespace

std::vector<ScannedEntry> ScanResult::files() const {
    std::vector<ScannedEntry> result;
    for (const auto& entry : entries) {
        if (entry.is_file && entry.error.empty() && !entry.excluded) result.push_back(entry);
    }
    return result;
}

std::vector<ScannedEntry> ScanResult::directories() const {
    std::vector<ScannedEntry> result;
    for (const auto& entry : entries) {
        if (entry.is_dir && !entry.excluded) result.push_back(entry);
    }
    return result;
}

size_t ScanResult::file_count() const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(),
                                             [](const ScannedEntry& e) { return e.is_file; }));
}

size_t ScanResult::directory_count() const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(),
                                             [](const ScannedEntry& e) { return e.is_dir; }));
}

bool entry_sort_key_less(const ScannedEntry& left, const ScannedEntry& right) {
    auto depth = [](const std::filesystem::path& path) {
        return static_cast<std::ptrdiff_t>(
            std::distance(path.begin(), path.end()));
    };
    const auto left_depth = depth(left.relative_path);
    const auto right_depth = depth(right.relative_path);
    if (left_depth != right_depth) return left_depth < right_depth;
    if (left.root_index != right.root_index) return left.root_index < right.root_index;
    const auto left_rel = left.relative_path.generic_string();
    const auto right_rel = right.relative_path.generic_string();
    if (left_rel != right_rel) return left_rel < right_rel;
    return left.path.generic_string() < right.path.generic_string();
}

Result<ScanResult> Scanner::scan(const ScanOptions& options) const {
    if (options.roots.empty()) {
        return Error{ErrorCode::InvalidArgument, "No scan roots were provided", "", "scan"};
    }
    if (options.follow_symlinks) {
        // The product requirement is explicit: links are never traversed, because
        // doing so can loop forever or escape the selected tree.
        return Error{ErrorCode::InvalidArgument,
                     "Following symbolic links is not supported; links are reported and skipped",
                     "", "scan"};
    }

    ScanResult result;
    std::vector<std::filesystem::path> accepted_roots;
    std::set<std::string> visited;

    struct Pending {
        std::filesystem::path path;
        std::filesystem::path relative;
        size_t root_index;
        std::filesystem::path root_path;
    };
    std::vector<Pending> stack;

    for (const auto& raw_root : options.roots) {
        const auto root = normalized(raw_root);

        // Overlapping roots must not double-count entries. Roots are processed
        // in the given order, so an earlier root wins.
        bool covered = false;
        for (const auto& accepted : accepted_roots) {
            if (is_within(accepted, root)) {
                covered = true;
                break;
            }
        }
        if (covered) {
            // Selecting a root twice (or a nested root) is harmless: its entries
            // are already covered by an earlier root. This is deduplication, not a
            // scan failure, so it must not mark the scan incomplete.
            continue;
        }
        accepted_roots.push_back(root);
        const size_t root_index = result.roots.size();
        result.roots.push_back(root);

        if (excluded_by_options(options, root)) {
            ScannedEntry entry;
            entry.id = std::to_string(root_index) + ":.";
            entry.path = root;
            entry.relative_path = ".";
            entry.root_index = root_index;
            entry.excluded = true;
            entry.exclusion_reason = "matches an exclusion rule";
            result.entries.push_back(entry);
            continue;
        }

        // A root may also be a single file.
        auto is_dir = m_fs.is_directory(root);
        auto is_file = m_fs.is_regular_file(root);
        if ((is_dir.has_error() || !is_dir.value()) && (is_file.has_error() || !is_file.value())) {
            result.issues.push_back(ScanIssue{root, ErrorCode::NotFound, "scan",
                                              "Selected root does not exist or is not accessible"});
            result.complete = false;
            continue;
        }

        stack.push_back(Pending{root, ".", root_index, root});
    }

    while (!stack.empty()) {
        Pending current = std::move(stack.back());
        stack.pop_back();

        if (options.max_entries != 0 && result.entries.size() >= options.max_entries) {
            result.truncated = true;
            result.complete = false;
            break;
        }

        ScannedEntry entry;
        entry.path = current.path;
        entry.relative_path = current.relative;
        entry.root_index = current.root_index;
        // Stable within the scan and independent of the absolute path prefix.
        entry.id = std::to_string(current.root_index) + ":" +
                   current.relative.generic_string();

        // Deduplicate on the normalized path: symlinked aliases and overlapping
        // roots can otherwise list the same object twice.
        const std::string key = path_to_utf8(normalized(current.path));
        if (!visited.insert(key).second) continue;

        // Exclusions are recorded, never silently dropped, so reports can show
        // what was deliberately left out.
        if (excluded_by_options(options, current.path)) {
            entry.excluded = true;
            entry.exclusion_reason = "matches an exclusion rule";
            result.entries.push_back(entry);
            continue;
        }

        if (!current.relative.empty() && current.relative != ".") {
            // Cross-filesystem traversal is opt-in. A denial is reported as a
            // skipped entry, not as an empty directory.
            auto same_device = m_fs.same_device(current.path, current.root_path);
            if (same_device.ok() && !same_device.value() && !options.cross_filesystem_boundaries) {
                entry.excluded = true;
                entry.exclusion_reason = "outside the selected filesystem boundary";
                result.entries.push_back(entry);
                result.issues.push_back(ScanIssue{current.path, ErrorCode::Unsupported, "boundary",
                                                  "Entry is on a different filesystem; not crossed"});
                continue;
            }
        }

        auto link_status = m_fs.is_symlink(current.path);
        if (link_status.has_error()) {
            entry.error = link_status.error().message;
            result.issues.push_back(ScanIssue{current.path, link_status.error().code, "is_symlink",
                                              link_status.error().message});
            result.entries.push_back(entry);
            result.complete = false;
            continue;
        }
        if (link_status.value()) {
            entry.is_symlink = true;
            entry.error = "symbolic link: reported but not followed";
            result.has_skipped_links = true;
            result.entries.push_back(entry);
            continue;
        }

        auto hidden = m_fs.is_hidden(current.path);
        entry.is_hidden = hidden.ok() && hidden.value();
        if (entry.is_hidden && !options.include_hidden) {
            entry.excluded = true;
            entry.exclusion_reason = "hidden entry excluded by settings";
            result.entries.push_back(entry);
            continue;
        }

        auto is_directory = m_fs.is_directory(current.path);
        auto is_regular = m_fs.is_regular_file(current.path);
        if (is_directory.has_error()) {
            entry.error = is_directory.error().message;
            result.issues.push_back(ScanIssue{current.path, is_directory.error().code, "is_directory",
                                              is_directory.error().message});
            result.complete = false;
        }
        if (is_regular.has_error()) {
            entry.error = is_regular.error().message;
            result.issues.push_back(ScanIssue{current.path, is_regular.error().code, "is_regular_file",
                                              is_regular.error().message});
            result.complete = false;
        }

        if (is_directory.ok() && is_directory.value()) {
            entry.is_dir = true;
            result.entries.push_back(entry);

            auto children = m_fs.list_directory(current.path);
            if (children.has_error()) {
                result.entries.back().error = children.error().message;
                result.issues.push_back(ScanIssue{current.path, children.error().code,
                                                  "list_directory", children.error().message});
                result.complete = false;
                continue;
            }

            auto listed = children.value();
            std::sort(listed.begin(), listed.end());
            // Push in reverse so the deterministic sorted order is preserved when
            // the stack is popped.
            for (auto it = listed.rbegin(); it != listed.rend(); ++it) {
                std::filesystem::path relative = current.relative;
                relative /= it->filename();
                stack.push_back(Pending{*it, relative, current.root_index, current.root_path});
            }
            continue;
        }

        if (is_regular.ok() && is_regular.value()) {
            entry.is_file = true;
            auto size = m_fs.file_size(current.path);
            if (size.ok()) {
                entry.size = size.value();
            } else {
                entry.error = size.error().message;
                result.issues.push_back(ScanIssue{current.path, size.error().code, "file_size",
                                                  size.error().message});
                result.complete = false;
            }
            result.entries.push_back(entry);
            continue;
        }

        entry.error = "unsupported entry kind";
        result.issues.push_back(ScanIssue{current.path, ErrorCode::Unsupported, "scan",
                                          "Entry is neither a regular file nor a directory"});
        result.entries.push_back(entry);
        result.complete = false;
    }

    std::sort(result.entries.begin(), result.entries.end(), entry_sort_key_less);
    if (!result.issues.empty()) result.complete = false;
    return result;
}

Result<std::vector<ScannedEntry>> Scanner::scan(const std::vector<std::filesystem::path>& roots,
                                                bool follow_symlinks) const {
    ScanOptions options;
    options.roots = roots;
    options.follow_symlinks = follow_symlinks;
    auto result = scan(options);
    if (result.has_error()) return result.error();
    return result.value().entries;
}

} // namespace dupli
