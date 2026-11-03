#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/grouping.hpp"
#include "dupli/hasher.hpp"
#include "dupli/journal.hpp"
#include "dupli/scanner.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dupli {

struct DetectedContainer {
    std::filesystem::path path;
    std::string type; // "git" or "web_archive"
    std::vector<std::string> warnings;
    bool requires_explicit_approval = true;
};

struct ReportItem {
    std::string path;
    std::string relative_path;
    uintmax_t size = 0;
    std::string note;
};

struct ReportGroup {
    std::string title;
    /// Why this group exists. Never a bare hash: the digest goes in `digest`
    /// (technical details) and the evidence explains the relationship.
    std::string evidence;
    std::string digest;
    std::vector<ReportItem> items;
    bool requires_review = false;
    std::vector<std::string> warnings;
};

struct ReportCategory {
    std::string id;
    std::string title;
    std::string description;
    std::vector<ReportGroup> groups;
};

struct ReportSummary {
    size_t scan_entries = 0;
    size_t files_scanned = 0;
    size_t duplicate_groups = 0;
    size_t duplicate_files = 0;
    size_t unique_files = 0;
    size_t unreadable_files = 0;
    size_t empty_directories = 0;
    size_t containers = 0;
    size_t skipped_links = 0;
    size_t excluded_entries = 0;
    uintmax_t total_file_bytes = 0;
    uintmax_t reclaimable_bytes = 0;
};

/// Everything a user (or a later GUI) needs to review results before deciding.
/// Categories are always present, even when empty, so consumers can rely on
/// stable ids instead of guessing from free-form text.
struct Report {
    std::string format = "dupli-report";
    int version = 1;
    std::string mode = "scan";
    std::string generated_at;
    std::vector<std::filesystem::path> roots;
    bool scan_complete = true;
    bool truncated = false;
    ReportSummary summary;
    std::vector<ReportCategory> categories;
    std::vector<ScanIssue> issues;
    std::vector<std::string> execution_notes;

    std::string to_json(bool pretty = true) const;
    Result<void> save(IFileSystem& fs, const std::filesystem::path& path) const;
};

struct ReportOptions {
    /// Tree fingerprints re-hash every file inside each selected root; useful but
    /// expensive, so callers can disable it.
    bool include_tree_fingerprints = true;
    bool include_content_sets = true;
};

class ReportBuilder {
public:
    ReportBuilder(IFileSystem& fs, IHasher& hasher, IClock& clock, const ScanResult& scan,
                  const GroupingResult& grouping)
        : m_fs(fs), m_hasher(hasher), m_clock(clock), m_scan(scan), m_grouping(grouping) {}

    void set_containers(std::vector<DetectedContainer> containers) {
        m_containers = std::move(containers);
    }
    void set_execution_notes(std::vector<std::string> notes) { m_execution_notes = std::move(notes); }
    void set_options(ReportOptions options) { m_options = options; }

    Report build(const std::string& mode) const;

private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
    IClock& m_clock;
    const ScanResult& m_scan;
    const GroupingResult& m_grouping;
    std::vector<DetectedContainer> m_containers;
    std::vector<std::string> m_execution_notes;
    ReportOptions m_options;
};

} // namespace dupli
