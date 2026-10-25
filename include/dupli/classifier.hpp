#pragma once

#include "dupli/grouping.hpp"
#include "dupli/fingerprint.hpp"
#include "dupli/filesystem.hpp"
#include <vector>

namespace dupli {

enum class RelationshipKind {
    ExactFileDuplicate,
    ExactTreeDuplicate,
    ContentSetMatch,
    StrictSubset,
    PartialOverlap,
    SameNameDifferentContent,
    RelativePathConflict,
    SameUnderlyingFile,
    Inconclusive
};

struct Relationship {
    RelationshipKind kind;
    std::vector<std::filesystem::path> entries;
    std::string evidence;
    double confidence = 0.0;
};

class RelationshipClassifier {
public:
    RelationshipClassifier(IFileSystem& fs, IHasher& hasher) 
        : m_fs(fs), m_hasher(hasher), m_grouper(fs, hasher), m_fp(fs, hasher) {}
    
    Result<std::vector<Relationship>> classify(const std::vector<std::filesystem::path>& paths) {
        std::vector<Relationship> relationships;
        
        // Check for exact file duplicates
        auto groups = m_grouper.group_duplicates(paths);
        if (groups.ok()) {
            for (const auto& group : groups.value()) {
                Relationship rel;
                rel.kind = RelationshipKind::ExactFileDuplicate;
                rel.entries = group.files;
                rel.evidence = "Identical BLAKE3 content: " + group.digest_hex;
                rel.confidence = 1.0;
                relationships.push_back(rel);
            }
        }
        
        // Check for exact tree duplicates
        std::vector<std::filesystem::path> dirs;
        for (const auto& p : paths) {
            auto is_dir = m_fs.is_directory(p);
            if (is_dir.ok() && is_dir.value()) {
                dirs.push_back(p);
            }
        }
        
        for (size_t i = 0; i < dirs.size(); ++i) {
            auto fp1 = m_fp.fingerprint_tree(dirs[i]);
            if (!fp1.ok() || !fp1.value().complete) continue;
            for (size_t j = i+1; j < dirs.size(); ++j) {
                auto fp2 = m_fp.fingerprint_tree(dirs[j]);
                if (!fp2.ok() || !fp2.value().complete) continue;
                if (fp1.value().fingerprint == fp2.value().fingerprint) {
                    Relationship rel;
                    rel.kind = RelationshipKind::ExactTreeDuplicate;
                    rel.entries = {dirs[i], dirs[j]};
                    rel.evidence = "Identical tree fingerprint";
                    rel.confidence = 1.0;
                    relationships.push_back(rel);
                }
            }
        }
        
        return relationships;
    }
    
private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
    DuplicateGrouper m_grouper;
    FolderFingerprinter m_fp;
};

} // namespace dupli
