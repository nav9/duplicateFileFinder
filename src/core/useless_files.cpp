#include "dupli/useless_files.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace dupli {

namespace {

constexpr const char* kWindows = "Windows";
constexpr const char* kMacOs = "macOS";
constexpr const char* kLinux = "Linux";
constexpr const char* kCross = "Cross-platform";

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Classic iterative glob match for a single name component. `*` may match any
/// run of characters (including none), `?` exactly one character. There is no
/// escape syntax, no character class and no path traversal: anything that would
/// turn a pattern into a shell expression is rejected earlier.
bool glob_match_impl(std::string_view pattern, std::string_view name, bool case_sensitive) {
    size_t pattern_index = 0;
    size_t name_index = 0;
    size_t star_index = std::string_view::npos;
    size_t star_name_index = 0;

    auto equal = [case_sensitive](char left, char right) {
        return case_sensitive ? left == right : ascii_lower(left) == ascii_lower(right);
    };

    while (name_index < name.size()) {
        if (pattern_index < pattern.size() &&
            (pattern[pattern_index] == '?' || equal(pattern[pattern_index], name[name_index]))) {
            ++pattern_index;
            ++name_index;
        } else if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_index = pattern_index++;
            star_name_index = name_index;
        } else if (star_index != std::string_view::npos) {
            pattern_index = star_index + 1;
            name_index = ++star_name_index;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') ++pattern_index;
    return pattern_index == pattern.size();
}

bool is_valid_pattern(std::string_view pattern, std::string* problem) {
    auto fail = [problem](const std::string& message) {
        if (problem) *problem = message;
        return false;
    };
    if (pattern.empty()) return fail("The pattern is empty.");
    bool only_space = true;
    for (char c : pattern) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (c == '/' || c == '\\') {
            return fail("Patterns match file names only; a path separator is not allowed.");
        }
        if (byte < 0x20) return fail("The pattern contains a control character.");
        if (c != ' ' && c != '\t') only_space = false;
    }
    if (only_space) return fail("The pattern contains only whitespace.");
    return true;
}

bool is_within(const std::filesystem::path& prefix, const std::filesystem::path& candidate) {
    const auto normalized_prefix = prefix.lexically_normal();
    const auto normalized_candidate = candidate.lexically_normal();
    auto prefix_it = normalized_prefix.begin();
    auto candidate_it = normalized_candidate.begin();
    for (; prefix_it != normalized_prefix.end(); ++prefix_it, ++candidate_it) {
        if (candidate_it == normalized_candidate.end()) return false;
        if (candidate_it->native() != prefix_it->native()) return false;
    }
    return true;
}

/// Deterministic, never-overwriting name choice. The same call with the same
/// inputs always produces the same names, which keeps plans reproducible.
std::filesystem::path unique_destination(const std::filesystem::path& directory,
                                         const std::filesystem::path& relative,
                                         std::set<std::string>& used, IFileSystem& fs) {
    auto available = [&used, &fs](const std::filesystem::path& candidate) {
        if (!used.insert(candidate.generic_string()).second) return false;
        auto exists = fs.exists(candidate);
        return exists.ok() && !exists.value();
    };

    std::filesystem::path candidate = (directory / relative).lexically_normal();
    if (available(candidate)) return candidate;

    const std::string stem = relative.stem().string();
    const std::string extension = relative.extension().string();
    const auto parent = relative.parent_path();
    for (int suffix = 1; suffix < 100000; ++suffix) {
        std::filesystem::path renamed =
            parent / (stem + "_" + std::to_string(suffix) + extension);
        candidate = (directory / renamed).lexically_normal();
        if (available(candidate)) return candidate;
    }
    return candidate;
}

const UselessPattern* find_pattern(const std::vector<UselessPattern>& patterns,
                                   const std::string& pattern) {
    for (const auto& candidate : patterns) {
        if (candidate.pattern == pattern) return &candidate;
    }
    return nullptr;
}

} // namespace

std::string to_string(UselessPatternGroup group) {
    switch (group) {
    case UselessPatternGroup::Windows: return kWindows;
    case UselessPatternGroup::MacOs: return kMacOs;
    case UselessPatternGroup::Linux: return kLinux;
    case UselessPatternGroup::CrossPlatform: return kCross;
    }
    return kCross;
}

