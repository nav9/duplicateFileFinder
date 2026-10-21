#include "scanengine.h"
#include "folderanalyzer.h"
#include "core/filesystemutils.h"
#include "core/logger.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QThread>

#include <utility>

namespace dedupvault::engine {

// ---------------------------------------------------------------------------

struct ScanEngine::HashRunnable : public QRunnable {
    HashRunnable(ScanEngine* engine, const core::FileRow& file)
        : m_engine(engine), m_file(file)
    {
        setAutoDelete(true);
    }

    void run() override {
        HasherOptions opts;
        HashResult result = FileHasher::hashFile(m_file, opts, m_engine->m_control);
        ScanEngine* self = m_engine;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            self->onHashResult(std::move(result));
        }, Qt::QueuedConnection);
    }

private:
    ScanEngine*  m_engine;
    core::FileRow m_file;
};

// ---------------------------------------------------------------------------

ScanEngine::ScanEngine(core::DatabaseManager& db, core::AppSettings& settings,
                       QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_settings(settings)
    , m_taskQueue(db, m_control, this)
{
    m_pool.setMaxThreadCount(qMax(2, QThread::idealThreadCount()));
    m_pumpTimer.setInterval(250);
    m_pumpTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_pumpTimer, &QTimer::timeout, this, &ScanEngine::pumpHashing);
    connect(&m_taskQueue, &TaskQueue::drained, this, &ScanEngine::onTaskDrained);

    // Traversal handler: executes on the task-queue pool. It must never touch
    // the database — results are posted back to the engine thread.
    m_taskQueue.registerHandler(QStringLiteral("traverse_root"),
                                [this](const core::TaskRow& task) {
        const qint64 rootId = task.targetId;
        const QString path = m_rootPaths.value(rootId);

        ScannerOptions opts;
        opts.protectedRoot = m_config.mainFolder;
        opts.followDirSymlinks = m_config.followDirSymlinks;
        opts.minFileSize = m_config.minFileSize;
        opts.maxFileSize = m_config.maxFileSize;
        opts.batchFileCount = 500;

        const ScanBatchSink sink = [this](ScanBatch&& batch) {
            ScanEngine* self = this;
            const qint64 rootId = batch.rootId;
            QVector<ScannedFile> files = std::move(batch.files);
            QMetaObject::invokeMethod(self,
                [self, rootId, files = std::move(files)]() mutable {
                    self->onScanBatch(rootId, std::move(files));
                }, Qt::QueuedConnection);
        };

        const TraversalResult result =
            FileScanner::scanRoot(rootId, path, opts, m_control, sink);
        core::Logger::instance().log(result.errorCount > 0 ? core::Logger::Warning : core::Logger::Info,
            QStringLiteral("Traversal task finished for root %1 (%2 files, %3 errors)")
                .arg(path).arg(result.filesFound).arg(result.errorCount));
    });
}

ScanEngine::~ScanEngine() {
    m_control.cancelled.store(true);
    m_pumpTimer.stop();
    m_pool.clear();
    m_pool.waitForDone(3000);
}

// ---------------------------------------------------------------------------

