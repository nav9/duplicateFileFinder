#include "dupli/gui/history_tab.hpp"

#include "dupli/gui/platform_paths.hpp"
#include "dupli/gui/settings.hpp"
#include "dupli/gui/wx_utils.hpp"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbox.h>
#include <wx/utils.h>

#include <algorithm>
#include <string>

namespace dupli {
namespace gui {

namespace {

// The journal is append-only, so a long-lived installation can accumulate a lot
// of history. Only the newest records are shown; the file itself is never
// modified, so nothing is lost.
constexpr size_t kMaxDisplayedRecords = 2000;

bool state_matches_filter(JournalState state, int filter) {
    switch (filter) {
    case 1: return state == JournalState::Completed;
    case 2: return state == JournalState::Failed;
    case 3: return state == JournalState::RolledBack;
    default: return true;
    }
}

} // namespace

HistoryTab::HistoryTab(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    SetBackgroundColour(wxColour(45, 45, 48));
    create_controls();
    reload();
}

HistoryTab::~HistoryTab() {
    if (m_thread) {
        m_thread->Cancel();
        m_thread->Wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void HistoryTab::create_controls() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, "Task History - every change this program made");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(235, 235, 240));
    outer->Add(title, 0, wxALL, 8);

    auto* explanation = new wxStaticText(
        this, wxID_ANY,
        "Each move or copy is written to a journal before it happens, so an interrupted run can be "
        "inspected and a completed one can be reversed. Undo verifies the recorded content before "
        "touching anything and never overwrites a file that appeared at an original path.");
    explanation->SetForegroundColour(wxColour(200, 200, 205));
    explanation->Wrap(1000);
    outer->Add(explanation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* bar = new wxBoxSizer(wxHORIZONTAL);
    bar->Add(new wxStaticText(this, wxID_ANY, "Show:"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_filter = new wxChoice(this, wxID_ANY);
    m_filter->Append("All records");
    m_filter->Append("Completed");
    m_filter->Append("Failed");
    m_filter->Append("Undone (rolled back)");
    m_filter->SetSelection(0);
    bar->Add(m_filter, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    m_refreshButton = new wxButton(this, wxID_ANY, "Refresh");
    m_undoButton = new wxButton(this, wxID_ANY, "Undo journaled changes...");
    m_inspectButton = new wxButton(this, wxID_ANY, "Check interrupted work...");
    m_openFolderButton = new wxButton(this, wxID_ANY, "Open journal folder");
    bar->Add(m_refreshButton, 0, wxALL, 3);
    bar->Add(m_undoButton, 0, wxALL, 3);
    bar->Add(m_inspectButton, 0, wxALL, 3);
    bar->Add(m_openFolderButton, 0, wxALL, 3);
    outer->Add(bar, 0, wxLEFT | wxRIGHT, 8);

    m_journalPath = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_journalPath->SetForegroundColour(wxColour(170, 170, 176));
    outer->Add(m_journalPath, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // The splitter's panes must be children of the splitter itself; parenting
    // them to the tab trips a wxWidgets assertion.
    auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxSP_LIVE_UPDATE | wxSP_3DSASH);
    splitter->SetMinimumPaneSize(80);

    m_list = new wxListCtrl(splitter, wxID_ANY, wxDefaultPosition, wxSize(-1, 260),
                            wxLC_REPORT | wxLC_SINGLE_SEL);
    m_list->InsertColumn(0, "Time", wxLIST_FORMAT_LEFT, 150);
    m_list->InsertColumn(1, "Action", wxLIST_FORMAT_LEFT, 70);
    m_list->InsertColumn(2, "Kind", wxLIST_FORMAT_LEFT, 70);
    m_list->InsertColumn(3, "Source", wxLIST_FORMAT_LEFT, 300);
    m_list->InsertColumn(4, "Destination", wxLIST_FORMAT_LEFT, 300);
    m_list->InsertColumn(5, "State", wxLIST_FORMAT_LEFT, 100);
    m_list->SetBackgroundColour(wxColour(28, 28, 30));
    m_list->SetForegroundColour(wxColour(225, 225, 228));

    m_details = new wxTextCtrl(splitter, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_details->SetBackgroundColour(wxColour(28, 28, 30));
    m_details->SetForegroundColour(wxColour(210, 210, 215));

    splitter->SplitVertically(m_list, m_details, 620);
    splitter->SetSashGravity(0.7);
    outer->Add(splitter, 1, wxEXPAND | wxALL, 8);

    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_status->SetForegroundColour(wxColour(205, 205, 210));
    outer->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(outer);

    m_refreshButton->Bind(wxEVT_BUTTON, &HistoryTab::on_refresh, this);
    m_undoButton->Bind(wxEVT_BUTTON, &HistoryTab::on_undo, this);
    m_inspectButton->Bind(wxEVT_BUTTON, &HistoryTab::on_inspect, this);
    m_openFolderButton->Bind(wxEVT_BUTTON, &HistoryTab::on_open_folder, this);
    m_filter->Bind(wxEVT_CHOICE, &HistoryTab::on_filter_changed, this);
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &HistoryTab::on_selection_changed, this);
    Bind(EVT_DUPLI_OPERATION_DONE, &HistoryTab::on_operation_done, this);
}

void HistoryTab::reload() {
    const std::string path = Settings::getInstance().journalPath();
    m_journalPath->SetLabel("Journal: " + fromUtf8(path));

    SystemClock clock;
    FileJournal journal(path, clock);
    auto entries = journal.entries();
    if (entries.has_error()) {
        m_records.clear();
        m_status->SetLabel("The journal could not be read: " + fromUtf8(entries.error().message));
        populate();
        return;
    }

    m_records = latest_records(entries.value());
    // Newest first is what a user expects from a history list.
    std::stable_sort(m_records.begin(), m_records.end(),
                     [](const JournalRecord& left, const JournalRecord& right) {
                         return left.timestamp > right.timestamp;
                     });
    populate();
}

void HistoryTab::populate() {
    m_list->Freeze();
    m_list->DeleteAllItems();
    m_visible.clear();

    const int filter = m_filter->GetSelection();
    for (size_t index = 0; index < m_records.size(); ++index) {
        if (!state_matches_filter(m_records[index].state, filter)) continue;
        m_visible.push_back(index);
    }

    size_t shown = 0;
    for (auto it = m_visible.rbegin(); it != m_visible.rend(); ++it) {
        if (shown >= kMaxDisplayedRecords) break;
        const JournalRecord& record = m_records[*it];
        const long row = m_list->InsertItem(m_list->GetItemCount(), fromUtf8(record.timestamp));
        m_list->SetItem(row, 1, fromUtf8(record.action_id));
        m_list->SetItem(row, 2, fromUtf8(record.kind));
        m_list->SetItem(row, 3, wxFromPath(record.source));
        m_list->SetItem(row, 4, wxFromPath(record.destination));
        m_list->SetItem(row, 5, fromUtf8(to_string(record.state)));
        m_list->SetItemData(row, static_cast<long>(*it));
        ++shown;
    }
    m_list->Thaw();

    if (m_records.empty()) {
        m_status->SetLabel("The journal is empty: nothing has been moved or copied yet.");
    } else {
        m_status->SetLabel(format_count(m_visible.size()) + " record(s) shown, newest first" +
                           (m_visible.size() > kMaxDisplayedRecords
                                ? wxString::Format(" (only the newest %zu are listed)",
                                                   kMaxDisplayedRecords)
                                : wxString()));
    }
    update_controls();
}

bool HistoryTab::matches_filter(const JournalRecord& record) const {
    return state_matches_filter(record.state, m_filter->GetSelection());
}

wxString HistoryTab::describe(const JournalRecord& record) const {
    wxString text = "Action: " + fromUtf8(record.action_id) +
                    "\nJournal id: " + fromUtf8(record.id) +
                    "\nOperation: " + fromUtf8(record.kind) +
                    "\nState: " + fromUtf8(to_string(record.state)) +
                    "\nTime: " + fromUtf8(record.timestamp) +
                    "\nSource: " + wxFromPath(record.source) +
                    "\nDestination: " + wxFromPath(record.destination);
    if (!record.expected_digest.empty()) {
        text += "\nRecorded content digest: " + fromUtf8(record.expected_digest);
    }
    if (!record.error.empty()) text += "\nError: " + fromUtf8(record.error);
    text += "\n\nUndo re-verifies the file on disk against this record before touching it. "
            "A file that changed since then is skipped and reported instead of being overwritten.";
    return text;
}

void HistoryTab::on_selection_changed(wxListEvent& event) {
    const long item = event.GetIndex();
    if (item < 0) return;
    const long index = m_list->GetItemData(item);
    if (index < 0 || static_cast<size_t>(index) >= m_records.size()) return;
    m_details->SetValue(describe(m_records[static_cast<size_t>(index)]));
}

void HistoryTab::on_filter_changed(wxCommandEvent&) { populate(); }

void HistoryTab::on_refresh(wxCommandEvent&) { reload(); }

void HistoryTab::on_open_folder(wxCommandEvent&) {
    PlatformPaths::ensure_directories_exist();
    const std::filesystem::path directory = PlatformPaths::get_data_dir();
    if (!wxLaunchDefaultApplication(wxFromPath(directory))) {
        wxMessageBox("Could not open " + wxFromPath(directory) +
                         "\nOpen that folder manually to inspect the journal.",
                     "Open folder", wxOK | wxICON_INFORMATION, this);
    }
}

void HistoryTab::start_operation(OperationRequest request, const wxString& status) {
    if (m_busy) return;
    m_busy = true;
    m_status->SetLabel(status);
    update_controls();
    m_thread = new OperationThread(this, std::move(request));
    if (m_thread->Run() != wxTHREAD_NO_ERROR) {
        delete m_thread;
        m_thread = nullptr;
        finish_operation();
        m_status->SetLabel("Could not start a background worker; nothing was changed.");
    }
}

void HistoryTab::finish_operation() {
    m_busy = false;
    update_controls();
}

void HistoryTab::update_controls() {
    const bool idle = !m_busy;
    m_refreshButton->Enable(idle);
    m_filter->Enable(idle);
    m_undoButton->Enable(idle);
    m_inspectButton->Enable(idle);
    m_openFolderButton->Enable(idle);
}

void HistoryTab::on_undo(wxCommandEvent&) {
    const wxString question =
        "Reverse every change recorded in the journal, newest first?\n\n"
        "Moves are renamed back and copies are removed only after their content still matches the "
        "journal. An occupied original path is never overwritten. Entries that cannot be reversed "
        "safely are skipped and reported, and the journal is kept.";
    if (wxMessageBox(question, "Undo journaled changes",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        return;
    }
    OperationRequest request;
    request.kind = OperationKind::Undo;
    request.journal_path = Settings::getInstance().journalPath();
    start_operation(std::move(request), "Undoing journaled changes...");
}

void HistoryTab::on_inspect(wxCommandEvent&) {
    OperationRequest request;
    request.kind = OperationKind::Recover;
    request.journal_path = Settings::getInstance().journalPath();
    start_operation(std::move(request), "Inspecting the journal and the filesystem...");
}

void HistoryTab::on_operation_done(wxThreadEvent& event) {
    auto payload = event.GetPayload<std::shared_ptr<OperationResult>>();
    if (m_thread) {
        m_thread->Wait();
        delete m_thread;
        m_thread = nullptr;
    }
    finish_operation();
    if (!payload) return;
    const OperationResult& result = *payload;

    if (result.kind == OperationKind::Undo && result.undo) {
        wxString text = "Restored: " + format_count(result.undo->undone) +
                        "\nFailed: " + format_count(result.undo->failed) +
                        "\nSkipped: " + format_count(result.undo->skipped) + "\n";
        for (const auto& outcome : result.undo->outcomes) {
            if (outcome.message.empty()) continue;
            text += "\n" + fromUtf8(outcome.action_id) + ": " + fromUtf8(outcome.message);
        }
        wxMessageBox(text, "Undo result",
                     wxOK | (result.undo->complete() ? wxICON_INFORMATION : wxICON_WARNING), this);
    } else if (result.kind == OperationKind::Recover) {
        if (!result.recovery) {
            wxMessageBox("The journal could not be inspected:\n" + fromUtf8(result.error),
                         "Inspection failed", wxOK | wxICON_ERROR, this);
        } else if (result.recovery->findings.empty()) {
            wxMessageBox("No interrupted operations were found.", "Nothing to repair",
                         wxOK | wxICON_INFORMATION, this);
        } else {
            wxString text = result.recovery->clean()
                                ? "Interrupted operations can each be retried safely:\n"
                                : "These operations need a manual decision:\n";
            for (const auto& finding : result.recovery->findings) {
                text += "\n" + fromUtf8(finding.record.action_id) + " (" +
                        fromUtf8(finding.record.kind) + ")\n  " +
                        wxFromPath(finding.record.source) + "\n  -> " +
                        wxFromPath(finding.record.destination) + "\n  " +
                        fromUtf8(finding.explanation);
            }
            wxMessageBox(text, "Interrupted work", wxOK | wxICON_WARNING, this);
        }
    }

    reload();
}

} // namespace gui
} // namespace dupli
