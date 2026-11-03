// Headless command-line front end for the duplicate manager.
//
// Safety model: scanning and planning never mutate anything. `apply` (or
// `plan --apply`) executes only after the plan validates against the live
// filesystem, and actions that need review (Git repositories, in-place merges)
// must be approved individually by id. The CLI never deletes user data: the
// quarantine move keeps one copy, and cross-filesystem work copies while
// retaining the source.

#include "dupli/action_planner.hpp"
#include "dupli/executor.hpp"
#include "dupli/journal.hpp"
#include "dupli/real_filesystem.hpp"
#include "dupli/report.hpp"
#include "dupli/scanner.hpp"
#include "dupli/service.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace dupli;

namespace {

constexpr const char* kDefaultJournal = "dupli-journal.jsonl";

struct CliOptions {
    std::string command;
    std::vector<std::filesystem::path> roots;
    std::vector<std::filesystem::path> excludes;
    bool include_hidden = true;
    bool cross_boundaries = false;
    bool no_trees = false;
    bool apply = false;
    bool verbose = false;
    std::string mode = "preview";
    std::filesystem::path destination;
    std::filesystem::path json_out;
    std::filesystem::path plan_out;
    std::filesystem::path plan_in;
    std::filesystem::path journal = kDefaultJournal;
    std::vector<std::string> approvals;
};

void print_usage() {
    std::cout <<
        "dupli - duplicate and unique file manager (headless)\n"
        "\n"
        "Usage:\n"
        "  dupli scan <root>... [options]\n"
        "  dupli plan <root>... --mode <quarantine|unique|merge|preview> [--dest DIR] [--apply]\n"
        "  dupli apply --plan <plan.json> [--approve ACTION_ID]...\n"
        "  dupli undo --journal <journal.jsonl>\n"
        "  dupli recover --journal <journal.jsonl>\n"
        "\n"
        "Common options:\n"
        "  --exclude-hidden          Skip hidden entries (they are included by default)\n"
        "  --cross-boundaries        Allow crossing filesystem/mount boundaries\n"
        "  --exclude PATH            Exclude a path (repeatable)\n"
        "  --json FILE               Write a machine-readable report\n"
        "  --journal FILE            Journal path (default: " << kDefaultJournal << ")\n"
        "  --no-trees                Skip whole-tree fingerprint comparison\n"
        "  --verbose                 Show every issue\n"
        "\n"
        "Plan options:\n"
        "  --mode M                  preview (default), quarantine, unique, merge\n"
        "  --dest DIR                Destination folder for the operation\n"
        "  --apply                   Execute the plan after validation\n"
        "  --approve ID              Approve one review-gated action (repeatable)\n"
        "  --plan-out FILE           Save the generated plan\n"
        "\n"
        "Exit codes: 0 success, 1 error, 2 partial or failed work, 3 scan incomplete.\n";
}

int fail(const std::string& message) {
    std::cerr << "error: " << message << "\n";
    return 1;
}

bool parse_arguments(int argc, char** argv, CliOptions& options, std::string& error) {
    if (argc < 2) {
        print_usage();
        return false;
    }
    options.command = argv[1];
    if (options.command == "--help" || options.command == "-h" || options.command == "help") {
        print_usage();
        std::exit(0);
    }

    auto needs_value = [&](int index) {
        if (index + 1 >= argc) {
            error = "missing value for " + std::string(argv[index]);
            return false;
        }
        return true;
    };

    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--include-hidden") {
            options.include_hidden = true;
        } else if (arg == "--exclude-hidden") {
            options.include_hidden = false;
        } else if (arg == "--cross-boundaries") {
            options.cross_boundaries = true;
        } else if (arg == "--no-trees") {
            options.no_trees = true;
        } else if (arg == "--apply") {
            options.apply = true;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--exclude") {
            if (!needs_value(i)) return false;
            options.excludes.emplace_back(argv[++i]);
        } else if (arg == "--mode") {
            if (!needs_value(i)) return false;
            options.mode = argv[++i];
        } else if (arg == "--dest") {
            if (!needs_value(i)) return false;
            options.destination = argv[++i];
        } else if (arg == "--json") {
            if (!needs_value(i)) return false;
            options.json_out = argv[++i];
        } else if (arg == "--plan-out") {
            if (!needs_value(i)) return false;
            options.plan_out = argv[++i];
        } else if (arg == "--plan") {
            if (!needs_value(i)) return false;
            options.plan_in = argv[++i];
        } else if (arg == "--journal") {
            if (!needs_value(i)) return false;
            options.journal = argv[++i];
        } else if (arg == "--approve") {
            if (!needs_value(i)) return false;
            options.approvals.emplace_back(argv[++i]);
        } else if (!arg.empty() && arg.front() == '-') {
            error = "unknown option " + arg;
            return false;
        } else {
            options.roots.emplace_back(arg);
        }
    }
    return true;
}

