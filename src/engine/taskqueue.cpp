#include "taskqueue.h"
#include "core/logger.h"

#include <QMetaObject>
#include <QRunnable>
#include <QThread>

namespace dedupvault::engine {

/**
 * @brief Worker unit: runs one handler and reports back on the queue thread.
 */
class TaskQueue::Runnable : public QRunnable {
public:
    Runnable(TaskQueue* queue, core::TaskRow task, TaskHandler handler,
             std::atomic<bool>* cancelledFlag)
        : m_queue(queue)
        , m_task(std::move(task))
        , m_handler(std::move(handler))
        , m_cancelled(cancelledFlag)
    {
        setAutoDelete(true);
    }

    void run() override {
        bool ok = true;
        QString error;
        if (m_cancelled && m_cancelled->load(std::memory_order_relaxed)) {
            ok = false;
            error = QStringLiteral("cancelled");
        } else if (m_handler) {
            try {
                m_handler(m_task);
            } catch (const std::exception& ex) {
                ok = false;
                error = QString::fromUtf8(ex.what());
            } catch (...) {
                ok = false;
                error = QStringLiteral("unknown exception in task handler");
            }
        } else {
            ok = false;
            error = QStringLiteral("no handler registered for task type: ") + m_task.taskType;
        }

        // Deliver completion on the queue thread: the database connection and
        // all signals must only be touched from the owning thread.
        TaskQueue* queue = m_queue;
        const qint64 taskId = m_task.taskId;
        QMetaObject::invokeMethod(queue, [queue, taskId, ok, error]() {
            queue->finishTask(taskId, ok, error);
        }, Qt::QueuedConnection);
    }

private:
    TaskQueue*    m_queue;
    core::TaskRow  m_task;
    TaskHandler    m_handler;
    std::atomic<bool>* m_cancelled;
};

TaskQueue::TaskQueue(core::DatabaseManager& db, ScanControl& control, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_control(control)
{
    m_pool.setMaxThreadCount(qMax(1, QThread::idealThreadCount() / 2));
}

TaskQueue::~TaskQueue() {
    m_pool.clear();
    m_pool.waitForDone(5000);
}

void TaskQueue::setThreadCount(int threads) {
    m_pool.setMaxThreadCount(qMax(1, threads));
}

void TaskQueue::registerHandler(const QString& type, const TaskHandler& handler) {
    m_handlers.insert(type, handler);
}

qint64 TaskQueue::enqueue(qint64 jobId, const QString& type, qint64 targetId) {
    // addTask() inserts the row directly in 'pending' state; dispatch()
    // flips it to running when the pool picks it up.
    return m_db.addTask(jobId, type, targetId);
}

void TaskQueue::dispatch(qint64 jobId) {
    const QVector<core::TaskRow> pending = m_db.tasksByJob(jobId);
    for (const core::TaskRow& task : pending) {
        if (task.status != QLatin1String("pending")) continue;
        m_db.updateTaskStatus(task.taskId, QStringLiteral("running"));

        auto* r = new Runnable(this, task, m_handlers.value(task.taskType),
                               &m_control.cancelled);
        ++m_inFlight;
        m_pool.start(r);
    }
}

bool TaskQueue::hasWork() const {
    return m_inFlight.load() > 0;
}

void TaskQueue::finishTask(qint64 taskId, bool ok, const QString& error) {
    m_db.updateTaskStatus(taskId,
                          ok ? QStringLiteral("completed") : QStringLiteral("failed"),
                          error);
    const int remaining = --m_inFlight;
    emit taskCompleted(taskId, ok, error);
    if (remaining == 0) {
        emit drained();
    }
}

} // namespace dedupvault::engine