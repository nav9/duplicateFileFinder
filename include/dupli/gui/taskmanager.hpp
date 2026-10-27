#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>
#include <map>
#include <functional>
#include <thread>

namespace dupli {
namespace gui {

// Task states as specified in GUI instructions
enum class TaskState {
    Queued,
    Running,
    CancellationRequested,
    Cancelled,
    Completed,
    PartiallyCompleted,
    Failed,
    NeedsReview,
    RecoveryRequired
};

// Task information with immutable snapshot
struct TaskInfo {
    std::string id;
    std::string tab_name;
    std::string operation_name;
    std::string display_name;
    std::chrono::system_clock::time_point start_time;
    TaskState state;
    double progress;
    std::string status_message;
    std::string result_summary;
    std::string error_message;
    bool can_undo;
    std::string undo_reason;
};

struct TaskSettingsSnapshot {
    std::string concurrency_limit;
    bool hidden_entries;
    bool follow_filesystem_boundaries;
    size_t hash_buffer_size;
};

// Modified for single-task-per-tab (no concurrency needed)
class TaskManager {
public:
    TaskManager();
    ~TaskManager();
    
    std::string start_task(const std::string& tab_name, 
                          const std::string& operation_name,
                          const std::string& display_name,
                          std::function<void(std::atomic<bool>&, std::function<void(double,const std::string&)>)> task_func);
    
    bool cancel_task(const std::string& task_id);
    TaskInfo get_task_info(const std::string& task_id);
    std::vector<TaskInfo> get_all_tasks();
    std::vector<TaskInfo> get_tasks_for_tab(const std::string& tab_name);
    bool is_tab_busy(const std::string& tab_name);
    
private:
    struct TaskData {
        TaskInfo info;
        std::atomic<bool> cancelled;
        std::thread worker_thread;
        std::mutex mutex;
    };
    
    std::map<std::string, std::shared_ptr<TaskData>> m_tasks;
    std::mutex m_tasks_mutex;
    std::map<std::string, std::string> m_tab_current_task;
    
    std::string generate_task_id();
};

} // namespace gui
} // namespace dupli
