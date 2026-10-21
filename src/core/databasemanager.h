#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <optional>
#include <cstdint>

namespace dedupvault::core {

/**
 * @brief Roles a scanned root folder can play in a deduplication job.
 */
enum class RootRole { Main, Candidate, Destination };

/**
 * @brief Lifecycle of a file row inside the catalog.
 */
enum class FileScanStatus {
    Pending          = 0,
    MetadataCollected = 1,
    Hashed           = 2,
    Error            = 3,
    Changed          = 4
};

enum class TaskStatus { Pending, Running, Completed, Failed, AwaitingReview, Cancelled };
enum class ActionStatus { Planned, Done, Undone, Failed };

struct RootRecord {
    qint64    id = 0;
    QString   path;
    RootRole  role = RootRole::Candidate;
    bool      enabled = true;
};

struct FileRecord {
    qint64   rootId   = 0;
    qint64   folderId = 0;
    QString  relPath;
    qint64   size     = 0;
    qint64   mtimeMsec = 0;
    quint64  deviceId = 0;
    quint64  inodeId  = 0;
    qint64   generation = 0; // scan generation that last saw this file
};

struct FileRow {
    qint64   fileId = 0;
    qint64   rootId = 0;
    QString  rootPath;
    QString  rootRole;
    QString  relPath;
    QString  absPath;
    qint64   size = 0;
    qint64   mtimeMsec = 0;
    QString  sha256;
    int      scanStatus = 0;
    QString  errorMessage;
};

struct DuplicateGroupRow {
    qint64  groupId = 0;
    qint64  size = 0;
    QString hash;
    qint64  keeperFileId = 0;
    QString keeperPath;
    int     memberCount = 0;
    qint64  wastedBytes = 0;
    QVector<FileRow> members;
};

struct FolderMatchRow {
    qint64  matchId = 0;
    qint64  folderAId = 0;
    qint64  folderBId = 0;
    QString pathA;
    QString pathB;
    QString matchType;      // exact_subtree | partial_overlap | structured
    double  overlapScore = 0.0;
    QString reviewStatus;   // pending | approved | skipped
    QString notes;
};

struct TaskRow {
    qint64  taskId = 0;
    qint64  jobId = 0;
    QString taskType;
    qint64  targetId = 0;
    QString status;
    int     attempts = 0;
    QString error;
};

struct ActionRow {
    qint64  actionId = 0;
    qint64  jobId = 0;
    QString opType;         // move_file | move_subtree
    QString sourcePath;
    QString destPath;
    QString expectedIdentity;
    QString status;         // planned | done | undone | failed
    QString createdAt;
    QString completedAt;
    QString recoveryInfo;
};

struct FolderRow {
    qint64  folderId = 0;
    qint64  rootId = 0;
    qint64  parentId = 0;
    QString relPath;
    QString absPath;
    QString rootPath;
    int     directFileCount = 0;
    qint64  recursiveFileCount = 0;
    qint64  totalSize = 0;
    QString subtreeSignature;
    QString classification; // normal | git_repo | website | project | unknown
};

struct JobRow {
    qint64  jobId = 0;
    QString status;   // running | paused | completed | cancelled | failed
    QString phase;    // idle | traversal | hashing | grouping | analysis | proposals | done
    QString startedAt;
    QString finishedAt;
};

/**
 * @brief Authoritative persistent catalog built on SQLite (Qt SQL).
 *
 * Threading model: a DatabaseManager instance is owned by exactly one
 * thread (the engine/UI thread) and all access must happen on that thread.
 * Background workers never touch the database directly; they post batches
 * back to the owning thread, which commits them in transactions.
 */
class DatabaseManager : public QObject {
    Q_OBJECT

public:
    explicit DatabaseManager(QObject* parent = nullptr);
    ~DatabaseManager() override;

    bool open(const QString& dbPath);
    void close();
    bool isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    // --- Schema / migrations ---
    int schemaVersion() const;
    bool migrate();

    // --- Roots ---
    qint64 upsertRoot(const QString& path, RootRole role, bool enabled = true);
    bool   removeRoot(qint64 rootId);
    QVector<RootRecord> roots(bool enabledOnly = false) const;
    std::optional<RootRecord> rootById(qint64 rootId) const;
    static QString roleToString(RootRole role);
    static RootRole roleFromString(const QString& role);