const std::vector<UselessPattern>& builtin_useless_patterns() {
    // Only well-understood generated files are enabled by default. Anything that
    // can carry folder configuration or resource-fork data stays opt-in and
    // explains itself, because calling useful metadata "useless" is a decision
    // the user must make.
    static const std::vector<UselessPattern> patterns = {
        {"Thumbs.db", "Windows thumbnail cache", UselessPatternGroup::Windows, true,
         "Explorer's cached folder thumbnails. Regenerated automatically; no user content.",
         ""},
        {"ehthumbs.db", "Windows Media Center thumbnails", UselessPatternGroup::Windows, true,
         "Media Center's cached thumbnails. Regenerated automatically.", ""},
        {"Desktop.ini", "Windows folder configuration", UselessPatternGroup::Windows, false,
         "Stores a folder's icon, localized name and other view settings.",
         "Not selected by default: deleting it can change how a folder looks or is named."},
        {".DS_Store", "macOS Finder settings", UselessPatternGroup::MacOs, true,
         "Finder's per-folder view settings. Regenerated automatically; contains no documents.",
         ""},
        {"._*", "macOS AppleDouble sidecars", UselessPatternGroup::MacOs, false,
         "Holds extended attributes and resource forks for a copied file.",
         "Not selected by default: the sidecar may be the only copy of a file's resource fork."},
        {"Thumbs.db", "Thumbnail cache in a copied folder", UselessPatternGroup::Linux, true,
         "A Windows cache file that travelled with a copied folder. Regenerated on Windows.",
         ""},
        {"*.tmp", "Temporary files", UselessPatternGroup::CrossPlatform, false,
         "Temporary files written by many programs.",
         "Not selected by default: a running program may still be using its temporary file."},
        {"*.crdownload", "Incomplete Chrome download", UselessPatternGroup::CrossPlatform, true,
         "A partially downloaded file left behind by Chrome.", ""},
        {"*.part", "Incomplete Firefox download", UselessPatternGroup::CrossPlatform, true,
         "A partially downloaded file left behind by Firefox.", ""},
        {".nfs*", "NFS temporary file", UselessPatternGroup::Linux, true,
         "A deleted-but-still-open file left behind by an NFS client.", ""},
    };
    return patterns;
}

bool is_valid_useless_glob(std::string_view pattern, std::string* problem) {
    return is_valid_pattern(pattern, problem);
}

bool glob_matches_filename(std::string_view pattern, std::string_view filename,
                           bool case_sensitive) {
    if (!is_valid_pattern(pattern, nullptr)) return false;
    return glob_match_impl(pattern, filename, case_sensitive);
}

ParsedPatternList parse_useless_pattern_list(std::string_view comma_separated) {
    ParsedPatternList parsed;
    size_t begin = 0;
    while (begin <= comma_separated.size()) {
        const size_t comma = comma_separated.find(',', begin);
        const size_t end = comma == std::string_view::npos ? comma_separated.size() : comma;
        std::string_view part = comma_separated.substr(begin, end - begin);
        // Surrounding whitespace is layout between list entries, not part of a
        // file name; an intentional leading/trailing space in a name is rare and
        // cannot be expressed in this version.
        while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
            part.remove_prefix(1);
        }
        while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
            part.remove_suffix(1);
        }
        std::string problem;
        if (!part.empty()) {
            if (is_valid_pattern(part, &problem)) {
                parsed.patterns.emplace_back(part);
            } else {
                parsed.problems.push_back(std::string(part) + ": " + problem);
            }
        }
        if (comma == std::string_view::npos) break;
        begin = comma + 1;
    }
    return parsed;
}

Result<UselessFileScanResult> UselessFileScanner::scan(
    const ScanOptions& options, const std::vector<std::string>& patterns) const {
    if (options.roots.empty()) {
        return Error{ErrorCode::InvalidArgument, "No folders were selected", "", "useless_scan"};
    }
    if (patterns.empty()) {
        return Error{ErrorCode::InvalidArgument, "No file patterns were selected", "",
                     "useless_scan"};
    }
    for (const auto& pattern : patterns) {
        std::string problem;
        if (!is_valid_pattern(pattern, &problem)) {
            return Error{ErrorCode::InvalidArgument, "Invalid pattern '" + pattern + "': " + problem,
                         "", "useless_scan"};
        }
    }

    Scanner scanner(m_fs);
    auto scanned = scanner.scan(options);
    if (scanned.has_error()) return scanned.error();

    UselessFileScanResult result;
    result.issues = scanned.value().issues;
    result.complete = scanned.value().complete;

    const auto& presets = builtin_useless_patterns();
    for (const auto& entry : scanned.value().entries) {
        if (!entry.is_file || entry.excluded || !entry.error.empty()) continue;
        UselessMatch match;
        for (const auto& pattern : patterns) {
            if (!glob_matches_filename(pattern, entry.path.filename().string())) continue;
            match.matched_patterns.push_back(pattern);
        }
        if (match.matched_patterns.empty()) continue;

        match.entry = entry;
        match.reason = "Matches ";
        for (size_t index = 0; index < match.matched_patterns.size(); ++index) {
            if (index > 0) match.reason += ", ";
            match.reason += "'" + match.matched_patterns[index] + "'";
            const UselessPattern* preset = find_pattern(presets, match.matched_patterns[index]);
            if (preset) match.reason += " (" + preset->label + ")";
        }
        result.total_bytes += entry.size;
        result.matches.push_back(std::move(match));
    }

    // Deterministic order makes previews, plans and tests comparable.
    std::sort(result.matches.begin(), result.matches.end(),
              [](const UselessMatch& left, const UselessMatch& right) {
                  return left.entry.path.generic_string() < right.entry.path.generic_string();
              });

    if (!result.complete) {
        result.warnings.push_back(
            "The folder scan was not complete, so some matching files may be missing.");
    }
    if (result.matches.empty()) {
        result.warnings.push_back("No files matching the selected patterns were found.");
    }
    return result;
}