std::filesystem::path absolute_path(const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) return path.lexically_normal();
    return absolute.lexically_normal();
}

ScanOptions make_scan_options(const CliOptions& options) {
    ScanOptions scan_options;
    // Roots are made absolute so exclusion checks (which compare paths
    // lexically) and journal/report paths stay consistent regardless of the
    // working directory a command was started from.
    for (const auto& root : options.roots) scan_options.roots.push_back(absolute_path(root));
    scan_options.include_hidden = options.include_hidden;
    scan_options.cross_filesystem_boundaries = options.cross_boundaries;
    for (const auto& exclude : options.excludes) {
        scan_options.excludes.push_back(absolute_path(exclude));
    }
    return scan_options;
}

void print_issue(const ScanIssue& issue) {
    std::cout << "    ! " << path_to_utf8(issue.path) << " [" << to_string(issue.code) << ", "
              << issue.operation << "] " << issue.message << "\n";
}

void print_report(const Report& report, const CliOptions& options) {
    std::cout << "Scan of " << report.roots.size() << " root(s)\n";
    for (const auto& root : report.roots) std::cout << "  - " << path_to_utf8(root) << "\n";
    std::cout << "Entries: " << report.summary.scan_entries
              << " | files analysed: " << report.summary.files_scanned
              << " | duplicate groups: " << report.summary.duplicate_groups
              << " (" << report.summary.duplicate_files << " files)"
              << " | unique files: " << report.summary.unique_files
              << " | unreadable: " << report.summary.unreadable_files << "\n";
    std::cout << "Reclaimable by quarantining duplicates: " << report.summary.reclaimable_bytes
              << " bytes\n";
    if (!report.scan_complete) {
        std::cout << "WARNING: the scan was not complete; unreadable entries are not proven "
                     "unique or duplicate.\n";
    }

    for (const auto& category : report.categories) {
        if (category.groups.empty()) continue;
        std::cout << "\n" << category.title << "\n  " << category.description << "\n";
        for (const auto& group : category.groups) {
            std::cout << "  * " << group.title << "\n";
            if (!group.evidence.empty()) std::cout << "      " << group.evidence << "\n";
            if (group.requires_review) std::cout << "      review required\n";
            for (const auto& item : group.items) {
                std::cout << "      - " << item.path;
                if (!item.note.empty()) std::cout << " (" << item.note << ")";
                std::cout << "\n";
            }
            for (const auto& warning : group.warnings) std::cout << "      ! " << warning << "\n";
        }
    }

    if (options.verbose && !report.issues.empty()) {
        std::cout << "\nIssues\n";
        for (const auto& issue : report.issues) print_issue(issue);
    }
}

int exit_code_for_scan(const Report& report) {
    if (!report.scan_complete) return 3;
    return 0;
}

bool write_text_file(const std::filesystem::path& path, const std::string& text,
                     std::string& error) {
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "cannot create " + path_to_utf8(path.parent_path()) + ": " + ec.message();
            return false;
        }
    }
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path_to_utf8(path) + " for writing";
        return false;
    }
    file << text;
    if (!file) {
        error = "failed while writing " + path_to_utf8(path);
        return false;
    }
    return true;
}

