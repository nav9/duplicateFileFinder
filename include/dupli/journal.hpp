#pragma once

#include "dupli/result.hpp"
#include <string>
#include <vector>
#include <filesystem>

namespace dupli {

class IJournal {
public:
    virtual ~IJournal() = default;
    virtual std::string start_action(const std::string& action_id, 
                                      const std::filesystem::path& source,
                                      const std::filesystem::path& dest) = 0;
    virtual Result<void> complete_action(const std::string& journal_id) = 0;
    virtual Result<void> fail_action(const std::string& journal_id, const std::string& error) = 0;
    virtual Result<std::vector<std::string>> recover_pending() = 0;
};

class SimpleJournal : public IJournal {
public:
    SimpleJournal() = default;
    
    std::string start_action(const std::string& action_id, 
                              const std::filesystem::path& source,
                              const std::filesystem::path& dest) override {
        std::string id = "j_" + std::to_string(m_counter++);
        Entry e{id, action_id, source, dest, "started", ""};
        m_entries.push_back(e);
        return id;
    }
    
    Result<void> complete_action(const std::string& journal_id) override {
        for (auto& e : m_entries) {
            if (e.id == journal_id) {
                e.state = "completed";
                return Result<void>();
            }
        }
        return Error{ErrorCode::NotFound, "Journal entry not found", "", "complete_action"};
    }
    
    Result<void> fail_action(const std::string& journal_id, const std::string& error) override {
        for (auto& e : m_entries) {
            if (e.id == journal_id) {
                e.state = "failed";
                e.error = error;
                return Result<void>();
            }
        }
        return Error{ErrorCode::NotFound, "Journal entry not found", "", "fail_action"};
    }
    
    Result<std::vector<std::string>> recover_pending() override {
        std::vector<std::string> pending;
        for (const auto& e : m_entries) {
            if (e.state == "started") {
                pending.push_back(e.id);
            }
        }
        return pending;
    }
    
private:
    struct Entry {
        std::string id;
        std::string action_id;
        std::filesystem::path source;
        std::filesystem::path dest;
        std::string state;
        std::string error;
    };
    std::vector<Entry> m_entries;
    int m_counter = 0;
};

} // namespace dupli
