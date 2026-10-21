#include "duplicatedetector.h"
#include "core/filesystemutils.h"
#include "core/logger.h"

#include <algorithm>

namespace dedupvault::engine {

namespace {

bool isInsideProtected(const core::FileRow& row, const DetectorConfig& config) {
    if (config.protectedRootPath.isEmpty()) return false;
    return core::FileSystemUtils::isContainedIn(row.absPath, config.protectedRootPath);
}

int rootOrderIndex(const core::FileRow& row, const DetectorConfig& config) {
    const int idx = config.rootOrder.indexOf(row.rootPath);
    return idx < 0 ? config.rootOrder.size() : idx;
}

/// Deterministic total order for keeper selection.
bool keeperLess(const core::FileRow& a, const core::FileRow& b,
                const DetectorConfig& config) {
    const bool aProtected = isInsideProtected(a, config);
    const bool bProtected = isInsideProtected(b, config);
    if (aProtected != bProtected) return aProtected; // protected root wins

    const bool aMain = a.rootRole == QLatin1String("main");
    const bool bMain = b.rootRole == QLatin1String("main");
    if (aMain != bMain) return aMain;

    const int ar = rootOrderIndex(a, config);
    const int br = rootOrderIndex(b, config);
    if (ar != br) return ar < br;

    // Case-sensitive comparison: on case-insensitive filesystems two paths
    // differing only by case are still distinct strings, and the choice must
    // remain stable and reproducible across platforms.
    const int cmp = QString::compare(a.relPath, b.relPath, Qt::CaseSensitive);
    if (cmp != 0) return cmp < 0;

    return a.fileId < b.fileId;
}

} // namespace

QHash<qint64, bool> DuplicateDetector::selectKeepers(const QVector<core::FileRow>& members,
                                                     const DetectorConfig& config) {
    QHash<qint64, bool> flags;
    if (members.isEmpty()) return flags;

    const core::FileRow* best = &members.first();
    for (const core::FileRow& m : members) {
        if (keeperLess(m, *best, config)) best = &m;
    }
    for (const core::FileRow& m : members) {
        flags.insert(m.fileId, m.fileId == best->fileId);
    }
    return flags;
}

QVector<DuplicateProposal> DuplicateDetector::buildProposals(core::DatabaseManager& db,
                                                             const DetectorConfig& config) {
    QVector<DuplicateProposal> proposals;

    const QVector<core::FileRow> rows = db.hashedFilesWithSharedSize();
    if (rows.isEmpty()) return proposals;

    // Group in memory by (size, hash): rows arrive already ordered by both.
    QVector<core::FileRow> current;
    current.reserve(64);
    QHash<qint64, bool> allKeepers; // fileId -> isKeeper (shared with the DB)
    qint64 currentSize = -1;
    QString currentHash;

    auto flushGroup = [&]() {
        if (current.size() >= 2 && currentSize >= config.minGroupSize) {
            const QHash<qint64, bool> keeperFlags = selectKeepers(current, config);
            for (auto it = keeperFlags.cbegin(); it != keeperFlags.cend(); ++it) {
                allKeepers.insert(it.key(), it.value());
            }

            DuplicateProposal prop;
            prop.size = currentSize;
            prop.hash = currentHash;
            for (const core::FileRow& m : current) {
                if (keeperFlags.value(m.fileId, false)) {
                    prop.keeperFileId = m.fileId;
                    prop.keeperPath = m.absPath;
                    prop.keeperRole = m.rootRole;
                    break;
                }
            }
            for (const core::FileRow& m : current) {
                if (m.fileId == prop.keeperFileId) continue;
                if (m.rootRole == QLatin1String("main") || isInsideProtected(m, config)) {
                    prop.protectedOnes.append(m);
                } else {
                    prop.redundant.append(m);
                }
            }
            // Bytes recoverable = every additional copy that can be moved out
            // of the candidate folders (protected copies are never counted).
            prop.recoverableBytes = static_cast<qint64>(prop.redundant.size()) * prop.size;
            const bool actionable = !prop.redundant.isEmpty();
            const bool reportableProtected =
                config.reportProtectedOnlyGroups && !prop.protectedOnes.isEmpty();
            if (actionable || reportableProtected) {
                proposals.append(std::move(prop));
            }
        }
        current.clear();
    };

    for (const core::FileRow& row : rows) {
        if (current.isEmpty()) {
            currentSize = row.size;
            currentHash = row.sha256;
        }
        if (row.size != currentSize || row.sha256 != currentHash) {
            flushGroup();
            currentSize = row.size;
            currentHash = row.sha256;
        }
        current.append(row);
    }
    flushGroup();

    // Persist groups, memberships and keeper flags in one transaction.
    const int groupCount = db.rebuildDuplicateGroups(allKeepers);
    Q_UNUSED(groupCount);

    // Attach persisted group ids to proposals.
    const QVector<core::DuplicateGroupRow> persisted = db.duplicateGroups();
    QHash<QString, qint64> idByHash; // key: size|hash
    idByHash.reserve(persisted.size());
    for (const auto& g : persisted) {
        idByHash.insert(QStringLiteral("%1|%2").arg(g.size).arg(g.hash), g.groupId);
    }
    for (DuplicateProposal& p : proposals) {
        p.groupId = idByHash.value(QStringLiteral("%1|%2").arg(p.size).arg(p.hash), 0);
    }

    qint64 proposedMoves = 0;
    qint64 recoverable = 0;
    qint64 protectedOnly = 0;
    for (const auto& p : proposals) {
        proposedMoves += p.redundant.size();
        recoverable += p.recoverableBytes;
        if (p.redundant.isEmpty()) ++protectedOnly;
    }
    core::Logger::instance().info(
        QStringLiteral("Duplicate analysis: %1 groups (%2 inside the main "
                       "folder only), %3 proposed moves, %4 bytes "
                       "recoverable")
            .arg(proposals.size()).arg(protectedOnly).arg(proposedMoves)
            .arg(recoverable));

    return proposals;
}

} // namespace dedupvault::engine
