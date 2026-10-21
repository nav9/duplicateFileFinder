#pragma once

#include "core/databasemanager.h"

namespace dedupvault::engine {

/**
 * @brief Folder and subtree analysis module.
 *
 * Computes per-folder statistics and classifications, detects exact
 * duplicate subtrees and partial overlaps across roots, and persists
 * FolderMatch rows as evidence for user review. Folder matches are never
 * authorization to move anything on their own.
 */
class FolderAnalyzer {
public:
    FolderAnalyzer() = delete;

    /// Phase 1: per-folder file counts, sizes and classification.
    static void computeFolderStats(core::DatabaseManager& db);

    /// Phase 2: cross-root folder comparison producing folder_match rows.
    /// \param signatureFloor minimum overlap (0..1) worth recording.
    static void compareFolders(core::DatabaseManager& db, double signatureFloor = 0.34);

    /// Classifies a directory on disk: normal | git_repo | website | project.
    static QString classifyFolder(const QString& absPath);
};

} // namespace dedupvault::engine
