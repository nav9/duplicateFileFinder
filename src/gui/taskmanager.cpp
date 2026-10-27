#include "dupli/gui/taskmanager.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <thread>

namespace dupli {
namespace gui {

TaskManager::TaskManager() {
}

TaskManager::~TaskManager() {
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    for (auto& [id, task_data] : m_tasks) {
        if (task_data->info.state == TaskState::Running || 
            task_data->info.state == TaskState::Queued) {
            task_data->cancelled = true;
        }
    }
}

std::string TaskManager::generate_task_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    for (int i = 0; i < 16; ++i) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

bool TaskManager::is_tab_busy(const std::string& tab_name) {
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    auto it = m_tab_current_task.find(tab_name);
    if (it == m_tab_current_task.end()) return false;
    
    auto task_it = m_tasks.find(it->second);
    if (task_it == m_tasks.end()) return false;
    
    std::lock_guard<std::mutex> task_lock(task_it->second->mutex);
    return task_it->second->info.state == TaskState::Running || 
           task_it->second->info.state == TaskState::Queued;
}

std::string TaskManager::start_task(const std::string& tab_name,
                                     const std::string& operation_name,
                                     const std::string& display_name,
                                     std::function<void(std::atomic<bool>&, std::function<void(double,const std::string&)>)> task_func) {
    if (is_tab_busy(tab_name)) {
        return ""; // Can't start another task on busy tab
    }
    
    std::string task_id = generate_task_id();
    
    auto task_data = std::make_shared<TaskData>();
    task_data->cancelled = false;
    task_data->info.id = task_id;
    task_data->info.tab_name = tab_name;
    task_data->info.operation_name = operation_name;
    task_data->info.display_name = display_name;
    task_data->info.start_time = std::chrono::system_clock::now();
    task_data->info.state = TaskState::Queued;
    task_data->info.progress = 0.0;
    task_data->info.status_message = "Queued";
    task_data->info.can_undo = false;
    
    {
        std::lock_guard<std::mutex> lock(m_tasks_mutex);
        m_tasks[task_id] = task_data;
        m_tab_current_task[tab_name] = task_id;
    }
    
    task_data->worker_thread = std::thread([this, task_id, task_data, task_func]() {
        {
            std::lock_guard<std::mutex> lock(task_data->mutex);
            task_data->info.state = TaskState::Running;
            task_data->info.status_message = "Running";
        }
        
        auto progress_cb = [task_data](double progress, const std::string& message) {
            std::lock_guard<std::mutex> lock(task_data->mutex);
            if (task_data->info.state == TaskState::Running) {
                task_data->info.progress = progress;
                task_data->info.status_message = message;
            }
        };
        
        try {
            if (task_data->cancelled) {
                std::lock_guard<std::mutex> lock(task_data->mutex);
                task_data->info.state = TaskState::Cancelled;
                task_data->info.status_message = "Cancelled";
                return;
            }
            
            task_func(task_data->cancelled, progress_cb);
            
            std::lock_guard<std::mutex> lock(task_data->mutex);
            if (task_data->cancelled) {
                task_data->info.state = TaskState::Cancelled;
                task_data->info.status_message = "Cancelled";
            } else {
                task_data->info.state = TaskState::Completed;
                task_data->info.status_message = "Completed";
                task_data->info.progress = 1.0;
            }
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(task_data->mutex);
            task_data->info.state = TaskState::Failed;
            task_data->info.status_message = "Failed";
            task_data->info.error_message = e.what();
        }
    });
    
    task_data->worker_thread.detach();
    
    return task_id;
}

bool TaskManager::cancel_task(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    auto it = m_tasks.find(task_id);
    if (it == m_tasks.end()) return false;
    
    auto task_data = it->second;
    std::lock_guard<std::mutex> task_lock(task_data->mutex);
    
    if (task_data->info.state == TaskState::Running || 
        task_data->info.state == TaskState::Queued) {
        task_data->cancelled = true;
        task_data->info.state = TaskState::CancellationRequested;
        task_data->info.status_message = "Cancellation requested";
        return true;
    }
    return false;
}

TaskInfo TaskManager::get_task_info(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    auto it = m_tasks.find(task_id);
    if (it == m_tasks.end()) {
        TaskInfo empty;
        empty.id = task_id;
        empty.state = TaskState::Failed;
        empty.status_message = "Task not found";
        return empty;
    }
    std::lock_guard<std::mutex> task_lock(it->second->mutex);
    return it->second->info;
}

std::vector<TaskInfo> TaskManager::get_all_tasks() {
    std::vector<TaskInfo> result;
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    for (auto& [id, task_data] : m_tasks) {
        std::lock_guard<std::mutex> task_lock(task_data->mutex);
        result.push_back(task_data->info);
    }
    return result;
}

std::vector<TaskInfo> TaskManager::get_tasks_for_tab(const std::string& tab_name) {
    std::vector<TaskInfo> result;
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    for (auto& [id, task_data] : m_tasks) {
        std::lock_guard<std::mutex> task_lock(task_data->mutex);
        if (task_data->info.tab_name == tab_name) {
            result.push_back(task_data->info);
        }
    }
    return result;
}

} // namespace gui
} // namespace dupli
