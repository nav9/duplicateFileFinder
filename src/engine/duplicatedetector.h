#pragma once

#include "core/databasemanager.h"

#include <QHash>
#include <QVector>
#include <QString>

namespace dedupvault::engine {

/**
 * @brief One exact-content duplicate group with a deterministically
 * selected keeper and the redundant copies proposed for consolidation.
 */
struct DuplicateProposal {
    qint64  groupId = 0;
    qint64  size = 0;
    QString hash;
    qint64  keeperFileId = 0;
    QString keeperPath;
    QString keeperRole;                 // main | candidate
    QVector<core::FileRow> redundant;   // proposed for moving
    QVector<core::FileRow> protectedOnes; // copies inside the main root (never moved)
    qint64  recoverableBytes = 0;
};

struct DetectorConfig {
    QString protectedRootPath;   // the main/reference folder
    QStringList rootOrder;       // root paths in selection order (determinism)
    bool    byteCompareBeforeMove = true; // high-assurance re-check
    qint64  minGroupSize = 1;
    // Report groups that consist only of protected (main folder) copies.
    // They are informational: nothing inside the main folder is movable.
    bool    reportProtectedOnlyGroups = false;
};

/**
 * @brief Grouping and keeper-selection rules for exact duplicates.
 *
 * Rules:
 *  - a copy inside the protected main root always wins the keeper slot;
 *  - members inside the protected root are never proposed for moving;
 *  - duplicates that exist only inside the protected root are reported when
 *    reportProtectedOnlyGroups is set, with no actionable copies;
 *  - otherwise the keeper is chosen deterministically by root order,
 *    then by relative path, then by file id.
 */
class DuplicateDetector {
public:
    DuplicateDetector() = delete;

    /// Keeper flags (fileId -> isKeeper) for the current catalog contents.
    static QHash<qint64, bool> selectKeepers(const QVector<core::FileRow>& members,
                                             const DetectorConfig& config);

    /// Groups (size, SHA-256) pairs, persists them and returns proposals.
    static QVector<DuplicateProposal> buildProposals(core::DatabaseManager& db,
                                                     const DetectorConfig& config);
};

} // namespace dedupvault::engine
