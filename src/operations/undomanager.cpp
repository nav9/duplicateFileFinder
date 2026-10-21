#include "undomanager.h"
#include "core/logger.h"

namespace dedupvault::operations {

UndoManager::UndoManager(core::DatabaseManager& db, SafeFileOps& ops, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_ops(ops)
{
    connect(&m_ops, &SafeFileOps::actionCompleted, this,
            [this](qint64, const QString&) { emit historyChanged(); });
    connect(&m_ops, &SafeFileOps::actionFailed, this,
            [this](qint64, const QString&) { emit historyChanged(); });
}

QVector<core::ActionRow> UndoManager::history(const QString& statusFilter) const {
    return m_db.actions(statusFilter);
}

MoveOutcome UndoManager::undoOne(qint64 actionId) {
    const auto action = m_db.actionById(actionId);
    if (!action) {
        return {false, false, tr("Unknown action id: %1").arg(actionId),
                actionId, QString()};
    }
    const MoveOutcome outcome = m_ops.undo(*action);
    if (outcome.ok) emit historyChanged();
    return outcome;
}

UndoManager::UndoReport UndoManager::undoMany(const QVector<qint64>& actionIds) {
    UndoReport report;
    for (const qint64 id : actionIds) {
        const MoveOutcome outcome = undoOne(id);
        if (outcome.ok) {
            ++report.succeeded;
        } else if (!outcome.dryRun) {
            ++report.failed;
            report.errors.append(tr("Action %1: %2").arg(id).arg(outcome.error));
            core::Logger::instance().warning(report.errors.last());
        }
    }
    emit undoFinished(report.succeeded, report.failed);
    return report;
}

} // namespace dedupvault::operations