#include "segregationmanager.h"
#include "core/logger.h"

#include <QDir>
#include <QFileInfo>

namespace dedupvault::operations {

SegregationManager::SegregationManager(core::DatabaseManager& db, QObject* parent)
    : QObject(parent)
    , m_db(db)
{
}

QString SegregationManager::segregationRoot(const QString& destinationRoot) const {
    return QDir(destinationRoot).filePath(QStringLiteral("segregated"));
}

QString SegregationManager::sanitizeName(const QString& name) {
    QString out;
    out.reserve(name.size());
    static const QString illegal = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : name) {
        out.append(illegal.contains(c) ? QLatin1Char('_') : c);
    }
    if (out.isEmpty() || out == QLatin1String(".") || out == QLatin1String("..")) {
        out = QStringLiteral("root");
    }
    return out;
}

QString SegregationManager::rootNameFor(const QString& rootPath,
                                       QSet<QString>* usedNames) const {
    const QFileInfo info(rootPath);
    QString base = sanitizeName(info.fileName());
    if (base.isEmpty()) base = QStringLiteral("root");

    QString candidate = base;
    int suffix = 2;
    while (usedNames->contains(candidate)) {
        candidate = QStringLiteral("%1_%2").arg(base).arg(suffix++);
    }
    usedNames->insert(candidate);
    return candidate;
}

QString SegregationManager::plannedDestination(const QString& destinationRoot,
                                              const QString& rootPath,
                                              const QString& relPath) const {
    // Disambiguation of identical root file-names uses only this call's own
    // bookkeeping, so callers that need stable names across a batch should
    // use planFileActions() instead.
    QSet<QString> used;
    const QString rootName = rootNameFor(rootPath, &used);
    return QDir(QDir(segregationRoot(destinationRoot)).filePath(rootName))
        .filePath(relPath);
}

int SegregationManager::planFileActions(qint64 jobId, const QString& destinationRoot,
                                       const QVector<engine::DuplicateProposal>& proposals) {
    if (destinationRoot.isEmpty() || proposals.isEmpty()) return 0;

    // Sources that already have any journal entry are never re-planned.
    QSet<QString> knownSources;
    const auto existing = m_db.actions();
    knownSources.reserve(existing.size());
    for (const core::ActionRow& a : existing) knownSources.insert(a.sourcePath);

    // Stable root names: derived from selection order of this batch.
    QSet<QString> usedNames;
    QHash<QString, QString> rootNames;
    for (const engine::DuplicateProposal& p : proposals) {
        for (const core::FileRow& f : p.redundant) {
            if (!rootNames.contains(f.rootPath)) {
                rootNames.insert(f.rootPath, rootNameFor(f.rootPath, &usedNames));
            }
        }
    }

    int planned = 0;
    for (const engine::DuplicateProposal& p : proposals) {
        for (const core::FileRow& f : p.redundant) {
            if (knownSources.contains(f.absPath)) continue;

            const QString dest = QDir(QDir(segregationRoot(destinationRoot))
                                          .filePath(rootNames.value(f.rootPath,
                                                                    QStringLiteral("root"))))
                                     .filePath(f.relPath);

            if (dest == f.absPath) continue; // never plan identity moves

            const QString identity =
                QStringLiteral("sha256=%1;size=%2").arg(f.sha256).arg(f.size);
            const qint64 actionId =
                m_db.recordPlannedAction(jobId, QStringLiteral("move_file"),
                                         f.absPath, dest, identity,
                                         QStringLiteral("segregation planning"));
            if (actionId > 0) {
                knownSources.insert(f.absPath);
                ++planned;
            } else {
                core::Logger::instance().error(
                    QStringLiteral("Failed to journal planned move for %1: %2")
                        .arg(f.absPath, m_db.lastError()));
            }
        }
    }

    if (planned > 0) {
        core::Logger::instance().info(
            QStringLiteral("Planned %1 segregation move(s) into %2")
                .arg(planned).arg(segregationRoot(destinationRoot)));
        emit actionsPlanned(planned);
    }
    return planned;
}

qint64 SegregationManager::planSubtreeAction(qint64 jobId, const QString& sourceDir,
                                            const QString& intendedDest) {
    if (sourceDir.isEmpty() || intendedDest.isEmpty()) return -1;

    for (const core::ActionRow& a : m_db.actions()) {
        if (a.sourcePath == sourceDir &&
            (a.status == QLatin1String("planned") ||
             a.status == QLatin1String("done"))) {
            return a.actionId; // already planned — idempotent
        }
    }

    const QString identity =
        QStringLiteral("subtree;files_hashed_at_move_time");
    const qint64 id =
        m_db.recordPlannedAction(jobId, QStringLiteral("move_subtree"),
                                 sourceDir, intendedDest, identity,
                                 QStringLiteral("approved folder match"));
    if (id > 0) {
        core::Logger::instance().info(
            QStringLiteral("Planned subtree move: %1 -> %2").arg(sourceDir, intendedDest));
        emit actionsPlanned(1);
    }
    return id;
}

} // namespace dedupvault::operations