bool ScanEngine::validateConfig(QString* error) const {
    using core::FileSystemUtils;

    if (m_config.mainFolder.isEmpty()) {
        if (error) *error = tr("A main (reference) folder is required.");
        return false;
    }
    const QFileInfo mainInfo(m_config.mainFolder);
    if (!mainInfo.exists() || !mainInfo.isDir()) {
        if (error) *error = tr("Main folder does not exist: %1").arg(m_config.mainFolder);
        return false;
    }
    if (!mainInfo.isReadable()) {
        if (error) *error = tr("Main folder is not readable: %1").arg(m_config.mainFolder);
        return false;
    }
    if (m_config.candidateFolders.isEmpty() && m_config.destinationFolder.isEmpty()) {
        if (error) *error = tr("Select at least one candidate folder to consolidate.");
        return false;
    }

    QStringList all{m_config.mainFolder};
    all.append(m_config.candidateFolders);

    // Reject overlapping roots: the same directory must never be scanned
    // twice, and a candidate must not live inside the protected root.
    const auto overlaps = FileSystemUtils::findOverlappingRoots(all);
    if (!overlaps.isEmpty()) {
        if (error) {
            *error = tr("Selected folders overlap (%1 and %2). "
                        "Remove duplicates and nested selections so every root "
                        "is scanned exactly once.")
                         .arg(all[overlaps.first().first],
                              all[overlaps.first().second]);
        }
        return false;
    }

    if (!m_config.destinationFolder.isEmpty()) {
        const QString dest = m_config.destinationFolder;
        if (FileSystemUtils::isContainedIn(dest, m_config.mainFolder)) {
            if (error) *error = tr("The destination must not be inside the protected "
                                   "main folder.");
            return false;
        }
        for (const QString& cand : m_config.candidateFolders) {
            if (FileSystemUtils::isContainedIn(dest, cand)) {
                if (error) *error = tr("The destination must not be inside a candidate "
                                       "folder: %1").arg(cand);
                return false;
            }
            if (FileSystemUtils::isContainedIn(cand, dest)) {
                if (error) *error = tr("A candidate folder must not be inside the "
                                       "destination: %1").arg(cand);
                return false;
            }
        }
        const QFileInfo destInfo(dest);
        if (!destInfo.exists() && !FileSystemUtils::isWritable(destInfo.absolutePath())) {
            if (error) *error = tr("Destination parent folder is not writable: %1")
                                    .arg(destInfo.absolutePath());
            return false;
        }
    }

    if (m_config.candidateFolders.size() > 0 && m_config.destinationFolder.isEmpty()) {
        if (error) *error = tr("A destination folder is required for consolidation.");
        return false;
    }

    if (m_config.hashingThreads < 1) {
        if (error) *error = tr("Hashing thread count must be at least 1.");
        return false;
    }
    return true;
}

DetectorConfig ScanEngine::detectorConfig() const {
    return detectorConfig(m_config);
}

DetectorConfig ScanEngine::detectorConfig(const ScanJobConfig& config) {
    DetectorConfig dc;
    dc.protectedRootPath = config.mainFolder;
    dc.rootOrder.clear();
    dc.rootOrder.append(config.mainFolder);
    dc.rootOrder.append(config.candidateFolders);
    dc.byteCompareBeforeMove = config.byteCompareBeforeMove;
    dc.reportProtectedOnlyGroups = config.includeMainFolderDuplicates;
    return dc;
}

