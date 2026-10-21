#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <functional>
#include <cstdint>
#include <thread>
#include <chrono>

namespace dedupvault::engine {

/**
 * @brief Shared control flags for any background unit of work.
 *
 * Workers poll checkpoint() frequently; it sleeps while paused and
 * returns false the moment cancellation is requested.
 */
struct ScanControl {
    std::atomic<bool> paused{false};
    std::atomic<bool> cancelled{false};

    /// @return true when it is OK to continue, false when cancelled.
    bool checkpoint() {
        if (cancelled.load(std::memory_order_relaxed)) return false;
        while (paused.load(std::memory_order_relaxed)) {
            if (cancelled.load(std::memory_order_relaxed)) return false;
            // Sleep without burning CPU; 50ms keeps pause latency snappy.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return !cancelled.load(std::memory_order_relaxed);
    }
};

/**
 * @brief A file discovered during traversal (pure data, no DB access).
 */
struct ScannedFile {
    QString relPath;   // path relative to the root, native separators
    QString relDir;    // containing directory relative to root ("" = root)
    qint64  size = 0;
    qint64  mtimeMsec = 0;
    quint64 deviceId = 0;
    quint64 inodeId = 0;
};

/**
 * @brief A batch of discovered files handed back to the engine thread.
 */
struct ScanBatch {
    qint64 rootId = 0;
    QVector<ScannedFile> files;
};

/**
 * @brief Result of a full traversal of one root.
 */
struct TraversalResult {
    qint64  rootId = 0;
    QString rootPath;
    bool    cancelled = false;
    qint64  filesFound = 0;
    qint64  bytesFound = 0;
    qint64  dirsVisited = 0;
    int     errorCount = 0;
    QStringList errors; // bounded sample of error strings for the log
};

struct ScannerOptions {
    QString protectedRoot;       // main/reference root (never bypassed)
    bool    followDirSymlinks = false;  // default policy: do not follow
    bool    skipFileSymlinks = true;    // record nothing for file symlinks
    qint64  minFileSize = 0;
    qint64  maxFileSize = -1;    // -1 = unlimited
    int     batchFileCount = 1000;
    int     maxErrorSamples = 50;
};

/// Called from worker threads; delivers batches to the engine thread.
using ScanBatchSink = std::function<void(ScanBatch&&)>;

/**
 * @brief Filesystem traversal module (metadata collection stage).
 *
 * Deliberately free of Qt SQL / UI dependencies: it only walks the tree,
 * collects metadata and pushes batches through the sink so the same
 * traversal can be reused by other pipelines (e.g. perceptual matching).
 */
class FileScanner {
public:
    FileScanner() = delete;

    static TraversalResult scanRoot(qint64 rootId,
                                    const QString& rootPath,
                                    const ScannerOptions& options,
                                    ScanControl& control,
                                    const ScanBatchSink& sink);
};

} // namespace dedupvault::engine
