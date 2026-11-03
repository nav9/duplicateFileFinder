#include "dupli/report.hpp"

#include "dupli/fingerprint.hpp"
#include "dupli/json.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dupli {

namespace {

constexpr int kReportFormatVersion = 1;

ReportItem item_from_entry(const ScannedEntry& entry, const std::string& note = "") {
    ReportItem item;
    item.path = path_to_utf8(entry.path);
    item.relative_path = entry.relative_path.generic_string();
    item.size = entry.size;
    item.note = note;
    return item;
}

ReportCategory make_category(std::string id, std::string title, std::string description) {
    ReportCategory category;
    category.id = std::move(id);
    category.title = std::move(title);
    category.description = std::move(description);
    return category;
}

std::string join(const std::vector<std::string>& parts, const std::string& separator) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += separator;
        out += parts[i];
    }
    return out;
}

json::Value category_to_json(const ReportCategory& category) {
    json::Value value;
    value.set("id", category.id);
    value.set("title", category.title);
    value.set("description", category.description);
    json::Value groups(json::Value::Array{});
    for (const auto& group : category.groups) {
        json::Value group_value;
        group_value.set("title", group.title);
        group_value.set("evidence", group.evidence);
        group_value.set("digest", group.digest);
        group_value.set("requires_review", group.requires_review);
        json::Value warnings(json::Value::Array{});
        for (const auto& warning : group.warnings) warnings.push(warning);
        group_value.set("warnings", warnings);
        json::Value items(json::Value::Array{});
        for (const auto& item : group.items) {
            json::Value item_value;
            item_value.set("path", item.path);
            item_value.set("relative_path", item.relative_path);
            item_value.set("size", item.size);
            item_value.set("note", item.note);
            items.push(std::move(item_value));
        }
        group_value.set("items", items);
        groups.push(std::move(group_value));
    }
    value.set("groups", groups);
    return value;
}

} // namespace

std::string Report::to_json(bool pretty) const {
    json::Value root;
    root.set("format", format);
    root.set("version", version);
    root.set("mode", mode);
    root.set("generated_at", generated_at);
    root.set("scan_complete", scan_complete);
    root.set("truncated", truncated);

    json::Value roots_value(json::Value::Array{});
    for (const auto& root_path : roots) roots_value.push(path_to_utf8(root_path));
    root.set("roots", roots_value);

    json::Value summary_value;
    summary_value.set("scan_entries", this->summary.scan_entries);
    summary_value.set("files_scanned", this->summary.files_scanned);
    summary_value.set("duplicate_groups", this->summary.duplicate_groups);
    summary_value.set("duplicate_files", this->summary.duplicate_files);
    summary_value.set("unique_files", this->summary.unique_files);
    summary_value.set("unreadable_files", this->summary.unreadable_files);
    summary_value.set("empty_directories", this->summary.empty_directories);
    summary_value.set("containers", this->summary.containers);
    summary_value.set("skipped_links", this->summary.skipped_links);
    summary_value.set("excluded_entries", this->summary.excluded_entries);
    summary_value.set("total_file_bytes", this->summary.total_file_bytes);
    summary_value.set("reclaimable_bytes", this->summary.reclaimable_bytes);
    root.set("summary", summary_value);

    json::Value categories_value(json::Value::Array{});
    for (const auto& category : this->categories) categories_value.push(category_to_json(category));
    root.set("categories", categories_value);

    json::Value issues(json::Value::Array{});
    for (const auto& issue : this->issues) {
        json::Value item;
        item.set("path", path_to_utf8(issue.path));
        item.set("code", to_string(issue.code));
        item.set("operation", issue.operation);
        item.set("message", issue.message);
        issues.push(std::move(item));
    }
    root.set("issues", issues);

    json::Value notes(json::Value::Array{});
    for (const auto& note : execution_notes) notes.push(note);
    root.set("execution_notes", notes);

    return json::dump(root, pretty) + "\n";
}

Result<void> Report::save(IFileSystem& fs, const std::filesystem::path& path) const {
    if (path.has_parent_path()) {
        auto exists = fs.exists(path.parent_path());
        if (exists.ok() && !exists.value()) {
            auto created = fs.create_directory(path.parent_path());
            if (created.has_error()) return created.error();
        }
    }
    auto opened = fs.open_write(path);
    if (opened.has_error()) return opened.error();

    const std::string text = to_json(true);
    auto written = opened.value()->write(reinterpret_cast<const std::byte*>(text.data()),
                                         text.size());
    if (written.has_error()) return written.error();
    if (written.value() != text.size()) {
        return Error{ErrorCode::IoError, "Not all report bytes were written",
                     path_to_utf8(path), "report_save"};
    }
    auto flushed = opened.value()->flush();
    if (flushed.has_error()) return flushed.error();
    return opened.value()->close();
}