Result<ActionPlan> UselessFilePlanner::plan_moves(const UselessMoveRequest& request) const {
    if (request.destination.empty()) {
        return Error{ErrorCode::InvalidArgument, "Choose a quarantine destination folder", "",
                     "useless_plan"};
    }
    if (request.sources.empty()) {
        return Error{ErrorCode::InvalidArgument, "No files were selected", "", "useless_plan"};
    }

    std::vector<std::string> warnings;
    for (const auto& root : request.scan_roots) {
        if (is_within(root, request.destination)) {
            warnings.push_back(
                "The quarantine folder is inside a scanned folder. Add it as an exclusion so a "
                "later scan does not pick the moved files up again.");
            break;
        }
    }

    // Sort sources so the keep-both suffixes are assigned deterministically
    // regardless of the order the UI happened to send them in.
    std::vector<std::filesystem::path> sources = request.sources;
    std::sort(sources.begin(), sources.end(), [](const auto& left, const auto& right) {
        return left.generic_string() < right.generic_string();
    });

    std::vector<PlannedAction> actions;
    std::set<std::string> used;
    size_t skipped = 0;

    for (const auto& source : sources) {
        auto is_file = m_fs.is_regular_file(source);
        if (is_file.has_error() || !is_file.value()) {
            ++skipped;
            warnings.push_back("Skipped " + path_to_utf8(source) + ": it is no longer a regular file");
            continue;
        }
        auto is_link = m_fs.is_symlink(source);
        if (is_link.has_error() || is_link.value()) {
            ++skipped;
            warnings.push_back("Skipped " + path_to_utf8(source) + ": links are never moved");
            continue;
        }

        auto opened = m_fs.open_read(source);
        if (opened.has_error()) {
            ++skipped;
            warnings.push_back("Skipped " + path_to_utf8(source) + ": " +
                               opened.error().message);
            continue;
        }
        auto digest = m_hasher.hash_file(*opened.value());
        (void)opened.value()->close();
        if (digest.has_error() || !digest.value().complete) {
            ++skipped;
            warnings.push_back("Skipped " + path_to_utf8(source) +
                               ": its content could not be read completely");
            continue;
        }

        std::filesystem::path relative;
        for (const auto& root : request.roots) {
            if (!is_within(root, source)) continue;
            relative = source.lexically_relative(root);
            break;
        }
        if (relative.empty() || relative == ".") relative = source.filename();
        // A relative path must never escape the destination, even if a caller
        // supplied a root that is not a real ancestor.
        for (const auto& part : relative) {
            if (part == "..") {
                relative = source.filename();
                break;
            }
        }

        auto size = m_fs.file_size(source);
        PlannedAction action;
        action.kind = ActionKind::Move;
        action.source = source;
        action.destination = unique_destination(request.destination, relative, used, m_fs);
        action.expected_digest = digest.value().hex;
        action.expected_size = size.ok() ? size.value() : digest.value().bytes_hashed;
        action.rationale = "Matched a useless-file pattern; moved to quarantine, never deleted";
        actions.push_back(std::move(action));
    }

    if (actions.empty()) {
        return Error{ErrorCode::InvalidArgument,
                     "None of the selected files can be moved safely", "", "useless_plan"};
    }
    if (skipped > 0) {
        warnings.push_back(std::to_string(skipped) +
                           " selected file(s) were skipped because they could not be verified.");
    }

    return assemble_plan(std::move(actions), ActionMode::QuarantineDuplicates,
                         request.destination, warnings);
}

} // namespace dupli