    // --- Folders ---
    qint64 upsertFolder(qint64 rootId, qint64 parentId, const QString& relPath);
    bool   updateFolderStats(qint64 folderId, int directFiles, qint64 recursiveFiles,
                             qint64 totalSize, const QString& signature,
                             const QString& classification);
    std::optional<FolderRow> folderById(qint64 folderId) const;
    QVector<FolderRow> foldersByRoot(qint64 rootId) const;

    // --- Files ---
    bool upsertFileMetadata(const QVector<FileRecord>& batch);    bool   updateFileHash(qint64 fileId, const QString& sha256);
    bool   markFileError(qint64 fileId, const QString& error);
    /// Persist fresh metadata after a file was observed to change on disk,
    /// invalidating its hash so the next job re-validates it.
    bool   resetFileAfterChange(qint64 fileId, qint64 size, qint64 mtimeMsec,
                                quint64 deviceId, quint64 inodeId);
    bool markFilesMissing(qint64 rootId, const QVector<QString>& relPaths);
    int  markStaleFiles(qint64 rootId, qint64 generation); // files not seen this run
    /// Drop catalog rows for absolute paths (moved/removed by an operation).
    int  removeFilesByAbsPath(const QStringList& absPaths);

    /// Files that still need hashing, bounded by \p limit for memory safety.
    /// With sharedSizeOnly (the staged-comparison default) only files whose
    /// size is shared with at least one other file are returned; otherwise
    /// every unhashed file is returned ("complete hashing" mode).
    QVector<FileRow> filesNeedingHash(int limit = 10000,
                                      bool sharedSizeOnly = true) const;
    /// Candidate files for duplicate analysis (size shared, hash known).
    QVector<FileRow> hashedFilesWithSharedSize() const;

    // --- Duplicate groups ---
    QVector<DuplicateGroupRow> duplicateGroups() const;
    bool clearDuplicateGroups();
    /// Builds groups from (size, sha256) with >1 member; assigns keepers
    /// by the provided keeper map (fileId -> isKeeper). Returns group count.
    int rebuildDuplicateGroups(const QHash<qint64, bool>& keeperFlags);

    // --- Folder matches ---
    bool clearFolderMatches();
    bool addFolderMatch(qint64 folderA, qint64 folderB, const QString& matchType,
                        double score, const QString& notes);
    bool setFolderMatchReview(qint64 matchId, const QString& status);
    QVector<FolderMatchRow> folderMatches() const;

    // --- Jobs ---
    qint64 createJob();
    bool   updateJobStatus(qint64 jobId, const QString& status, const QString& phase);
    bool   finishJob(qint64 jobId, const QString& status);
    std::optional<JobRow> latestResumableJob() const;
    std::optional<JobRow> jobById(qint64 jobId) const;

    // --- Tasks ---
    qint64 addTask(qint64 jobId, const QString& type, qint64 targetId);
    bool   updateTaskStatus(qint64 taskId, const QString& status, const QString& error = QString());
    QVector<TaskRow> tasksByJob(qint64 jobId) const;
    int    pendingTaskCount(qint64 jobId) const;
    /// Requeue tasks that were 'running' when the app died.
    int    recoverInterruptedTasks();
    /// Requeue all tasks of one type for a job (used when resuming a scan).
    int    resetTasks(qint64 jobId, const QString& taskType);

    // --- Action log (move/undo journal) ---
    qint64 recordPlannedAction(qint64 jobId, const QString& opType,
                               const QString& sourcePath, const QString& destPath,
                               const QString& expectedIdentity,
                               const QString& recoveryInfo = QString());
    bool   finalizeAction(qint64 actionId, const QString& status,
                          const QString& completedAt = QString());
    bool   setActionRecoveryInfo(qint64 actionId, const QString& info);
    QVector<ActionRow> actions(const QString& statusFilter = QString()) const;
    std::optional<ActionRow> actionById(qint64 actionId) const;

    // --- Statistics ---
    struct ScanStats {
        qint64 fileCount = 0;
        qint64 totalBytes = 0;
        qint64 hashedCount = 0;
        qint64 duplicateGroupCount = 0;
        qint64 duplicateFileCount = 0;
        qint64 wastedBytes = 0;
        qint64 errorCount = 0;
        qint64 pendingReviewCount = 0;
    };
    ScanStats statistics(qint64 jobId = 0) const;

    // Low-level escape hatch for the tests / advanced queries.
    QSqlDatabase database() const { return m_db; }

private:
    bool exec(const QString& sql);

    QSqlDatabase m_db;
    QString      m_connectionName;
    QString      m_dbPath;
    QString      m_lastError;
};

} // namespace dedupvault::core
