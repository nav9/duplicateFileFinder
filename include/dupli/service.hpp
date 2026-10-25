#pragma once

#include "dupli/scanner.hpp"
#include "dupli/classifier.hpp"
#include "dupli/action_planner.hpp"
#include "dupli/validator.hpp"
#include "dupli/executor.hpp"
#include "dupli/filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/result.hpp"

namespace dupli {

struct ScanOptions {
    std::vector<std::filesystem::path> roots;
    bool follow_symlinks = false;
};

struct ScanResult {
    std::vector<ScannedEntry> entries;
};

struct ClassificationResult {
    std::vector<Relationship> relationships;
};

struct PlanRequest {
    std::vector<std::filesystem::path> paths;
    ActionMode mode = ActionMode::PreviewOnly;
};

struct ApprovalSet {
    std::vector<std::string> approved_action_ids;
};

class DuplicateManagerService {
public:
    DuplicateManagerService(IFileSystem& fs, IHasher& hasher, IJournal& journal)
        : m_fs(fs), m_hasher(hasher), m_journal(journal),
          m_scanner(fs), m_classifier(fs, hasher) {}
    
    Result<ScanResult> scan(const ScanOptions& options) {
        ScanResult result;
        auto entries = m_scanner.scan(options.roots, options.follow_symlinks);
        if (!entries.ok()) return entries.error();
        result.entries = entries.value();
        return result;
    }
    
    Result<ClassificationResult> classify(const ScanResult& scan_result) {
        ClassificationResult result;
        std::vector<std::filesystem::path> paths;
        for (const auto& e : scan_result.entries) {
            if (e.is_file) paths.push_back(e.path);
        }
        auto rels = m_classifier.classify(paths);
        if (!rels.ok()) return rels.error();
        result.relationships = rels.value();
        return result;
    }
    
    Result<ActionPlanner::ActionPlan> create_plan(const PlanRequest& request) {
        ActionPlanner planner;
        return planner.create_plan(request.mode, request.paths);
    }
    
    Result<Validator::ValidationResult> validate(const ActionPlanner::ActionPlan& plan) {
        Validator validator(m_fs, m_hasher);
        return validator.validate_plan(plan);
    }
    
    Result<ExecutionReport> execute(const ActionPlanner::ActionPlan& plan, const ApprovalSet& approvals) {
        Executor executor(m_fs, m_hasher, m_journal);
        return executor.execute(plan, approvals.approved_action_ids);
    }
    
private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
    IJournal& m_journal;
    Scanner m_scanner;
    RelationshipClassifier m_classifier;
};

} // namespace dupli
