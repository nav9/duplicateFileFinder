#pragma once

#include "core/databasemanager.h"
#include "filescanner.h"

#include <QHash>
#include <QObject>
#include <QThreadPool>
#include <atomic>
#include <functional>

namespace dedupvault::engine {

/**
 * @brief Persistent, resumable task queue.
 *
 * Task rows live in SQLite (pending / running / completed / failed), so an
 * interrupted application can reconcile them on restart: tasks were stale
 * 'running' rows become 'pending' again and are dispatched again. Handlers
 * are expected to be idempotent. Status updates are marshalled back to the
 * queue's thread so the database connection is only ever used from one
 * thread.
 */
class TaskQueue : public QObject {
    Q_OBJECT

public:
    using TaskHandler = std::function<void(const core::TaskRow&)>;

    TaskQueue(core::DatabaseManager& db, ScanControl& control,
              QObject* parent = nullptr);
    ~TaskQueue() override;

    void setThreadCount(int threads);
    void registerHandler(const QString& type, const TaskHandler& handler);

    /// Records a task row (pending) for later dispatch.
    qint64 enqueue(qint64 jobId, const QString& type, qint64 targetId);

    /// Dispatch every pending task of the job onto the thread pool.
    void dispatch(qint64 jobId);

    /// True while any task is queued for dispatch or executing.
    bool hasWork() const;
    int inFlight() const { return m_inFlight.load(); }

signals:
    /// Emitted on the queue's thread after each task finishes.
    void taskCompleted(qint64 taskId, bool ok, const QString& error);
    /// Emitted when the last in-flight task of a dispatch round finishes.
    void drained();

private:
    class Runnable;

    void finishTask(qint64 taskId, bool ok, const QString& error);

    core::DatabaseManager& m_db;
    ScanControl&           m_control;
    QHash<QString, TaskHandler> m_handlers;
    QThreadPool            m_pool;
    std::atomic<int>       m_inFlight{0};
};

} // namespace dedupvault::engine