bool ScanEngine::start(const ScanJobConfig& config, bool resumeExisting) {
    if (m_running) {
        emit errorOccurred(tr("A scan is already running."));
        return false;
    }

    m_config = config;
    m_config.mainFolder =
        core::FileSystemUtils::normalizePath(QFileInfo(config.mainFolder).absoluteFilePath());
    QStringList normalizedCandidates;
    for (const QString& c : config.candidateFolders) {
        normalizedCandidates.append(
            core::FileSystemUtils::normalizePath(QFileInfo(c).absoluteFilePath()));
    }
    m_config.candidateFolders = normalizedCandidates;
    if (!m_config.destinationFolder.isEmpty()) {
        m_config.destinationFolder = core::FileSystemUtils::normalizePath(
            QFileInfo(config.destinationFolder).absoluteFilePath());
    }

    QString error;
    if (!validateConfig(&error)) {
        core::Logger::instance().warning(QStringLiteral("Scan rejected: %1").arg(error));
        emit errorOccurred(error);
        return false;
    }

    // Fresh start: clear escalation state.
    m_control.paused.store(false);
    m_control.cancelled.store(false);
    m_filesScanned.store(0);
    m_bytesScanned.store(0);
    m_dirsScanned.store(0);
    m_filesHashed.store(0);
    m_bytesHashed.store(0);
    m_errors.store(0);
    m_inFlightHash.store(0);
    m_folderCache.clear();
    m_hashSkip.clear();
    m_proposals.clear();
    m_lastMessage.clear();
    m_rootIds.clear();
    m_rootPaths.clear();

    // --- Register roots (main first: selection order is the tie-breaker) ---
    const QSet<QString> selected = [&] {
        QSet<QString> s;
        s.insert(m_config.mainFolder);
        for (const QString& c : m_config.candidateFolders) s.insert(c);
        if (!m_config.destinationFolder.isEmpty()) s.insert(m_config.destinationFolder);
        return s;
    }();

    const qint64 mainId = m_db.upsertRoot(m_config.mainFolder, core::RootRole::Main);
    if (mainId <= 0) {
        emit errorOccurred(tr("Could not register the main folder: %1")
                               .arg(m_db.lastError()));
        return false;
    }
    m_rootIds.append(mainId);
    m_rootPaths.insert(mainId, m_config.mainFolder);

    for (const QString& cand : m_config.candidateFolders) {
        const qint64 id = m_db.upsertRoot(cand, core::RootRole::Candidate);
        if (id <= 0) {
            emit errorOccurred(tr("Could not register candidate folder: %1")
                                   .arg(cand));
            return false;
        }
        m_rootIds.append(id);
        m_rootPaths.insert(id, cand);
    }
    if (!m_config.destinationFolder.isEmpty()) {
        m_db.upsertRoot(m_config.destinationFolder, core::RootRole::Destination);
    }

    // Scope the catalog to this job's selection: roots the user dropped are
    // removed (files/folders cascade), so results never mix stale scopes.
    for (const core::RootRecord& r : m_db.roots(false)) {
        if (!selected.contains(r.path)) {
            m_db.removeRoot(r.id);
            core::Logger::instance().info(
                QStringLiteral("Removed deselected root from catalog: %1").arg(r.path));
        }
    }

    // Root folder rows ("" = the root directory itself).
    for (const qint64 rootId : std::as_const(m_rootIds)) {
        m_db.upsertFolder(rootId, 0, QString());
    }

    // --- Job: resume an interrupted one when asked --------------------------
    qint64 jobId = -1;
    bool resumed = false;
    if (resumeExisting) {
        if (const auto job = m_db.latestResumableJob()) {
            jobId = job->jobId;
            resumed = true;
            core::Logger::instance().info(
                QStringLiteral("Resuming job %1 (phase was %2)")
                    .arg(jobId).arg(job->phase));
        } else {
            core::Logger::instance().info("No resumable job found; starting a new one.");
        }
    }
    if (jobId <= 0) {
        jobId = m_db.createJob();
        resumed = false;
    } else {
        m_db.updateJobStatus(jobId, QStringLiteral("running"), Phase::Traversal);
    }
    m_jobId = jobId;

    // Reconcile tasks that were mid-flight when the app died.
    const int recovered = m_db.recoverInterruptedTasks();
    if (recovered > 0) {
        core::Logger::instance().info(
            QStringLiteral("Recovered %1 interrupted task(s) from a previous run.")
                .arg(recovered));
    }

    // --- Start -------------------------------------------------------------
    m_running = true;
    m_pool.setMaxThreadCount(qMax(2, m_config.hashingThreads));
    m_taskQueue.setThreadCount(qMax(1, qMin(static_cast<int>(m_rootIds.size()),
                                            QThread::idealThreadCount())));

    setPhase(Phase::Traversal);
    if (resumed) {
        // Requeue the job's existing traversal tasks (idempotent re-walk of
        // every root revalidates metadata). If the folder selection changed
        // since the pause, the task set no longer matches the scope: start a
        // clean job instead of mixing two catalog scopes.
        const int taskCount =
            m_db.resetTasks(m_jobId, QStringLiteral("traverse_root"));
        if (taskCount != m_rootIds.size()) {
            core::Logger::instance().info(
                QStringLiteral("Folder selection changed since the paused job; "
                               "starting a new job for the new scope."));
            resumed = false;
            m_jobId = m_db.createJob();
        }
    }
    if (!resumed) {
        for (const qint64 rootId : std::as_const(m_rootIds)) {
            m_taskQueue.enqueue(m_jobId, QStringLiteral("traverse_root"), rootId);
        }
    }
    m_taskQueue.dispatch(m_jobId);
    return true;
}

