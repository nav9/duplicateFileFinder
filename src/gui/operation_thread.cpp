#include "dupli/gui/operation_thread.hpp"

#include "dupli/real_filesystem.hpp"

#include <exception>
#include <string>
#include <utility>

namespace dupli {
namespace gui {

wxDEFINE_EVENT(EVT_DUPLI_OPERATION_DONE, wxThreadEvent);

OperationThread::OperationThread(wxEvtHandler* sink, OperationRequest request)
    : wxThread(wxTHREAD_JOINABLE), m_sink(sink), m_request(std::move(request)) {}

wxThread::ExitCode OperationThread::Entry() {
    OperationResult outcome;
    outcome.kind = m_request.kind;

    try {
        run(outcome);
    } catch (const std::exception& error) {
        // A worker must never let an exception escape into wxWidgets' C code.
        outcome.success = false;
        outcome.error = std::string("Unexpected failure: ") + error.what();
    } catch (...) {
        outcome.success = false;
        outcome.error = "Unexpected failure";
    }

    // Marshal to the UI thread. The event owns a shared_ptr, so ownership is
    // unambiguous even if the user closes the window while it is queued.
    auto payload = std::make_shared<OperationResult>(std::move(outcome));
    auto* event = new wxThreadEvent(EVT_DUPLI_OPERATION_DONE);
    event->SetPayload(payload);
    wxQueueEvent(m_sink, event);
    return static_cast<wxThread::ExitCode>(nullptr);
}

void OperationThread::run(OperationResult& outcome) {
    dupli::RealFileSystem fs;
    dupli::Blake3Hasher hasher;
    dupli::SystemClock clock;

    if (m_request.kind == OperationKind::Scan) {
        dupli::ScanOptions options;
        options.roots = m_request.roots;
        options.include_hidden = m_request.include_hidden;
        options.cross_filesystem_boundaries = m_request.cross_filesystem_boundaries;
        options.excludes = m_request.excludes;
        options.max_entries = 500000; // Safety valve for accidental huge scans.

        dupli::FileJournal journal(m_request.journal_path, clock);
        dupli::DuplicateManagerService service(fs, hasher, journal, clock);

        auto scanned = service.scan(options);
        if (scanned.has_error()) {
            outcome.error = scanned.error().message;
            return;
        }
        if (m_cancel.load()) {
            outcome.cancelled = true;
            outcome.error = "Scan cancelled";
            return;
        }

        auto classification = service.classify(scanned.value());
        if (classification.has_error()) {
            outcome.error = classification.error().message;
            return;
        }
        if (m_cancel.load()) {
            outcome.cancelled = true;
            outcome.error = "Scan cancelled";
            return;
        }

        dupli::ReportBuilder builder(fs, hasher, clock, scanned.value(),
                                     classification.value().grouping);
        builder.set_containers(classification.value().containers);
        dupli::ReportOptions report_options;
        report_options.include_tree_fingerprints = m_request.include_tree_fingerprints;
        builder.set_options(report_options);

        outcome.scan = std::make_shared<dupli::ScanResult>(std::move(scanned.value()));
        outcome.classification =
            std::make_shared<dupli::ClassificationResult>(std::move(classification.value()));
        outcome.report = std::make_shared<dupli::Report>(builder.build("scan"));
        outcome.success = true;
        return;
    }

    if (m_request.kind == OperationKind::Plan) {
        if (!m_request.previous_scan || !m_request.previous_classification) {
            outcome.error = "Run a scan before generating a plan";
            return;
        }
        if (m_cancel.load()) {
            outcome.cancelled = true;
            outcome.error = "Planning cancelled";
            return;
        }

        // Planning is read-only: the planner neither writes nor predicts that a
        // later execution will succeed. Execution re-validates every action.
        dupli::FileJournal journal(m_request.journal_path, clock);
        dupli::DuplicateManagerService service(fs, hasher, journal, clock);

        const auto& classification = *m_request.previous_classification;
        dupli::PlanRequest request;
        request.mode = m_request.mode;
        request.destination = m_request.destination;
        request.duplicate_groups = classification.grouping.groups;
        request.unique_files = classification.grouping.unique_files;
        request.digest_by_id = classification.grouping.digest_by_id;
        request.git_roots = classification.git_roots;
        request.in_place = m_request.mode == dupli::ActionMode::InPlaceMerge;
        request.fs = &fs; // Only used to choose destination names that are free.

        auto plan = service.create_plan(request);
        if (plan.has_error()) {
            outcome.error = plan.error().message;
            return;
        }
        outcome.plan = std::make_shared<dupli::ActionPlan>(std::move(plan.value()));
        outcome.success = true;
        outcome.summary = std::to_string(outcome.plan->actions.size()) + " action(s) planned";
        return;
    }

    if (m_request.kind == OperationKind::Execute) {
        if (!m_request.plan) {
            outcome.error = "No action plan was supplied";
            return;
        }
        // Checked before the first mutation: a half-applied plan is worse than a
        // refusal, so cancellation never interrupts an execution.
        if (m_cancel.load()) {
            outcome.cancelled = true;
            outcome.error = "Execution cancelled before any change was made";
            return;
        }

        dupli::FileJournal journal(m_request.journal_path, clock);
        dupli::DuplicateManagerService service(fs, hasher, journal, clock);

        dupli::ApprovalSet approvals;
        approvals.plan_id = m_request.plan->plan_id;
        approvals.approvals = m_request.approvals;

        auto execution = service.execute(*m_request.plan, approvals);
        if (execution.has_error()) {
            outcome.error = execution.error().message;
            return;
        }
        outcome.execution = std::make_shared<dupli::ExecutionReport>(std::move(execution.value()));
        const auto& report = *outcome.execution;
        outcome.success = report.success;
        outcome.summary = std::to_string(report.succeeded_count()) + " succeeded, " +
                          std::to_string(report.failed_count()) + " failed, " +
                          std::to_string(report.skipped_count()) + " skipped";
        if (!report.errors.empty()) outcome.error = report.errors.front();
        return;
    }

    if (m_request.kind == OperationKind::Undo) {
        dupli::FileJournal journal(m_request.journal_path, clock);
        dupli::RecoveryEngine engine(fs, hasher);
        auto undone = engine.undo(journal, clock);
        if (undone.has_error()) {
            outcome.error = undone.error().message;
            return;
        }
        outcome.undo = std::make_shared<dupli::UndoReport>(std::move(undone.value()));
        outcome.success = outcome.undo->complete();
        outcome.summary = std::to_string(outcome.undo->undone) + " restored, " +
                          std::to_string(outcome.undo->failed) + " failed, " +
                          std::to_string(outcome.undo->skipped) + " skipped";
        return;
    }

    // Recover: inspect, never mutate.
    dupli::FileJournal journal(m_request.journal_path, clock);
    dupli::RecoveryEngine engine(fs, hasher);
    auto recovery = engine.inspect(journal);
    if (recovery.has_error()) {
        outcome.error = recovery.error().message;
        return;
    }
    outcome.recovery = std::make_shared<dupli::RecoveryReport>(std::move(recovery.value()));
    outcome.success = outcome.recovery->clean();
    outcome.summary = outcome.recovery->findings.empty()
                          ? "No interrupted operations"
                          : std::to_string(outcome.recovery->needs_attention) +
                                " operation(s) need manual review";
}

} // namespace gui
} // namespace dupli
