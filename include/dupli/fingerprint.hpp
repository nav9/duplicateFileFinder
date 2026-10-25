#pragma once

#include "dupli/hasher.hpp"
#include "dupli/filesystem.hpp"
#include <map>
#include <string>
#include <vector>

namespace dupli {

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
            return result;
        }
        result.fingerprint = fp;
        result.complete = true;
        return result;
    }
    
private:
    std::string compute_tree_fingerprint(const std::filesystem::path& current_path, const std::string& relative_path) {
        // Use deterministic ordering
        auto list_result = m_fs.list_directory(current_path);
        if (!list_result.ok()) {
            return ""; // Incomplete - don't certify
        }
        
        std::vector<std::filesystem::path> entries = list_result.value();
        // Sort for determinism
        std::sort(entries.begin(), entries.end());
        
        std::string content;
        content += "DUPLI_TREE_V1|";
        
        for (const auto& entry : entries) {
            auto is_dir = m_fs.is_directory(entry);
            auto is_file = m_fs.is_regular_file(entry);
            
            if (is_dir.ok() && is_dir.value()) {
                std::string name = entry.filename().string();
                std::string child_rel = relative_path.empty() ? name : relative_path + "/" + name;
                std::string child_fp = compute_tree_fingerprint(entry, child_rel);
                if (child_fp.empty()) {
                    return ""; // Propagate incomplete
                }
                content += "DIR:" + name + ":" + child_fp + "|";
            } else if (is_file.ok() && is_file.value()) {
                std::string name = entry.filename().string();
                auto read_result = m_fs.open_read(entry);
                if (!read_result.ok()) {
                    return "";
                }
                auto digest_result = m_hasher.hash_file(*read_result.value());
                if (!digest_result.ok() || !digest_result.value().complete) {
                    return "";
                }
                content += "FILE:" + name + ":" + digest_result.value().hex + "|";
            }
            // Skip other types
        }
        
        // Hash the content to create fingerprint
        auto data_digest = m_hasher.hash_data(to_bytes(content));
        return data_digest.value().hex;
    }
    
    std::vector<std::byte> to_bytes(const std::string& s) {
        std::vector<std::byte> bytes;
        bytes.reserve(s.size());
        for (char c : s) {
            bytes.push_back(static_cast<std::byte>(c));
        }
        return bytes;
    }
    
    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
