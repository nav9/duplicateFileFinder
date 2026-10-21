#pragma once

#include "core/databasemanager.h"
#include "core/filesystemutils.h"

#include <QObject>
#include <QString>

namespace dedupvault::operations {

/**
 * @brief Result of executing (or undoing) one journaled operation.
 */
struct MoveOutcome {
    bool    ok = false;
    bool    dryRun = false;
    QString error;
    qint64  actionId = 0;
    QString actualDest;   // may differ from the planned path on collision
};

/**
 * @brief Centralized safety checks and execution of move operations.
 *
 * No UI component or worker may touch files directly: every move goes
 * through the checks in execute(). The journal (action_log) is written
 * before the filesystem is touched and finalized after verification, so a
 * crash at any point can be reconciled by reconcile() on the next start.
 *
 * Move strategy: same-filesystem moves use rename(2) (atomic). Cross-
 * filesystem moves copy to a temporary name, verify the destination
 * (size, and SHA-256 when verification is enabled) and only then remove
 * the source, so an interrupted copy never corrupts the destination.
 */
class SafeFileOps : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString protectedRoot;   // main folder; never a move source
        bool    verifyHash = true;
        bool    dryRun = false;
    };

    explicit SafeFileOps(core::DatabaseManager& db, QObject* parent = nullptr);

    /// Full validation without side effects (used by the UI before enabling
    /// an action button as well as by execute()).
    static core::FileSystemUtils::MoveValidation validate(const core::ActionRow& action,
                                                          const QString& protectedRoot);

    /// Execute a journaled operation: re-validate, persist, move, verify.
    MoveOutcome execute(const core::ActionRow& action, const Options& options);

    /// Reverse a completed operation, restoring the original location.
    MoveOutcome undo(const core::ActionRow& action);

    /// Reconcile the journal with actual filesystem state after a crash.
    /// \return number of rows corrected.
    int reconcile();

    /// Streaming SHA-256 check of a file's content (bounded memory).
    static bool verifyFileHash(const QString& path, const QString& expectedSha256,
                               QString* error);

signals:
    void actionCompleted(qint64 actionId, const QString& destPath);
    void actionFailed(qint64 actionId, const QString& error);

private:
    core::DatabaseManager& m_db;

    enum class MoveKind { File, Directory };

    MoveOutcome perform(qint64 actionId, const QString& source, const QString& dest,
                        MoveKind kind, const Options& options, bool isUndo);

    static bool movePath(const QString& source, const QString& dest, MoveKind kind,
                         bool verifyHash, QString* error);
    static bool copyFileVerified(const QString& src, const QString& dst,
                                 bool verifyHash, QString* error);
    static bool copyTreeVerified(const QString& src, const QString& dst,
                                 QString* error);
    static bool dirStats(const QString& path, qint64* fileCount, qint64* totalSize,
                         QString* error);
    static void removePartialCopy(const QString& destPath);
};

} // namespace dedupvault::operations