void ScanEngine::pause() {
    if (!m_running || m_control.paused.load()) return;
    m_control.paused.store(true);
    m_lastMessage = tr("Paused");
    m_db.updateJobStatus(m_jobId, QStringLiteral("paused"), m_phase);
    core::Logger::instance().info(QStringLiteral("Job %1 paused.").arg(m_jobId));
    emit phaseChanged(m_phase);
}

void ScanEngine::resume() {
    if (!m_running || !m_control.paused.load()) return;
    m_control.paused.store(false);
    m_lastMessage.clear();
    m_db.updateJobStatus(m_jobId, QStringLiteral("running"), m_phase);
    core::Logger::instance().info(QStringLiteral("Job %1 resumed.").arg(m_jobId));
    if (m_phase == Phase::Hashing) {
        QTimer::singleShot(0, this, &ScanEngine::pumpHashing);
    }
    emit phaseChanged(m_phase);
}

void ScanEngine::stopForShutdown() {
    if (!m_running) return;

    // Stop workers, keep the catalog and leave the job resumable.
    m_control.cancelled.store(true);
    m_control.paused.store(false);
    m_pumpTimer.stop();
    m_pool.clear();
    m_pool.waitForDone(5000);

    m_running = false;
    m_db.updateJobStatus(m_jobId, QStringLiteral("paused"), m_phase);
    m_lastMessage = tr("Scan paused for shutdown \u2014 resume it from the "
                       "dashboard next time.");
    core::Logger::instance().info(
        QStringLiteral("Job %1 paused for shutdown at phase %2.")
            .arg(m_jobId).arg(m_phase));
}

void ScanEngine::cancel() {
    if (!m_running) return;
    m_control.cancelled.store(true);
    m_lastMessage = tr("Cancelling\u2026");
    core::Logger::instance().info(QStringLiteral("Job %1 cancellation requested.")
                                .arg(m_jobId));
    if (m_inFlightHash.load() == 0 && !m_taskQueue.hasWork()) {
        finishJob(true, tr("Cancelled by user."));
    }
}

// ---------------------------------------------------------------------------

qint64 ScanEngine::folderIdFor(qint64 rootId, const QString& relDir) {
    const QPair<qint64, QString> key{rootId, relDir};
    if (const auto it = m_folderCache.constFind(key); it != m_folderCache.cend()) {
        return it.value();
    }
    qint64 parentId = 0;
    if (!relDir.isEmpty()) {
        const int idx = qMax(relDir.lastIndexOf(QLatin1Char('/')),
                              relDir.lastIndexOf(QLatin1Char('\\')));
        const QString parentRel = idx < 0 ? QString() : relDir.left(idx);
        parentId = folderIdFor(rootId, parentRel);
    }
    const qint64 id = m_db.upsertFolder(rootId, parentId, relDir);
    m_folderCache.insert(key, id);
    return id;
}

void ScanEngine::commitBatch(qint64 rootId, const QVector<ScannedFile>& files,
                             const QVector<QString>& dirs) {
    Q_UNUSED(dirs); // folder rows are derived from file paths on demand

    if (files.isEmpty()) return;

    QVector<core::FileRecord> records;
    records.reserve(files.size());
    for (const ScannedFile& f : files) {
        core::FileRecord rec;
        rec.rootId = rootId;
        rec.folderId = folderIdFor(rootId, f.relDir);
        rec.relPath = f.relPath;
        rec.size = f.size;
        rec.mtimeMsec = f.mtimeMsec;
        rec.deviceId = f.deviceId;
        rec.inodeId = f.inodeId;
        rec.generation = m_jobId;
        records.append(std::move(rec));
    }

    if (!m_db.upsertFileMetadata(records)) {
        const QString err = tr("Failed to persist discovered files: %1")
                                .arg(m_db.lastError());
        m_errors.fetch_add(1);
        core::Logger::instance().error(err);
        emit errorOccurred(err);
    }

    qint64 bytes = 0;
    for (const ScannedFile& f : files) bytes += f.size;
    m_filesScanned.fetch_add(static_cast<qint64>(files.size()));
    m_bytesScanned.fetch_add(bytes);
}

