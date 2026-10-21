#pragma once

#include "core/databasemanager.h"
#include "engine/duplicatedetector.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

namespace dedupvault::operations {

/**
 * @brief Destination-side planning for the segregation/review workflow.
 *
 * Redundant copies are mapped into a structured segregation area inside the
 * selected destination folder:
 *
 *     <destination>/segregated/<rootName>/<relative path>
 *
 * Paths are only *planned* here — every plan is a journaled row that the
 * user can review and approve; SafeFileOps performs the actual move.
 */
class SegregationManager : public QObject {
    Q_OBJECT

public:
    explicit SegregationManager(core::DatabaseManager& db, QObject* parent = nullptr);

    /// Root of the segregation area inside the destination folder.
    QString segregationRoot(const QString& destinationRoot) const;

    /// Full planned path for one redundant file.
    QString plannedDestination(const QString& destinationRoot,
                               const QString& rootPath,
                               const QString& relPath) const;

    /// Journal planned move_file actions for every redundant copy in the
    /// proposals. Idempotent: sources that already carry a planned/done
    /// action are skipped, so repeated calls never duplicate work.
    /// \return number of newly planned actions.
    int planFileActions(qint64 jobId, const QString& destinationRoot,
                        const QVector<engine::DuplicateProposal>& proposals);

    /// Journal one subtree move (only ever called after explicit user
    /// approval of a folder match).
    qint64 planSubtreeAction(qint64 jobId, const QString& sourceDir,
                             const QString& intendedDest);

signals:
    void actionsPlanned(int newCount);

private:
    core::DatabaseManager& m_db;

    /// Sanitized, collision-free folder name for a root path.
    QString rootNameFor(const QString& rootPath, QSet<QString>* usedNames) const;
    static QString sanitizeName(const QString& name);
};

} // namespace dedupvault::operations