Report ReportBuilder::build(const std::string& mode) const {
    Report report;
    report.version = kReportFormatVersion;
    report.mode = mode;
    report.generated_at = m_clock.now_iso8601();
    report.roots = m_scan.roots;
    report.scan_complete = m_scan.complete && m_grouping.complete() && !m_scan.truncated;
    report.truncated = m_scan.truncated;
    report.issues = m_scan.issues;
    report.issues.insert(report.issues.end(), m_grouping.issues.begin(),
                         m_grouping.issues.end());
    report.execution_notes = m_execution_notes;

    ReportCategory duplicates = make_category(
        "duplicate_files", "Exact duplicate files",
        "Files whose complete BLAKE3 digests are identical. Names, extensions and locations are "
        "irrelevant; the digest is computed over every byte.");
    for (const auto& group : m_grouping.groups) {
        ReportGroup entry;
        const std::string name = path_to_utf8(group.files.front().path.filename());
        entry.title = name + " - " + std::to_string(group.files.size()) + " identical copies";
        entry.evidence = "Identical BLAKE3 content over all " + std::to_string(group.size) +
                         " bytes; " + std::to_string(group.reclaimable_bytes()) +
                         " bytes are reclaimable if all but one copy are quarantined";
        entry.digest = group.digest_hex;
        for (size_t i = 0; i < group.files.size(); ++i) {
            entry.items.push_back(item_from_entry(
                group.files[i], i == 0 ? "Suggested copy to keep (deterministic order)"
                                       : "Duplicate copy"));
        }
        duplicates.groups.push_back(std::move(entry));
    }

    ReportCategory unique = make_category(
        "unique_files", "Unique files",
        "Files whose full content occurs exactly once. A file that could not be read is listed "
        "under unreadable entries instead, never here.");
    if (!m_grouping.unique_files.empty()) {
        ReportGroup entry;
        entry.title = std::to_string(m_grouping.unique_files.size()) + " files with unique content";
        entry.evidence = "Each listed file's BLAKE3 digest occurs exactly once across every "
                         "selected root";
        for (const auto& file : m_grouping.unique_files) {
            entry.items.push_back(item_from_entry(file));
        }
        unique.groups.push_back(std::move(entry));
    }

    ReportCategory unreadable = make_category(
        "unreadable_entries", "Inaccessible, unreadable or unsupported entries",
        "Entries that could not be scanned or hashed. They are never classified as duplicates or "
        "as unique, because their real content is unknown.");
    for (const auto& file : m_grouping.unreadable_files) {
        ReportGroup entry;
        entry.title = path_to_utf8(file.path.filename());
        entry.evidence = "Content could not be read: " + file.error;
        entry.requires_review = true;
        entry.items.push_back(item_from_entry(file, file.error));
        unreadable.groups.push_back(std::move(entry));
    }
    for (const auto& issue : m_scan.issues) {
        if (issue.operation == "boundary") continue; // reported in its own category below
        ReportGroup entry;
        entry.title = path_to_utf8(issue.path.filename());
        entry.evidence = std::string(to_string(issue.code)) + " during " + issue.operation + ": " +
                         issue.message;
        entry.requires_review = true;
        ReportItem item;
        item.path = path_to_utf8(issue.path);
        item.note = issue.message;
        entry.items.push_back(std::move(item));
        unreadable.groups.push_back(std::move(entry));
    }

    // Content-set relationships between selected roots. These are content-only
    // comparisons (path layout ignored) and are kept separate from exact tree
    // duplicates so a "same files, different layout" result is never presented
    // as an exact duplicate.
    ReportCategory content_sets = make_category(
        "content_sets", "Content-set relationships",
        "Whole-root comparisons by content multiset, independent of folder layout. These are not "
        "exact tree duplicates.");
    if (m_options.include_content_sets && m_scan.roots.size() > 1) {
        std::map<size_t, std::map<std::string, size_t>> multiset;
        for (const auto& entry : m_scan.entries) {
            if (!entry.is_file || !entry.error.empty() || entry.excluded) continue;
            auto digest = m_grouping.digest_by_id.find(entry.id);
            if (digest == m_grouping.digest_by_id.end()) continue;
            multiset[entry.root_index][digest->second]++;
        }

        for (size_t left = 0; left < m_scan.roots.size(); ++left) {
            for (size_t right = left + 1; right < m_scan.roots.size(); ++right) {
                const auto& left_set = multiset[left];
                const auto& right_set = multiset[right];
                if (left_set.empty() && right_set.empty()) continue;

                size_t left_extra = 0;
                size_t right_extra = 0;
                size_t shared = 0;
                std::set<std::string> all_digests;
                for (const auto& [digest, count] : left_set) {
                    all_digests.insert(digest);
                    (void)count;
                }
                for (const auto& [digest, count] : right_set) {
                    all_digests.insert(digest);
                    (void)count;
                }
                for (const auto& digest : all_digests) {
                    auto left_it = left_set.find(digest);
                    auto right_it = right_set.find(digest);
                    const size_t left_count = left_it == left_set.end() ? 0 : left_it->second;
                    const size_t right_count = right_it == right_set.end() ? 0 : right_it->second;
                    shared += std::min(left_count, right_count);
                    left_extra += left_count > right_count ? left_count - right_count : 0;
                    right_extra += right_count > left_count ? right_count - left_count : 0;
                }

                ReportGroup entry;
                entry.title = path_to_utf8(m_scan.roots[left].filename()) + " <-> " +
                              path_to_utf8(m_scan.roots[right].filename());
                if (left_extra == 0 && right_extra == 0) {
                    entry.evidence = "Same content multiset: all " + std::to_string(shared) +
                                     " file contents match, possibly at different paths";
                } else if (left_extra == 0 && shared > 0) {
                    entry.evidence = "Left root is a strict content subset: " +
                                     std::to_string(shared) + " shared, " +
                                     std::to_string(right_extra) + " only on the right";
                    entry.requires_review = true;
                } else if (right_extra == 0 && shared > 0) {
                    entry.evidence = "Right root is a strict content subset: " +
                                     std::to_string(shared) + " shared, " +
                                     std::to_string(left_extra) + " only on the left";
                    entry.requires_review = true;
                } else if (shared > 0) {
                    entry.evidence = "Partial content overlap: " + std::to_string(shared) +
                                     " shared, " + std::to_string(left_extra) + " only left, " +
                                     std::to_string(right_extra) + " only right";
                    entry.requires_review = true;
                } else {
                    continue; // No shared content: no relationship worth reporting.
                }
                ReportItem left_item;
                left_item.path = path_to_utf8(m_scan.roots[left]);
                left_item.note = "root";
                left_item.size = left_set.size();
                ReportItem right_item;
                right_item.path = path_to_utf8(m_scan.roots[right]);
                right_item.note = "root";
                right_item.size = right_set.size();
                entry.items.push_back(std::move(left_item));
                entry.items.push_back(std::move(right_item));
                content_sets.groups.push_back(std::move(entry));
            }
        }
    }

    ReportCategory trees = make_category(
        "tree_duplicates", "Exact duplicate directory trees",
        "Whole trees whose relative paths, entry kinds and complete file contents are identical. "
        "Only certified when every subtree could be read.");
    if (m_options.include_tree_fingerprints) {
        FolderFingerprinter fingerprinter(m_fs, m_hasher);
        std::map<std::string, std::vector<std::filesystem::path>> by_fingerprint;
        for (const auto& root_path : m_scan.roots) {
            auto is_dir = m_fs.is_directory(root_path);
            if (!is_dir.ok() || !is_dir.value()) continue;
            auto fingerprint = fingerprinter.fingerprint_tree(root_path);
            if (!fingerprint.ok() || !fingerprint.value().complete) continue;
            by_fingerprint[fingerprint.value().fingerprint].push_back(root_path);
        }
        for (const auto& [fingerprint, roots] : by_fingerprint) {
            if (roots.size() < 2) continue;
            ReportGroup entry;
            entry.title = std::to_string(roots.size()) + " identical trees";
            entry.evidence =
                "Identical tree fingerprints including empty directories and every file's content";
            entry.digest = fingerprint;
            for (const auto& root_path : roots) {
                ReportItem item;
                item.path = path_to_utf8(root_path);
                entry.items.push_back(std::move(item));
            }
            trees.groups.push_back(std::move(entry));
        }
    }

    ReportCategory conflicts = make_category(
        "relative_path_conflicts", "Same relative path, different content",
        "Entries that occupy the same relative path in different roots but have different "
        "content. Merging these requires a decision.");
    {
        std::map<std::string, std::map<std::string, const ScannedEntry*>> by_relative;
        for (const auto& entry : m_scan.entries) {
            if (!entry.is_file || !entry.error.empty() || entry.excluded) continue;
            auto digest = m_grouping.digest_by_id.find(entry.id);
            if (digest == m_grouping.digest_by_id.end()) continue;
            by_relative[entry.relative_path.generic_string()][digest->second] = &entry;
        }
        for (const auto& [relative, digests] : by_relative) {
            if (digests.size() < 2) continue;
            ReportGroup entry;
            entry.title = relative;
            entry.evidence = "Different content at the same relative path (" +
                             std::to_string(digests.size()) + " distinct contents)";
            entry.requires_review = true;
            for (const auto& [digest, scanned] : digests) {
                entry.items.push_back(item_from_entry(*scanned, "content differs"));
            }
            conflicts.groups.push_back(std::move(entry));
        }
    }

    ReportCategory containers = make_category(
        "containers", "Special containers (Git repositories, saved web pages)",
        "Compound items that must be reviewed as a unit. Moving or merging them needs explicit "
        "per-operation approval.");
    for (const auto& container : m_containers) {
        ReportGroup entry;
        entry.title = container.type == "git" ? "Git repository" : "Saved web page with resources";
        entry.evidence = join(container.warnings, " ");
        if (entry.evidence.empty()) entry.evidence = "Detected by structural evidence";
        entry.requires_review = container.requires_explicit_approval;
        for (const auto& warning : container.warnings) entry.warnings.push_back(warning);
        ReportItem item;
        item.path = path_to_utf8(container.path);
        item.note = "container root";
        entry.items.push_back(std::move(item));
        containers.groups.push_back(std::move(entry));
    }

    ReportCategory links = make_category(
        "skipped_links", "Symbolic links (reported, never followed)",
        "Links are listed for transparency but are not traversed, copied or moved.");
    {
        ReportGroup entry;
        entry.title = "Symbolic links";
        entry.evidence = "Links are skipped to avoid loops and escaping the selected tree";
        for (const auto& scanned : m_scan.entries) {
            if (!scanned.is_symlink) continue;
            entry.items.push_back(item_from_entry(scanned, "not followed"));
        }
        if (!entry.items.empty()) links.groups.push_back(std::move(entry));
    }

    ReportCategory excluded = make_category(
        "excluded_entries", "Excluded or protected entries",
        "Entries deliberately left out by the current settings or exclusion rules.");
    {
        ReportGroup entry;
        entry.title = "Excluded entries";
        entry.evidence = "Excluded by scan settings; they were not compared";
        for (const auto& scanned : m_scan.entries) {
            if (!scanned.excluded) continue;
            entry.items.push_back(item_from_entry(scanned, scanned.exclusion_reason));
        }
        if (!entry.items.empty()) excluded.groups.push_back(std::move(entry));
    }

    ReportCategory empty_dirs = make_category(
        "empty_directories", "Empty directories",
        "Directories with no entries beneath them. Useful for cleanup after moving duplicates.");
    {
        std::set<std::string> parents;
        for (const auto& entry : m_scan.entries) {
            if (entry.relative_path == "." || entry.relative_path.empty()) continue;
            parents.insert(entry.relative_path.parent_path().generic_string());
        }
        ReportGroup entry;
        entry.title = "Empty directories";
        entry.evidence = "No files or subdirectories inside";
        for (const auto& scanned : m_scan.directories()) {
            if (scanned.relative_path == "." || scanned.relative_path.empty()) continue;
            if (parents.count(scanned.relative_path.generic_string())) continue;
            entry.items.push_back(item_from_entry(scanned));
        }
        if (!entry.items.empty()) empty_dirs.groups.push_back(std::move(entry));
    }

    ReportCategory execution = make_category(
        "execution", "Execution and recovery",
        "Outcome of applied operations. Partially completed work is never reported as completed.");
    if (!m_execution_notes.empty()) {
        ReportGroup entry;
        entry.title = "Operation results";
        entry.evidence = join(m_execution_notes, " | ");
        entry.requires_review = true;
        execution.groups.push_back(std::move(entry));
    }

    report.categories = {duplicates, trees,   content_sets, conflicts,  containers,
                         unique,     unreadable, links,     excluded, empty_dirs,
                         execution};

    // --- Summary -----------------------------------------------------------
    report.summary.scan_entries = m_scan.entries.size();
    report.summary.files_scanned = m_grouping.unique_files.size();
    report.summary.duplicate_groups = m_grouping.groups.size();
    for (const auto& group : m_grouping.groups) {
        report.summary.duplicate_files += group.files.size();
        report.summary.reclaimable_bytes += group.reclaimable_bytes();
    }
    report.summary.files_scanned += report.summary.duplicate_files;
    report.summary.unique_files = m_grouping.unique_files.size();
    report.summary.unreadable_files = m_grouping.unreadable_files.size();
    report.summary.containers = m_containers.size();
    for (const auto& scanned : m_scan.entries) {
        if (scanned.is_symlink) ++report.summary.skipped_links;
        if (scanned.excluded) ++report.summary.excluded_entries;
        if (scanned.is_file) report.summary.total_file_bytes += scanned.size;
    }
    for (const auto& category : report.categories) {
        if (category.id != "empty_directories") continue;
        for (const auto& group : category.groups) {
            report.summary.empty_directories += group.items.size();
        }
    }
    return report;
}

} // namespace dupli
