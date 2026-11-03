#pragma once

#include "dupli/action_planner.hpp"
#include "dupli/classifier.hpp"
#include "dupli/container_detector.hpp"
#include "dupli/executor.hpp"
#include "dupli/filesystem.hpp"
#include "dupli/grouping.hpp"
#include "dupli/hasher.hpp"
#include "dupli/journal.hpp"
#include "dupli/report.hpp"
#include "dupli/result.hpp"
#include "dupli/scanner.hpp"
#include "dupli/validator.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace dupli {

/// Result of the read-only analysis stage. All three buckets are kept distinct:
/// duplicates, unique files and entries that could not be analysed.
struct ClassificationResult {
    GroupingResult grouping;
    std::vector<Relationship> relationships;
    std::vector<DetectedContainer> containers;
    std::vector<std::filesystem::path> git_roots;
    std::vector<std::string> warnings;
};

/// GUI-neutral facade. A future wxWidgets front end calls exactly these methods
/// and never reimplements scanning, classification, planning or execution.
/// Threading rule: methods are synchronous and safe to call from a worker
/// thread; results must be marshalled back to the UI thread by the caller.
class DuplicateManagerService {
public:
    DuplicateManagerService(IFileSystem& fs, IHasher& hasher, IJournal& journal)
        : m_fs(fs), m_hasher(hasher), m_journal(journal), m_scanner(fs),
          m_owned_clock(std::make_unique<SystemClock>()), m_clock(*m_owned_clock) {}

    DuplicateManagerService(IFileSystem& fs, IHasher& hasher, IJournal& journal, IClock& clock)
        : m_fs(fs), m_hasher(hasher), m_journal(journal), m_scanner(fs),
          m_owned_clock(nullptr), m_clock(clock) {}

    DuplicateManagerService(const DuplicateManagerService&) = delete;
    DuplicateManagerService& operator=(const DuplicateManagerService&) = delete;

    Result<ScanResult> scan(const ScanOptions& options) const;
    Result<ClassificationResult> classify(const ScanResult& scan_result) const;
    Result<ActionPlan> create_plan(const PlanRequest& request) const;
    Result<ValidationResult> validate(const ActionPlan& plan, const ApprovalSet& approvals) const;
    Result<ExecutionReport> execute(const ActionPlan& plan, const ApprovalSet& approvals) const;
    Report build_report(const ScanResult& scan_result, const ClassificationResult& classification,
                        const std::string& mode,
                        const ExecutionReport* execution = nullptr) const;

private:
    IFileSystem& m_fs;
    IHasher& m_hasher;
    IJournal& m_journal;
    Scanner m_scanner;
    std::unique_ptr<SystemClock> m_owned_clock;
    IClock& m_clock;
};

} // namespace dupli
