#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace dupli {

// Bottom-up folder fingerprint.
//
// The encoding is unambiguous: every entry is written as
//   <kind letter><name byte length>:<name><payload>
// where payload is a child fingerprint, a 32-byte digest rendered as hex, or
// nothing for links. Length prefixes stop two different trees from producing the
// same byte stream (e.g. "ab"+"c" vs "a"+"bc"). A versioned domain separator
// prevents this format from colliding with any other hash input.
//
// Only content and structure are included. Modification times and permissions are
// metadata and deliberately excluded, so metadata-only differences do not change
// tree identity (they are reported separately).
//
// If any subtree cannot be listed or any file cannot be fully hashed, the whole
// fingerprint fails instead of certifying a partial tree as an exact duplicate.
class FolderFingerprinter {
public:
    FolderFingerprinter(IFileSystem& fs, IHasher& hasher)
        : m_fs(fs), m_hasher(hasher) {}

    struct TreeFingerprint {
        std::string fingerprint;
        bool complete = false;
        std::vector<std::string> errors;
    };

    Result<TreeFingerprint> fingerprint_tree(const std::filesystem::path& root) {
        TreeFingerprint result;
        auto fp = compute_tree_fingerprint(root, "");
        if (fp.empty()) {
            result.complete = false;
            result.errors.push_back("Tree could not be read completely; not certified as a duplicate");
            return result;
        }
        result.fingerprint = fp;
        result.complete = true;
        return result;
    }

private:
    static std::string encode_name(char kind, const std::string& name) {
        return std::string(1, kind) + std::to_string(name.size()) + ":" + name;
    }

    std::string compute_tree_fingerprint(const std::filesystem::path& current_path,
                                         const std::string& relative_path) {
        (void)relative_path; // Relative paths are implied by the recursive encoding.
        auto list_result = m_fs.list_directory(current_path);
        if (!list_result.ok()) {
            return ""; // Incomplete: never certify a partially readable tree.
        }

        std::vector<std::filesystem::path> entries = list_result.value();
        std::sort(entries.begin(), entries.end()); // Deterministic across listings.

        std::string content = "DUPLI_TREE_V1|";

        for (const auto& entry : entries) {
            const std::string name = path_to_utf8(entry.filename());

            // Links are part of the structure (so two trees that differ only by a
            // link are not called identical) but are never followed.
            auto is_link = m_fs.is_symlink(entry);
            if (is_link.has_error()) return "";
            if (is_link.value()) {
                content += encode_name('L', name);
                continue;
            }

            auto is_dir = m_fs.is_directory(entry);
            auto is_file = m_fs.is_regular_file(entry);

            if (is_dir.ok() && is_dir.value()) {
                const std::string child = compute_tree_fingerprint(entry, relative_path);
                if (child.empty()) return "";
                content += encode_name('D', name) + child;
            } else if (is_file.ok() && is_file.value()) {
                auto read_result = m_fs.open_read(entry);
                if (!read_result.ok()) return "";
                auto digest_result = m_hasher.hash_file(*read_result.value());
                if (!digest_result.ok() || !digest_result.value().complete) return "";
                content += encode_name('F', name) + digest_result.value().hex;
            } else {
                // Special files (sockets, devices, ...) cannot be certified.
                return "";
            }
        }

        auto data_digest = m_hasher.hash_data(to_bytes(content));
        if (!data_digest.ok() || !data_digest.value().complete) return "";
        return data_digest.value().hex;
    }

    static std::vector<std::byte> to_bytes(const std::string& text) {
        std::vector<std::byte> bytes;
        bytes.reserve(text.size());
        for (char c : text) bytes.push_back(static_cast<std::byte>(c));
        return bytes;
    }

    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
