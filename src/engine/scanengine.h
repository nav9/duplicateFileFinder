#pragma once

#include "core/databasemanager.h"
#include "core/settings.h"
#include "filescanner.h"
#include "filehasher.h"
#include "duplicatedetector.h"
#include "taskqueue.h"

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QSet>
#include <QVector>
#include <atomic>

namespace dedupvault::engine {

/**
 * @brief Configuration of one deduplication job.
 *
 * Everything the user chose on the scan screens: folder roles, scanning
 * behaviour and tunables that are also exposed in Settings.
 */
struct ScanJobConfig {
    QString   mainFolder;
    QStringList candidateFolders;
    QString   destinationFolder;

    bool      dryRun = true;            // preview by default; user opts in
    bool      completeHashing = false;  // hash everything, even unique sizes
    // Also report duplicate groups that live entirely inside the main folder.
    // The main folder is protected, so such groups are shown for review only.
    bool      includeMainFolderDuplicates = false;
    bool      byteCompareBeforeMove = true;
    bool      followDirSymlinks = false;
    qint64    minFileSize = 0;
    qint64    maxFileSize = -1;
    int       hashingThreads = 4;
    double    folderOverlapFloor = 0.34;
    bool      analyzeFolders = true;
};

/**
 * @brief Phase names surfaced to the UI.
 */
namespace Phase {
inline const QString Idle        = QStringLiteral("idle");
inline const QString Traversal   = QStringLiteral("traversal");
inline const QString Hashing     = QStringLiteral("hashing");
inline const QString Grouping    = QStringLiteral("grouping");
inline const QString Analysis    = QStringLiteral("analysis");
inline const QString Planning    = QStringLiteral("planning");
inline const QString Done        = QStringLiteral("done");
}

/**
 * @brief Immutable snapshot of job progress for UI polling.
 *
 * The engine owns all counters as atomics; the UI reads them on a timer,
 * which keeps worker threads free of signal fan-out.
 */
struct EngineProgress {
    QString phase = Phase::Idle;
    bool    running = false;
    bool    paused = false;
    bool    cancelled = false;
    qint64  jobId = 0;

    qint64  filesScanned = 0;
    qint64  bytesScanned = 0;
    qint64  dirsScanned = 0;
    qint64  filesHashed = 0;
    qint64  bytesHashed = 0;
    qint64  hashQueueDepth = 0;
    int     inFlightHashes = 0;
    int     traversalActive = 0;
    int     errors = 0;

    core::DatabaseManager::ScanStats dbStats;
    QString lastMessage;
};

/**
 * @brief Orchestrates one deduplication job across worker threads.
 *
 * Pipeline: traversal -> size-bucketed SHA-256 hashing -> grouping and
 * keeper selection -> folder/subtree analysis -> action planning.
 *
 * Threading: workers (traversal, hashing) never touch SQLite. They post
 * batches/results to the engine thread with queued invocations; the engine
 * thread commits them in transactions, making every phase checkpointed,
 * resumable and safe against cancellation.
 */
class ScanEngine : public QObject {
    Q_OBJECT

public:
    ScanEngine(core::DatabaseManager& db, core::AppSettings& settings,
               QObject* parent = nullptr);
    ~ScanEngine() override;

    /// Validate + register roots, create (or resume) the job, start workers.
    bool start(const ScanJobConfig& config, bool resumeExisting = false);
    void pause();
    void resume();
    void cancel();

    /// Stop all workers for application shutdown while keeping the job
    /// resumable (the job is left in the persisted 'paused' state).
    void stopForShutdown();

    bool isRunning() const { return m_running; }
    bool isPaused() const  { return m_control.paused.load(); }
    bool isCancelled() const { return m_control.cancelled.load(); }
    qint64 currentJobId() const { return m_jobId; }
    ScanJobConfig config() const { return m_config; }

    /// Id of a job that was interrupted by a previous crash/exit, if any.
    std::optional<qint64> resumableJob() const;

    EngineProgress progress() const;
    QVector<DuplicateProposal> takeProposals();

    /// (Re)build proposals from persisted groups without running a scan.
    /// The config is supplied by the UI so the protected root and reporting
    /// options are correct even before a job has been started this session.
    QVector<DuplicateProposal> reloadProposals(const ScanJobConfig& config);

    /// Roots registered before the current session (for resume/review).
    QVector<core::RootRecord> registeredRoots() const;

signals:
    void phaseChanged(const QString& phase);
    void jobFinished(qint64 jobId, bool cancelled, const QString& message);
    void errorOccurred(const QString& message);
    void proposalsReady();

private:
    /// Batch of files discovered by a worker (queued onto the engine thread).
    void onScanBatch(qint64 rootId, QVector<ScannedFile> files);
    /// A worker finished hashing one file.
    void onHashResult(HashResult result);
    void onTaskDrained();
    void pumpHashing();
    void finishPhaseTraversal();
    void finishPhaseHashing();
    void finishPhaseGrouping();
    void finishPhaseAnalysis();
    void finishJob(bool cancelled, const QString& message);

private:
    struct HashRunnable;

    void        setPhase(const QString& phase);
    qint64      folderIdFor(qint64 rootId, const QString& relDir);
    void        commitBatch(qint64 rootId, const QVector<ScannedFile>& files,
                            const QVector<QString>& dirs);
    bool        validateConfig(QString* error) const;
    DetectorConfig detectorConfig() const;
    static DetectorConfig detectorConfig(const ScanJobConfig& config);

    core::DatabaseManager& m_db;
    core::AppSettings&     m_settings;

    ScanControl     m_control;
    ScanJobConfig   m_config;
    TaskQueue       m_taskQueue;
    QThreadPool     m_pool;
    QTimer          m_pumpTimer;

    bool            m_running = false;
    qint64          m_jobId = 0;

    std::atomic<qint64> m_filesScanned{0};
    std::atomic<qint64> m_bytesScanned{0};
    std::atomic<qint64> m_dirsScanned{0};
    std::atomic<qint64> m_filesHashed{0};
    std::atomic<qint64> m_bytesHashed{0};
    std::atomic<int>    m_errors{0};
    std::atomic<int>    m_inFlightHash{0};

    QString                     m_phase = Phase::Idle;
    QString                     m_lastMessage;
    QVector<DuplicateProposal>  m_proposals;

    // Engine-thread-only state.
    QHash<QPair<qint64, QString>, qint64> m_folderCache;
    QSet<qint64>                m_hashSkip;   // changed while hashing
    QVector<qint64>             m_rootIds;    // roots scanned by this job
    QHash<qint64, QString>      m_rootPaths;  // id -> absolute path (read-only)
};

} // namespace dedupvault::engine