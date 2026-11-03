#include "dupli/service.hpp"

#include "dupli/fingerprint.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace dupli {

Result<ScanResult> DuplicateManagerService::scan(const ScanOptions& options) const {
    return m_scanner.scan(options);
}

Result<ClassificationResult> DuplicateManagerService::classify(
    const ScanResult& scan_result) const {
    ClassificationResult result;

    DuplicateGrouper grouper(m_fs, m_hasher);
    result.grouping = grouper.group(scan_result.files());

    // Relationships are derived from the already-computed digests instead of
    // re-hashing every file, so classification cannot disagree with the report.
    for (const auto& group : result.grouping.groups) {
        Relationship relationship;
        relationship.kind = RelationshipKind::ExactFileDuplicate;
        relationship.confidence = 1.0;
        relationship.evidence = "Identical BLAKE3 digest over complete file contents";
        for (const auto& file : group.files) relationship.entries.push_back(file.path);
        result.relationships.push_back(std::move(relationship));
    }

    // Exact tree duplicates are certified only when both trees were fully read.
    FolderFingerprinter fingerprinter(m_fs, m_hasher);
    std::vector<std::filesystem::path> directory_roots;
    for (const auto& root : scan_result.roots) {
        auto is_dir = m_fs.is_directory(root);
        if (is_dir.ok() && is_dir.value()) directory_roots.push_back(root);
    }
    std::vector<std::pair<std::filesystem::path, std::string>> fingerprinted;
    for (const auto& root : directory_roots) {
        auto fingerprint = fingerprinter.fingerprint_tree(root);
        if (!fingerprint.ok() || !fingerprint.value().complete) {
            result.warnings.push_back("Tree comparison skipped for " + path_to_utf8(root) +
                                      ": the tree could not be read completely");
            continue;
        }
        fingerprinted.emplace_back(root, fingerprint.value().fingerprint);
    }
    for (size_t i = 0; i < fingerprinted.size(); ++i) {
        for (size_t j = i + 1; j < fingerprinted.size(); ++j) {
            if (fingerprinted[i].second != fingerprinted[j].second) continue;
            Relationship relationship;
            relationship.kind = RelationshipKind::ExactTreeDuplicate;
            relationship.confidence = 1.0;
            relationship.evidence =
                "Identical tree fingerprint (relative paths, entry kinds, empty directories and "
                "complete file contents)";
            relationship.entries = {fingerprinted[i].first, fingerprinted[j].first};
            result.relationships.push_back(std::move(relationship));
        }
    }

    // Container detection is structural and read-only. Every detected container
    // forces review before anything inside it can be moved or merged.
    ContainerDetector detector(m_fs);
    for (const auto& entry : scan_result.entries) {
        if (!entry.is_dir || entry.excluded) continue;
        auto containers = detector.detect(entry.path);
        if (containers.has_error()) {
            result.warnings.push_back("Container detection failed for " + path_to_utf8(entry.path) +
                                      ": " + containers.error().message);
            continue;
        }
        for (const auto& container : containers.value()) {
            if (!container.is_container) continue;
            DetectedContainer detected;
            detected.path = entry.path;
            detected.type = container.type;
            detected.warnings = container.warnings;
            detected.requires_explicit_approval = container.requires_explicit_approval;
            if (detected.type == "git") result.git_roots.push_back(entry.path);
            result.containers.push_back(std::move(detected));
        }
    }

    std::sort(result.containers.begin(), result.containers.end(),
              [](const DetectedContainer& left, const DetectedContainer& right) {
                  return path_to_utf8(left.path) < path_to_utf8(right.path);
              });
    std::sort(result.git_roots.begin(), result.git_roots.end());
    result.git_roots.erase(std::unique(result.git_roots.begin(), result.git_roots.end()),
                           result.git_roots.end());

    for (const auto& issue : result.grouping.issues) {
        result.warnings.push_back("Could not analyse " + path_to_utf8(issue.path) + ": " +
                                  issue.message);
    }
    return result;
}

Result<ActionPlan> DuplicateManagerService::create_plan(const PlanRequest& request) const {
    ActionPlanner planner;
    return planner.create_plan(request);
}

Result<ValidationResult> DuplicateManagerService::validate(const ActionPlan& plan,
                                                           const ApprovalSet& approvals) const {
    Validator validator(m_fs, m_hasher);
    return validator.validate_plan(plan, approvals);
}

Result<ExecutionReport> DuplicateManagerService::execute(const ActionPlan& plan,
                                                         const ApprovalSet& approvals) const {
    Executor executor(m_fs, m_hasher, m_journal);
    return executor.execute(plan, approvals);
}

Report DuplicateManagerService::build_report(const ScanResult& scan_result,
                                             const ClassificationResult& classification,
                                             const std::string& mode,
                                             const ExecutionReport* execution) const {
    ReportBuilder builder(m_fs, m_hasher, m_clock, scan_result, classification.grouping);
    builder.set_containers(classification.containers);

    std::vector<std::string> notes = classification.warnings;
    if (execution) {
        notes.push_back("Succeeded: " + std::to_string(execution->succeeded_count()) +
                        ", failed: " + std::to_string(execution->failed_count()) +
                        ", skipped: " + std::to_string(execution->skipped_count()));
        for (const auto& error : execution->errors) notes.push_back("Error: " + error);
        for (const auto& warning : execution->warnings) notes.push_back("Warning: " + warning);
        if (execution->recovery_required) {
            notes.push_back("Recovery is required for at least one operation; inspect the journal");
        }
    }
    builder.set_execution_notes(std::move(notes));
    return builder.build(mode);
}

} // namespace dupli
