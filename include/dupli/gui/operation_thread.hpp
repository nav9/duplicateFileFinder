#pragma once

#include <wx/event.h>
#include <wx/thread.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "dupli/action_planner.hpp"
#include "dupli/executor.hpp"
#include "dupli/journal.hpp"
#include "dupli/report.hpp"
#include "dupli/service.hpp"

namespace dupli {
namespace gui {

enum class OperationKind { Scan, Plan, Execute, Undo, Recover };

// Immutable snapshot of everything an operation needs. It is captured on the UI
// thread when the user starts the task, so editing settings or folders while the
// task runs cannot change its behavior.
struct OperationRequest {
    OperationKind kind = OperationKind::Scan;
    std::vector<std::filesystem::path> roots;
    std::vector<std::filesystem::path> excludes;
    bool include_hidden = true;
    bool cross_filesystem_boundaries = false;
    bool include_tree_fingerprints = true;
    size_t hash_buffer_size = 65536;
    dupli::ActionMode mode = dupli::ActionMode::PreviewOnly;
    std::filesystem::path destination;
    std::shared_ptr<dupli::ActionPlan> plan;
    std::vector<dupli::Approval> approvals;
    std::filesystem::path journal_path;
    std::shared_ptr<dupli::ScanResult> previous_scan;
    std::shared_ptr<dupli::ClassificationResult> previous_classification;
};

// Everything the UI needs, delivered as one event. Pointers keep the payload
// cheap to copy through wxWidgets' event queue; the data is immutable to the UI
// thread once handed over.
struct OperationResult {
    OperationKind kind = OperationKind::Scan;
    bool success = false;
    bool cancelled = false;
    std::string error;
    std::string summary;
    std::shared_ptr<dupli::ScanResult> scan;
    std::shared_ptr<dupli::ClassificationResult> classification;
    std::shared_ptr<dupli::Report> report;
    std::shared_ptr<dupli::ActionPlan> plan;
    std::shared_ptr<dupli::ExecutionReport> execution;
    std::shared_ptr<dupli::UndoReport> undo;
    std::shared_ptr<dupli::RecoveryReport> recovery;
};

wxDECLARE_EVENT(EVT_DUPLI_OPERATION_DONE, wxThreadEvent);

// Runs one operation on a worker thread and posts EVT_DUPLI_OPERATION_DONE to the
// sink. Workers never touch widgets: all UI work happens in the event handler.
//
// Cancellation is cooperative. A scan checks the flag between phases; an
// execution checks it before the first mutation so a mutation is never abandoned
// halfway because the user pressed Cancel.
class OperationThread : public wxThread {
public:
    OperationThread(wxEvtHandler* sink, OperationRequest request);
    ~OperationThread() override = default;

    ExitCode Entry() override;
    void Cancel() { m_cancel.store(true); }

private:
    void run(OperationResult& outcome);

    wxEvtHandler* m_sink;
    OperationRequest m_request;
    std::atomic<bool> m_cancel{false};
};

} // namespace gui
} // namespace dupli