std::string read_text_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path_to_utf8(path) + " for reading";
        return {};
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

ApprovalSet make_approvals(const ActionPlan& plan, const std::vector<std::string>& ids,
                           std::string& error) {
    ApprovalSet approvals;
    approvals.plan_id = plan.plan_id;
    for (const auto& id : ids) {
        auto it = std::find_if(plan.actions.begin(), plan.actions.end(),
                               [&id](const PlannedAction& action) { return action.id == id; });
        if (it == plan.actions.end()) {
            error = "approval refers to unknown action id '" + id + "'";
            return approvals;
        }
        approvals.approvals.push_back(Approval{id, path_to_utf8(it->source), std::string()});
    }
    return approvals;
}

void print_plan(const ActionPlan& plan) {
    std::cout << "Plan " << plan.plan_id << " (" << to_string(plan.mode) << "), "
              << plan.actions.size() << " action(s)\n";
    for (const auto& action : plan.actions) {
        std::cout << "  " << action.id << " " << to_string(action.kind) << " "
                  << path_to_utf8(action.source) << " -> " << path_to_utf8(action.destination)
                  << "\n";
        if (!action.rationale.empty()) std::cout << "      " << action.rationale << "\n";
        if (action.requires_approval) {
            std::cout << "      APPROVAL REQUIRED: " << action.approval_reason << "\n";
        }
    }
    for (const auto& warning : plan.warnings) std::cout << "  ! " << warning << "\n";
    for (const auto& conflict : plan.unresolved_conflicts) {
        std::cout << "  CONFLICT: " << conflict << "\n";
    }
}

void print_execution(const ExecutionReport& execution) {
    std::cout << "\nExecution result: " << (execution.success ? "completed"
                                                              : (execution.partial ? "partially completed"
                                                                                   : "failed"))
              << "\n";
    std::cout << "  succeeded: " << execution.succeeded_count()
              << ", failed: " << execution.failed_count()
              << ", skipped: " << execution.skipped_count() << "\n";
    if (execution.bytes_transferred > 0) {
        std::cout << "  bytes copied: " << execution.bytes_transferred << "\n";
    }
    for (const auto& path : execution.sources_retained) {
        std::cout << "  source kept: " << path << "\n";
    }
    for (const auto& error : execution.errors) std::cout << "  error: " << error << "\n";
    for (const auto& warning : execution.warnings) std::cout << "  warning: " << warning << "\n";
    if (execution.recovery_required) {
        std::cout << "  RECOVERY REQUIRED: run `dupli recover --journal <file>` before retrying\n";
    }
}

int command_scan(const CliOptions& options) {
    if (options.roots.empty()) return fail("scan requires at least one root folder");

    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    SimpleJournal journal; // Scanning never mutates, so no durable journal is needed.
    DuplicateManagerService service(fs, hasher, journal, clock);

    auto scan = service.scan(make_scan_options(options));
    if (scan.has_error()) return fail(scan.error().message);

    auto classification = service.classify(scan.value());
    if (classification.has_error()) return fail(classification.error().message);

    ReportBuilder builder(fs, hasher, clock, scan.value(), classification.value().grouping);
    builder.set_containers(classification.value().containers);
    ReportOptions report_options;
    report_options.include_tree_fingerprints = !options.no_trees;
    builder.set_options(report_options);
    const Report report = builder.build("scan");

    print_report(report, options);

    if (!options.json_out.empty()) {
        auto saved = report.save(fs, options.json_out);
        if (saved.has_error()) return fail(saved.error().message);
        std::cout << "\nReport written to " << path_to_utf8(options.json_out) << "\n";
    }
    return exit_code_for_scan(report);
}

