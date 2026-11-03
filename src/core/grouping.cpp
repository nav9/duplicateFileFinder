#include "dupli/grouping.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace dupli {

GroupingResult DuplicateGrouper::group(const std::vector<ScannedEntry>& files,
                                       size_t buffer_size) {
    GroupingResult result;
    std::map<std::string, std::vector<ScannedEntry>> by_digest;
    std::map<std::string, uintmax_t> size_by_digest;

    for (const auto& file : files) {
        if (!file.is_file || file.excluded) continue;
        if (!file.error.empty()) {
            // The scanner already knows this entry could not be inspected. It must
            // be reported as unreadable, never silently dropped (dropping it would
            // make it look unique or absent).
            result.unreadable_files.push_back(file);
            result.issues.push_back(ScanIssue{file.path, ErrorCode::IoError, "scan", file.error});
            continue;
        }

        auto opened = m_fs.open_read(file.path);
        if (opened.has_error()) {
            ScannedEntry failed = file;
            failed.error = opened.error().message;
            result.unreadable_files.push_back(failed);
            result.issues.push_back(ScanIssue{file.path, opened.error().code, "open_read",
                                              opened.error().message});
            continue;
        }

        auto digest = m_hasher.hash_file(*opened.value(), buffer_size);
        if (digest.has_error()) {
            ScannedEntry failed = file;
            failed.error = digest.error().message;
            result.unreadable_files.push_back(failed);
            result.issues.push_back(ScanIssue{file.path, digest.error().code, "hash_file",
                                              digest.error().message});
            continue;
        }
        if (!digest.value().complete) {
            // A partial digest can never establish identity.
            ScannedEntry failed = file;
            failed.error = "hashing did not complete";
            result.unreadable_files.push_back(failed);
            result.issues.push_back(ScanIssue{file.path, ErrorCode::IoError, "hash_file",
                                              "Hashing did not complete"});
            continue;
        }

        const auto& value = digest.value();
        result.bytes_hashed += value.bytes_hashed;
        result.digest_by_id[file.id] = value.hex;
        by_digest[value.hex].push_back(file);
        size_by_digest[value.hex] = static_cast<uintmax_t>(value.bytes_hashed);
    }

    for (auto& [hex, members] : by_digest) {
        if (members.size() > 1) {
            DuplicateGroup group;
            group.digest_hex = hex;
            group.size = size_by_digest[hex];
            group.files = std::move(members);
            std::sort(group.files.begin(), group.files.end(), entry_sort_key_less);
            result.groups.push_back(std::move(group));
        } else {
            result.unique_files.push_back(std::move(members.front()));
        }
    }

    std::sort(result.groups.begin(), result.groups.end(),
              [](const DuplicateGroup& left, const DuplicateGroup& right) {
                  return left.digest_hex < right.digest_hex;
              });
    std::sort(result.unique_files.begin(), result.unique_files.end(), entry_sort_key_less);
    std::sort(result.unreadable_files.begin(), result.unreadable_files.end(), entry_sort_key_less);
    return result;
}

Result<std::vector<DuplicateGrouper::Group>>
DuplicateGrouper::group_duplicates(const std::vector<std::filesystem::path>& paths) {
    std::map<std::string, Group> groups;
    for (const auto& path : paths) {
        auto is_file = m_fs.is_regular_file(path);
        if (!is_file.ok() || !is_file.value()) continue;

        auto opened = m_fs.open_read(path);
        if (opened.has_error()) continue;
        auto digest = m_hasher.hash_file(*opened.value());
        if (digest.has_error() || !digest.value().complete) continue;

        auto& group = groups[digest.value().hex];
        group.digest_hex = digest.value().hex;
        group.files.push_back(path);
        group.total_size += digest.value().bytes_hashed;
    }

    std::vector<Group> result;
    for (auto& [hex, group] : groups) {
        if (group.files.size() > 1) result.push_back(std::move(group));
    }
    return result;
}

} // namespace dupli
