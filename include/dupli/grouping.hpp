#pragma once

#include "dupli/hasher.hpp"
#include "dupli/filesystem.hpp"
#include <map>
#include <vector>
#include <string>

namespace dupli {

class DuplicateGrouper {
public:
    struct Group {
        std::string digest_hex;
        std::vector<std::filesystem::path> files;
        size_t total_size = 0;
    };
    
    DuplicateGrouper(IFileSystem& fs, IHasher& hasher) 
        : m_fs(fs), m_hasher(hasher) {}
    
    Result<std::vector<Group>> group_duplicates(const std::vector<std::filesystem::path>& paths) {
        std::map<std::string, Group> groups;
        
        for (const auto& path : paths) {
            auto is_file_result = m_fs.is_regular_file(path);
            if (!is_file_result.ok() || !is_file_result.value()) {
                continue;
            }
            
            auto read_result = m_fs.open_read(path);
            if (!read_result.ok()) {
                continue; // Skip unreadable files, don't classify as unique
            }
            
            auto digest_result = m_hasher.hash_file(*read_result.value());
            if (!digest_result.ok() || !digest_result.value().complete) {
                continue; // Failed hashes - don't classify as unique
            }
            
            std::string key = digest_result.value().hex;
            if (groups.find(key) == groups.end()) {
                groups[key] = Group{key, {}, 0};
            }
            groups[key].files.push_back(path);
        }
        
        std::vector<Group> result;
        for (auto& [key, group] : groups) {
            if (group.files.size() > 1) {
                result.push_back(std::move(group));
            }
        }
        return result;
    }
    
private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
};

} // namespace dupli