void ScanEngine::onScanBatch(qint64 rootId, QVector<ScannedFile> files) {
    commitBatch(rootId, files, {});
}

void ScanEngine::onHashResult(HashResult result) {
    m_inFlightHash.fetch_sub(1);

    if (result.ok) {
        if (!m_db.updateFileHash(result.fileId, result.sha256)) {
            core::Logger::instance().warning(
                tr("Could not persist hash for file %1: %2")
                    .arg(result.fileId).arg(m_db.lastError()));
        }
        m_filesHashed.fetch_add(1);
        m_bytesHashed.fetch_add(result.bytesHashed);
    } else if (result.changed) {
        // The file was modified while being hashed: persist fresh metadata,
        // invalidate the hash and skip it for the rest of this job.
        m_db.resetFileAfterChange(result.fileId, result.freshSize, result.freshMtimeMsec,
                                  result.freshDeviceId, result.freshInodeId);
        m_hashSkip.insert(result.fileId);
        m_errors.fetch_add(1);
        core::Logger::instance().warning(
            tr("File changed during hashing, deferred to next scan: %1")
                .arg(result.absPath));
    } else if (result.error == QLatin1String("cancelled")) {
        // Reported through the job status, not as a file error.
    } else {
        m_db.markFileError(result.fileId, result.error);
        m_errors.fetch_add(1);
        core::Logger::instance().warning(
            tr("Hashing failed for %1: %2").arg(result.absPath, result.error));
    }

    if (m_inFlightHash.load() <= 0) {
        if (m_control.cancelled.load()) {
            finishJob(true, tr("Cancelled by user."));
            return;
        }
        if (m_phase == Phase::Hashing && !m_control.paused.load()) {
            QTimer::singleShot(0, this, &ScanEngine::pumpHashing);
        }
    }
}

void ScanEngine::pumpHashing() {
    if (!m_running || m_phase != Phase::Hashing) return;
    if (m_control.cancelled.load()) {
        if (m_inFlightHash.load() == 0) finishJob(true, tr("Cancelled by user."));
        return;
    }
    if (m_control.paused.load()) return;
    if (m_inFlightHash.load() > 0) return;

    const QVector<core::FileRow> batch =
        m_db.filesNeedingHash(1024, !m_config.completeHashing);

    QVector<core::FileRow> todo;
    todo.reserve(batch.size());
    for (const core::FileRow& f : batch) {
        if (!m_hashSkip.contains(f.fileId)) todo.append(f);
    }

    if (todo.isEmpty()) {
        finishPhaseHashing();
        return;
    }

    m_inFlightHash.store(static_cast<int>(todo.size()));
    for (const core::FileRow& f : std::as_const(todo)) {
        m_pool.start(new HashRunnable(this, f));
    }
}

void ScanEngine::onTaskDrained() {
    if (!m_running || m_phase != Phase::Traversal) return;
    finishPhaseTraversal();
}

void ScanEngine::finishPhaseTraversal() {
    if (m_control.cancelled.load()) {
        finishJob(true, tr("Cancelled during traversal."));
        return;
    }

    // Commit-point cleanup: files no longer present on disk (renamed,
    // deleted while we ran) disappear from the catalog — the scan does not
    // depend on a single traversal cursor to converge.
    for (const qint64 rootId : std::as_const(m_rootIds)) {
        const int stale = m_db.markStaleFiles(rootId, m_jobId);
        if (stale > 0) {
            core::Logger::instance().info(
                tr("%1 file(s) no longer present under root %2 were removed "
                   "from the catalog.").arg(stale).arg(rootId));
        }
    }

    setPhase(Phase::Hashing);
    m_pumpTimer.start();
    QTimer::singleShot(0, this, &ScanEngine::pumpHashing);
}