int command_plan(const CliOptions& options) {
    if (options.roots.empty()) return fail("plan requires at least one root folder");

    ActionMode mode = ActionMode::PreviewOnly;
    if (!parse_action_mode(options.mode, mode)) {
        return fail("unknown mode '" + options.mode +
                    "' (expected preview, quarantine, unique or merge)");
    }
    if (mode != ActionMode::PreviewOnly && options.destination.empty()) {
        return fail("--dest is required for quarantine, unique and merge plans");
    }

    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(options.journal, clock);
    DuplicateManagerService service(fs, hasher, journal, clock);

    auto scan_options = make_scan_options(options);
    const auto destination = options.destination.empty()
                                 ? std::filesystem::path()
                                 : absolute_path(options.destination);
    if (!destination.empty()) {
        // Never scan the output folder itself: previously quarantined copies or
        // collected files must not be treated as new source material.
        scan_options.excludes.push_back(destination);
    }

    auto scan = service.scan(scan_options);
    if (scan.has_error()) return fail(scan.error().message);
    auto classification = service.classify(scan.value());
    if (classification.has_error()) return fail(classification.error().message);

    PlanRequest request;
    request.mode = mode;
    request.destination = destination;
    request.duplicate_groups = classification.value().grouping.groups;
    request.unique_files = classification.value().grouping.unique_files;
    request.digest_by_id = classification.value().grouping.digest_by_id;
    request.git_roots = classification.value().git_roots;
    request.in_place = (mode == ActionMode::InPlaceMerge);
    // Lets the planner avoid names that already exist in the output folder,
    // instead of failing preflight with a collision.
    request.fs = &fs;

    auto plan = service.create_plan(request);
    if (plan.has_error()) return fail(plan.error().message);
    print_plan(plan.value());

    if (!options.plan_out.empty()) {
        std::string error;
        if (!write_text_file(options.plan_out, ActionPlanner::serialize_plan(plan.value()), error)) {
            return fail(error);
        }
        std::cout << "\nPlan written to " << path_to_utf8(options.plan_out) << "\n";
    }

    std::string approval_error;
    ApprovalSet approvals = make_approvals(plan.value(), options.approvals, approval_error);
    if (!approval_error.empty()) return fail(approval_error);

    ReportBuilder builder(fs, hasher, clock, scan.value(), classification.value().grouping);
    builder.set_containers(classification.value().containers);
    ReportOptions report_options;
    report_options.include_tree_fingerprints = !options.no_trees;
    builder.set_options(report_options);

    if (!options.apply) {
        std::cout << "\nPreview only: nothing was changed. Re-run with --apply to execute.\n";
        const Report report = builder.build("preview");
        if (!options.json_out.empty()) {
            auto saved = report.save(fs, options.json_out);
            if (saved.has_error()) return fail(saved.error().message);
            std::cout << "Report written to " << path_to_utf8(options.json_out) << "\n";
        }
        return exit_code_for_scan(report);
    }

    if (mode == ActionMode::PreviewOnly) {
        return fail("preview mode cannot be applied; choose quarantine, unique or merge");
    }

    if (plan.value().actions.empty()) {
        // Nothing to do is a success: the requested outcome already holds.
        std::cout << "\nNothing to do: no files need " << to_string(mode) << " treatment.\n";
        return 0;
    }

    auto validation = service.validate(plan.value(), approvals);
    if (validation.has_error()) return fail(validation.error().message);
    if (!validation.value().valid) {
        std::cout << "\nPreflight validation failed; nothing was changed:\n";
        for (const auto& error : validation.value().errors) std::cout << "  error: " << error << "\n";
        return 1;
    }

    auto execution = service.execute(plan.value(), approvals);
    if (execution.has_error()) return fail(execution.error().message);
    print_execution(execution.value());

    std::vector<std::string> notes;
    notes.push_back("Succeeded: " + std::to_string(execution.value().succeeded_count()) +
                    ", failed: " + std::to_string(execution.value().failed_count()) +
                    ", skipped: " + std::to_string(execution.value().skipped_count()));
    for (const auto& error : execution.value().errors) notes.push_back("Error: " + error);
    for (const auto& warning : execution.value().warnings) notes.push_back("Warning: " + warning);
    builder.set_execution_notes(std::move(notes));
    const Report report = builder.build("execute");

    if (!options.json_out.empty()) {
        auto saved = report.save(fs, options.json_out);
        if (saved.has_error()) return fail(saved.error().message);
        std::cout << "Report written to " << path_to_utf8(options.json_out) << "\n";
    }

    if (execution.value().success) return 0;
    return execution.value().partial ? 2 : 1;
}

