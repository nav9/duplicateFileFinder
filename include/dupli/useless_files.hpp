#pragma once

#include "dupli/action_planner.hpp"
#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/scanner.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace dupli {

// Finding and quarantining OS-generated cache/metadata files ("useless files").
//
// Three rules shape this module:
//  * Nothing is ever deleted: selected files are MOVED to a quarantine folder
//    through the same validated, journaled executor the duplicate workflow uses.
//  * A pattern only ever matches the *filename component*. It is a glob, never a
//    shell expansion or a regular expression, and it can never reach outside the
//    selected roots.
//  * Potentially useful metadata (Desktop.ini, AppleDouble sidecars) is offered
//    but never enabled by default and always carries a warning.

enum class UselessPatternGroup { Windows, MacOs, Linux, CrossPlatform };

std::string to_string(UselessPatternGroup group);

struct UselessPattern {
    /// Glob matched against the file name, e.g. "Thumbs.db", "._*", "*.tmp".
    std::string pattern;
    /// Short human name shown next to the checkbox.
    std::string label;
    UselessPatternGroup group = UselessPatternGroup::CrossPlatform;
    /// Presets a user can rely on without thinking. Metadata that can carry real
    /// configuration is never in this category.
    bool default_enabled = true;
    /// Plain-language explanation of what the file is.
    std::string description;
    /// Non-empty when the file may matter to the user; the UI must show it.
    std::string warning;
};

/// OS-labelled presets. They are always available, even on another OS, because
/// copied folders routinely contain foreign cache files.
const std::vector<UselessPattern>& builtin_useless_patterns();

/// Validate one glob. Rejected: empty/whitespace-only patterns, patterns that
/// contain a path separator (they must match names only) and control characters.
bool is_valid_useless_glob(std::string_view pattern, std::string* problem);

/// Case-insensitive ASCII matching by default: the presets must behave the same
/// on Windows/macOS (case-preserving but usually case-insensitive) and Linux.
/// `*` matches any run of characters, `?` exactly one. No escaping, no classes.
bool glob_matches_filename(std::string_view pattern, std::string_view filename,
                           bool case_sensitive = false);

struct ParsedPatternList {
    std::vector<std::string> patterns;
    /// One entry per rejected part, so the UI can explain exactly what is wrong
    /// instead of silently ignoring a typo.
    std::vector<std::string> problems;
};

/// Split a comma-separated pattern list. Literal commas cannot be escaped in
/// version 1; the UI says so explicitly.
ParsedPatternList parse_useless_pattern_list(std::string_view comma_separated);

struct UselessMatch {
    ScannedEntry entry;
    /// Every pattern that matched, in the order the patterns were supplied.
    std::vector<std::string> matched_patterns;
    /// Human-readable reason, e.g. "Matches 'Thumbs.db' (Windows thumbnail cache)".
    std::string reason;
};

struct UselessFileScanResult {
    std::vector<UselessMatch> matches;
    std::vector<ScanIssue> issues;
    std::vector<std::string> warnings;
    uintmax_t total_bytes = 0;
    /// True only when the traversal finished without a single issue. An
    /// incomplete scan is reported as such and never described as exhaustive.
    bool complete = true;
};

class UselessFileScanner {
public:
    explicit UselessFileScanner(IFileSystem& fs) : m_fs(fs) {}

    /// Walk the roots with the link-safe scanner and collect every regular file
    /// whose name matches at least one pattern. Each file appears once, even if
    /// several patterns match it.
    Result<UselessFileScanResult> scan(const ScanOptions& options,
                                       const std::vector<std::string>& patterns) const;

private:
    IFileSystem& m_fs;
};

struct UselessMoveRequest {
    /// Files the user explicitly ticked. Nothing else is touched.
    std::vector<std::filesystem::path> sources;
    std::filesystem::path destination;
    /// Roots used to compute the preserved relative layout. When a source lies
    /// outside every root its bare filename is used.
    std::vector<std::filesystem::path> roots;
    /// Add a warning when the destination would be scanned again by a later run.
    std::vector<std::filesystem::path> scan_roots;
};

/// Builds a move plan for the quarantine workflow. Planning is read-only; the
/// plan is executed by the shared Validator/Executor pair, which re-hashes every
/// source, journals the move and makes undo possible.
class UselessFilePlanner {
public:
    UselessFilePlanner(IFileSystem& fs, IHasher& hasher) : m_fs(fs), m_hasher(hasher) {}

    Result<ActionPlan> plan_moves(const UselessMoveRequest& request) const;

private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