void ScanEngine::finishPhaseHashing() {
    if (m_control.cancelled.load()) {
        finishJob(true, tr("Cancelled during hashing."));
        return;
    }
    m_pumpTimer.stop();

    setPhase(Phase::Grouping);
    m_proposals = DuplicateDetector::buildProposals(m_db, detectorConfig());
    emit proposalsReady();

    finishPhaseGrouping();
}

void ScanEngine::finishPhaseGrouping() {
    if (m_control.cancelled.load()) {
        finishJob(true, tr("Cancelled during grouping."));
        return;
    }

    setPhase(Phase::Analysis);
    if (m_config.analyzeFolders) {
        FolderAnalyzer::computeFolderStats(m_db);
        if (!m_control.cancelled.load()) {
            FolderAnalyzer::compareFolders(m_db, m_config.folderOverlapFloor);
        }
    }
    finishPhaseAnalysis();
}

void ScanEngine::finishPhaseAnalysis() {
    if (m_control.cancelled.load()) {
        finishJob(true, tr("Cancelled during analysis."));
        return;
    }
    const qint64 groups = static_cast<qint64>(m_proposals.size());
    finishJob(false, m_config.includeMainFolderDuplicates
        ? tr("Scan complete: %1 duplicate group(s) (including the main "
             "folder).").arg(groups)
        : tr("Scan complete: %1 duplicate group(s) with actionable "
             "copies.").arg(groups));
}

void ScanEngine::finishJob(bool cancelled, const QString& message) {
    if (!m_running) return;
    m_running = false;

    m_pumpTimer.stop();
    m_lastMessage = message;

    m_db.finishJob(m_jobId, cancelled ? QStringLiteral("cancelled")
                                      : QStringLiteral("completed"));
    setPhase(Phase::Done);
    core::Logger::instance().info(
        QStringLiteral("Job %1 finished (%2): %3")
            .arg(m_jobId)
            .arg(cancelled ? QStringLiteral("cancelled") : QStringLiteral("completed"))
            .arg(message));

    emit jobFinished(m_jobId, cancelled, message);
}

// ---------------------------------------------------------------------------

void ScanEngine::setPhase(const QString& phase) {
    m_phase = phase;
    if (m_running) {
        const QString status = m_control.paused.load() ? QStringLiteral("paused")
                                                       : QStringLiteral("running");
        m_db.updateJobStatus(m_jobId, status, phase);
    }
    core::Logger::instance().info(QStringLiteral("Phase: %1").arg(phase));
    emit phaseChanged(phase);
}

EngineProgress ScanEngine::progress() const {
    EngineProgress p;
    p.phase = m_phase;
    p.running = m_running;
    p.paused = m_control.paused.load();
    p.cancelled = m_control.cancelled.load();
    p.jobId = m_jobId;
    p.filesScanned = m_filesScanned.load();
    p.bytesScanned = m_bytesScanned.load();
    p.dirsScanned = m_dirsScanned.load();
    p.filesHashed = m_filesHashed.load();
    p.bytesHashed = m_bytesHashed.load();
    p.inFlightHashes = m_inFlightHash.load();
    p.hashQueueDepth = p.inFlightHashes;
    p.traversalActive = m_taskQueue.inFlight();
    p.errors = m_errors.load();
    p.lastMessage = m_lastMessage;
    if (m_jobId > 0) {
        p.dbStats = m_db.statistics(m_jobId);
        p.hashQueueDepth += p.dbStats.fileCount - p.dbStats.hashedCount;
        if (p.hashQueueDepth < 0) p.hashQueueDepth = 0;
    }
    return p;
}

QVector<DuplicateProposal> ScanEngine::takeProposals() {
    return m_proposals;
}

QVector<DuplicateProposal> ScanEngine::reloadProposals(const ScanJobConfig& config) {
    m_proposals = DuplicateDetector::buildProposals(m_db, detectorConfig(config));
    return m_proposals;
}

QVector<core::RootRecord> ScanEngine::registeredRoots() const {
    return m_db.roots(false);
}

std::optional<qint64> ScanEngine::resumableJob() const {
    if (const auto job = m_db.latestResumableJob()) return job->jobId;
    return std::nullopt;
}

} // namespace dedupvault::engine