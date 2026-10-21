#pragma once

#include "core/databasemanager.h"
#include "safefileops.h"

#include <QObject>
#include <QStringList>
#include <QVector>

namespace dedupvault::operations {

/**
 * @brief Reads the action journal and reverses completed operations.
 *
 * Every move performed through SafeFileOps is journaled with both its
 * original and its new location, so any completed action can be undone from
 * the History screen — files first, subtrees as well.
 */
class UndoManager : public QObject {
    Q_OBJECT

public:
    struct UndoReport {
        int     succeeded = 0;
        int     failed = 0;
        QStringList errors;
    };

    UndoManager(core::DatabaseManager& db, SafeFileOps& ops,
                 QObject* parent = nullptr);

    /// Journal entries, newest first; optionally filtered by status
    /// (planned | done | undone | failed).
    QVector<core::ActionRow> history(const QString& statusFilter = QString()) const;

    MoveOutcome undoOne(qint64 actionId);
    UndoReport  undoMany(const QVector<qint64>& actionIds);

signals:
    void historyChanged();
    void undoFinished(int succeeded, int failed);

private:
    core::DatabaseManager& m_db;
    SafeFileOps&           m_ops;
};

} // namespace dedupvault::operations