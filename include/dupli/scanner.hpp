#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/result.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace dupli {

// ScanOptions captures every user-visible scan choice. A copy of these options
// is taken when a task starts so later changes cannot affect a running scan.
struct ScanOptions {
    std::vector<std::filesystem::path> roots;
    // Symlinks are always reported and skipped. The flag exists so the policy is
    // explicit; it is never enabled by the CLI.
    bool follow_symlinks = false;
    bool include_hidden = true;
    // Default is to stay inside one filesystem so mount points/network shares
    // are not scanned accidentally. Users can opt in.
    bool cross_filesystem_boundaries = false;
    /// Paths (files or directory trees) that must not be scanned at all.
    std::vector<std::filesystem::path> excludes;
    /// Safety valve for accidental scans of huge trees; 0 means unlimited.
    size_t max_entries = 0;
};

// A problem encountered while scanning. Scans continue after these are
// recorded; the report surfaces them so "no duplicates found" is never claimed
// when part of the tree could not be read.
struct ScanIssue {
    std::filesystem::path path;
    ErrorCode code = ErrorCode::Unknown;
    std::string operation;
    std::string message;
};

struct ScannedEntry {
    /// Stable within one scan: "<root index>:<relative path>". Paths are not
    /// used as sole identity because a path can be reused after a move.
    std::string id;
    std::filesystem::path path;
    std::filesystem::path relative_path;
    size_t root_index = 0;
    bool is_file = false;
    bool is_dir = false;
    bool is_symlink = false;
    bool is_hidden = false;
    bool excluded = false;
    std::string exclusion_reason;
    uintmax_t size = 0;
    /// Non-empty when this entry is reported but cannot be used (e.g. a broken
    /// entry or a skipped link).
    std::string error;
};

struct ScanResult {
    std::vector<ScannedEntry> entries;
    std::vector<ScanIssue> issues;
    std::vector<std::filesystem::path> roots;
    /// True only when every selected tree was traversed without any issue.
    bool complete = true;
    bool has_skipped_links = false;
    bool truncated = false;

    std::vector<ScannedEntry> files() const;
    std::vector<ScannedEntry> directories() const;
    size_t file_count() const;
    size_t directory_count() const;
};

/// Deterministic ordering used for every emitted list. Shallower paths come
/// first, so when duplicates are grouped the copy closest to its scan root is
/// the one suggested to keep (and the report and planner agree on which copy
/// that is). Ties fall back to root index, relative path and absolute path.
bool entry_sort_key_less(const ScannedEntry& left, const ScannedEntry& right);

// Scanner walks the selected roots without following links. It records problems
// instead of aborting, avoids double-counting overlapping roots, and never
// touches file content (hashing is a separate, injectable step).
class Scanner {
public:
    explicit Scanner(IFileSystem& fs) : m_fs(fs) {}

    Result<ScanResult> scan(const ScanOptions& options) const;

    /// Convenience overload kept for callers/tests that only need entry paths.
    Result<std::vector<ScannedEntry>> scan(const std::vector<std::filesystem::path>& roots,
                                           bool follow_symlinks = false) const;

private:
    IFileSystem& m_fs;
};

} // namespace dupli