int command_apply(const CliOptions& options) {
    if (options.plan_in.empty()) return fail("apply requires --plan <file>");

    std::string error;
    const std::string text = read_text_file(options.plan_in, error);
    if (!error.empty()) return fail(error);

    auto parsed = ActionPlanner::parse_plan(text);
    if (parsed.has_error()) return fail(parsed.error().message);

    std::string approval_error;
    ApprovalSet approvals = make_approvals(parsed.value(), options.approvals, approval_error);
    if (!approval_error.empty()) return fail(approval_error);

    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(options.journal, clock);
    DuplicateManagerService service(fs, hasher, journal, clock);

    if (parsed.value().actions.empty()) {
        std::cout << "The plan contains no actions; nothing to do.\n";
        return 0;
    }

    auto validation = service.validate(parsed.value(), approvals);
    if (validation.has_error()) return fail(validation.error().message);
    if (!validation.value().valid) {
        std::cout << "Preflight validation failed; nothing was changed:\n";
        for (const auto& problem : validation.value().errors) {
            std::cout << "  error: " << problem << "\n";
        }
        return 1;
    }

    auto execution = service.execute(parsed.value(), approvals);
    if (execution.has_error()) return fail(execution.error().message);
    print_execution(execution.value());
    std::cout << "Journal: " << path_to_utf8(journal.path()) << "\n";

    if (execution.value().success) return 0;
    return execution.value().partial ? 2 : 1;
}

int command_recover(const CliOptions& options) {
    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(options.journal, clock);

    auto records = journal.entries();
    if (records.has_error()) return fail(records.error().message);
    std::cout << "Journal: " << path_to_utf8(journal.path()) << " (" << records.value().size()
              << " record(s))\n";

    RecoveryEngine recovery(fs, hasher);
    auto report = recovery.inspect(journal);
    if (report.has_error()) return fail(report.error().message);
    if (report.value().findings.empty()) {
        std::cout << "No interrupted operations were found.\n";
        return 0;
    }
    for (const auto& finding : report.value().findings) {
        std::cout << "  " << finding.record.action_id << " " << finding.record.kind << " "
                  << path_to_utf8(finding.record.source) << " -> "
                  << path_to_utf8(finding.record.destination) << "\n      "
                  << finding.explanation << "\n";
    }
    return report.value().clean() ? 0 : 3;
}

int command_undo(const CliOptions& options) {
    RealFileSystem fs;
    Blake3Hasher hasher;
    SystemClock clock;
    FileJournal journal(options.journal, clock);

    RecoveryEngine recovery(fs, hasher);
    auto report = recovery.undo(journal, clock);
    if (report.has_error()) return fail(report.error().message);

    std::cout << "Undo: " << report.value().undone << " restored, " << report.value().failed
              << " failed, " << report.value().skipped << " skipped\n";
    for (const auto& outcome : report.value().outcomes) {
        std::cout << "  " << outcome.action_id << ": " << outcome.message << "\n";
    }
    return report.value().complete() ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
    CliOptions options;
    std::string parse_error;
    if (!parse_arguments(argc, argv, options, parse_error)) {
        if (!parse_error.empty()) return fail(parse_error);
        return 0;
    }

    try {
        if (options.command == "scan") return command_scan(options);
        if (options.command == "plan") return command_plan(options);
        if (options.command == "apply") return command_apply(options);
        if (options.command == "undo") return command_undo(options);
        if (options.command == "recover") return command_recover(options);
        print_usage();
        return fail("unknown command '" + options.command + "'");
    } catch (const std::exception& exception) {
        // Last-resort boundary: report instead of terminating silently.
        return fail(std::string("unexpected failure: ") + exception.what());
    }
}
