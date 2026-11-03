#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/scanner.hpp"
#include <map>
#include <string>
#include <vector>

namespace dupli {

/// One set of files whose complete BLAKE3 digests are identical. Size is shown
/// for diagnostics and to estimate reclaimable space; it is never the identity.
struct DuplicateGroup {
    std::string digest_hex;
    uintmax_t size = 0;
    std::vector<ScannedEntry> files;

    uintmax_t reclaimable_bytes() const {
        if (files.size() < 2) return 0;
        return size * static_cast<uintmax_t>(files.size() - 1);
    }
};

/// Result of hashing every selected file once.
///
/// The three buckets are deliberately distinct:
///  * groups     - fully hashed, identical content (safe to call duplicates)
///  * unique     - fully hashed, no other file shares the digest
///  * unreadable - hashing failed; these are NOT unique and must not be reported
///                 as such, because a read error hides the real content.
struct GroupingResult {
    std::vector<DuplicateGroup> groups;
    std::vector<ScannedEntry> unique_files;
    std::vector<ScannedEntry> unreadable_files;
    std::map<std::string, std::string> digest_by_id;
    std::vector<ScanIssue> issues;
    uintmax_t bytes_hashed = 0;

    bool complete() const { return issues.empty() && unreadable_files.empty(); }
};

class DuplicateGrouper {
public:
    DuplicateGrouper(IFileSystem& fs, IHasher& hasher) : m_fs(fs), m_hasher(hasher) {}

    /// Hash every readable regular file and split the results into duplicate
    /// groups, unique files and unreadable files. Never classifies a failed read
    /// as unique.
    GroupingResult group(const std::vector<ScannedEntry>& files, size_t buffer_size = 64 * 1024);

    /// Legacy view used by earlier tests: only the duplicate groups.
    struct Group {
        std::string digest_hex;
        std::vector<std::filesystem::path> files;
        size_t total_size = 0;
    };
    Result<std::vector<Group>> group_duplicates(const std::vector<std::filesystem::path>& paths);

private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
