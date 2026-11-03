#include "dupli/gui/scan_tab.hpp"

#include "dupli/gui/file_dialog_helper.hpp"
#include "dupli/gui/plan_dialog.hpp"
#include "dupli/gui/platform_paths.hpp"
#include "dupli/gui/settings.hpp"
#include "dupli/gui/wx_utils.hpp"
#include "dupli/real_filesystem.hpp"

#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/statbox.h>

#include <string>
#include <utility>

namespace dupli {
namespace gui {

namespace {

// The three operations the core actually implements. "Preview only" is not a
// mode here: every plan is previewed in the plan dialog before it can be applied,
// so a separate do-nothing mode would only be a trap.
struct ModeEntry {
    const char* label;
    ActionMode mode;
};

const ModeEntry kModes[] = {
    {"Move duplicates to a quarantine folder", ActionMode::QuarantineDuplicates},
    {"Copy unique files into a collection folder", ActionMode::CreateUniqueCollection},
    {"Merge folders in place (advanced)", ActionMode::InPlaceMerge},
};

int mode_index_from_settings(const std::string& value) {
    if (value == "unique") return 1;
    if (value == "merge") return 2;
    return 0; // preview and quarantine both mean "quarantine" in this dialog
}

wxString human_result(bool success, bool partial) {
    if (success) return "completed";
    return partial ? "partially completed" : "failed";
}

} // namespace

ScanTab::ScanTab(wxWindow* parent) : wxPanel(parent, wxID_ANY), m_pulseTimer(this) {
    SetBackgroundColour(wxColour(45, 45, 48));
    create_controls();
    reload_settings();
    update_controls();
}

ScanTab::~ScanTab() {
    m_pulseTimer.Stop();
    if (m_thread) {
        // The tab is only destroyed when no operation is running (the frame
        // vetoes closing), but cancel and join as a last line of defence so a
        // worker can never outlive the widgets it posts events to.
        m_thread->Cancel();
        m_thread->Wait();
        delete m_thread;
        m_thread = nullptr;
    }
}

void ScanTab::create_controls() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(
        this, wxID_ANY,
        "Duplicate Files - scan folders, review every duplicate, then decide what to change");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(235, 235, 240));
    outer->Add(title, 0, wxALL, 8);

    auto* explanation = new wxStaticText(
        this, wxID_ANY,
        "Files are compared by their complete content (BLAKE3), never by name or size alone. "
        "Nothing is ever deleted: duplicates are moved to a folder you choose, and every "
        "completed change is journaled so it can be undone.");
    explanation->SetForegroundColour(wxColour(200, 200, 205));
    explanation->Wrap(1000);
    outer->Add(explanation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxSP_LIVE_UPDATE | wxSP_3DSASH);
    splitter->SetMinimumPaneSize(120);

    auto* controls = new wxScrolledWindow(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxVSCROLL);
    controls->SetBackgroundColour(wxColour(45, 45, 48));
    controls->SetScrollRate(0, 12);

    auto* controlsSizer = new wxBoxSizer(wxVERTICAL);
    controlsSizer->Add(create_input_panel(controls), 0, wxEXPAND | wxALL, 6);

    // Action bar: start/cancel the analysis and expose the report.
    auto* actionRow = new wxBoxSizer(wxHORIZONTAL);
    m_scanButton = new wxButton(controls, wxID_ANY, "Scan and analyse");
    m_scanButton->SetToolTip("Scan the selected folders and group files by complete content");
    m_cancelButton = new wxButton(controls, wxID_ANY, "Cancel");
    m_cancelButton->SetToolTip("Ask the running operation to stop at its next safe point");
    m_saveReportButton = new wxButton(controls, wxID_ANY, "Save report...");
    m_saveReportButton->SetToolTip("Write the full findings as a JSON report");
    m_gauge = new wxGauge(controls, wxID_ANY, 100, wxDefaultPosition, wxSize(160, -1));
    m_gauge->SetToolTip("The scan does not report a percentage; this bar shows that work is running");
    m_status = new wxStaticText(controls, wxID_ANY, "Ready. Add at least one folder to scan.");
    m_status->SetForegroundColour(wxColour(210, 210, 215));
    actionRow->Add(m_scanButton, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    actionRow->Add(m_cancelButton, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    actionRow->Add(m_saveReportButton, 0, wxALL | wxALIGN_CENTER_VERTICAL, 4);
    actionRow->Add(m_gauge, 0, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    actionRow->Add(m_status, 1, wxALL | wxALIGN_CENTER_VERTICAL, 6);
    controlsSizer->Add(actionRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    controlsSizer->Add(create_plan_panel(controls), 0, wxEXPAND | wxALL, 6);
    controls->SetSizer(controlsSizer);
    controls->FitInside();

    m_resultView = new ResultView(splitter);
    splitter->SplitHorizontally(controls, m_resultView, 320);
    splitter->SetSashGravity(0.0);
    outer->Add(splitter, 1, wxEXPAND | wxALL, 8);

    SetSizer(outer);

    m_addFolder->Bind(wxEVT_BUTTON, &ScanTab::on_add_folder, this);
    m_removeFolder->Bind(wxEVT_BUTTON, &ScanTab::on_remove_folder, this);
    m_clearFolders->Bind(wxEVT_BUTTON, &ScanTab::on_clear_folders, this);
    m_addExclude->Bind(wxEVT_BUTTON, &ScanTab::on_add_exclude, this);
    m_removeExclude->Bind(wxEVT_BUTTON, &ScanTab::on_remove_exclude, this);
    m_scanButton->Bind(wxEVT_BUTTON, &ScanTab::on_scan, this);
    m_cancelButton->Bind(wxEVT_BUTTON, &ScanTab::on_cancel, this);
    m_saveReportButton->Bind(wxEVT_BUTTON, &ScanTab::on_save_report, this);
    m_previewButton->Bind(wxEVT_BUTTON, &ScanTab::on_preview_plan, this);
    m_undoButton->Bind(wxEVT_BUTTON, &ScanTab::on_undo_journal, this);
    m_inspectButton->Bind(wxEVT_BUTTON, &ScanTab::on_inspect_journal, this);
    m_mode->Bind(wxEVT_CHOICE, &ScanTab::on_mode_changed, this);
    m_browseDestination->Bind(wxEVT_BUTTON, &ScanTab::on_browse_destination, this);
    Bind(EVT_DUPLI_OPERATION_DONE, &ScanTab::on_operation_done, this);
    Bind(wxEVT_TIMER, &ScanTab::on_pulse, this);
}

wxWindow* ScanTab::create_input_panel(wxWindow* parent) {
    auto* panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(wxColour(45, 45, 48));
    auto* row = new wxBoxSizer(wxHORIZONTAL);

    auto* foldersBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Folders to scan");
    auto* foldersParent = foldersBox->GetStaticBox();
    m_roots = new wxListBox(foldersParent, wxID_ANY, wxDefaultPosition, wxSize(-1, 110));
    m_roots->SetToolTip("Every selected folder is searched; overlapping folders are handled once");
    foldersBox->Add(m_roots, 1, wxEXPAND | wxALL, 4);

    auto* folderButtons = new wxBoxSizer(wxHORIZONTAL);
    m_addFolder = new wxButton(foldersParent, wxID_ANY, "Add folder...");
    m_removeFolder = new wxButton(foldersParent, wxID_ANY, "Remove");
    m_clearFolders = new wxButton(foldersParent, wxID_ANY, "Clear");
    folderButtons->Add(m_addFolder, 0, wxALL, 2);
    folderButtons->Add(m_removeFolder, 0, wxALL, 2);
    folderButtons->Add(m_clearFolders, 0, wxALL, 2);
    foldersBox->Add(folderButtons, 0, wxALL, 2);

    auto* optionsBox = new wxStaticBoxSizer(wxVERTICAL, panel, "Scan options");
    auto* optionsParent = optionsBox->GetStaticBox();
    m_hidden = new wxCheckBox(optionsParent, wxID_ANY, "Include hidden files and folders");
    m_hidden->SetToolTip("Hidden entries are included by default; uncheck to skip them");
    m_crossBoundaries = new wxCheckBox(optionsParent, wxID_ANY,
                                       "Cross filesystem/mount boundaries");
    m_crossBoundaries->SetToolTip(
        "Off by default: other disks and network shares are not scanned accidentally");
    m_compareTrees = new wxCheckBox(optionsParent, wxID_ANY,
                                    "Compare whole directory trees as well");
    m_compareTrees->SetToolTip(
        "Detects folders that are duplicates as a whole. Slower because every file is hashed.");
    optionsBox->Add(m_hidden, 0, wxALL, 3);
    optionsBox->Add(m_crossBoundaries, 0, wxALL, 3);
    optionsBox->Add(m_compareTrees, 0, wxALL, 3);

    optionsBox->Add(new wxStaticText(optionsParent, wxID_ANY, "Excluded paths (never scanned):"),
                    0, wxLEFT | wxRIGHT | wxTOP, 4);
    m_excludes = new wxListBox(optionsParent, wxID_ANY, wxDefaultPosition, wxSize(-1, 54));
    m_excludes->SetToolTip("Useful to keep a quarantine folder out of later scans");
    optionsBox->Add(m_excludes, 1, wxEXPAND | wxALL, 4);
    auto* excludeButtons = new wxBoxSizer(wxHORIZONTAL);
    m_addExclude = new wxButton(optionsParent, wxID_ANY, "Add exclusion...");
    m_removeExclude = new wxButton(optionsParent, wxID_ANY, "Remove");
    excludeButtons->Add(m_addExclude, 0, wxALL, 2);
    excludeButtons->Add(m_removeExclude, 0, wxALL, 2);
    optionsBox->Add(excludeButtons, 0, wxALL, 2);

    row->Add(foldersBox, 1, wxEXPAND | wxALL, 4);
    row->Add(optionsBox, 1, wxEXPAND | wxALL, 4);
    panel->SetSizer(row);
    return panel;
}

wxWindow* ScanTab::create_plan_panel(wxWindow* parent) {
    auto* panel = new wxPanel(parent, wxID_ANY);
    panel->SetBackgroundColour(wxColour(45, 45, 48));
    auto* box = new wxStaticBoxSizer(wxVERTICAL, panel, "Decide what to do with the duplicates");
    auto* boxParent = box->GetStaticBox();

    auto* modeRow = new wxBoxSizer(wxHORIZONTAL);
    modeRow->Add(new wxStaticText(boxParent, wxID_ANY, "Operation:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_mode = new wxChoice(boxParent, wxID_ANY);
    for (const auto& entry : kModes) m_mode->Append(fromUtf8(entry.label));
    m_mode->SetSelection(0);
    modeRow->Add(m_mode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    modeRow->Add(new wxStaticText(boxParent, wxID_ANY, "Destination:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_destination = new wxTextCtrl(boxParent, wxID_ANY);
    m_destination->SetHint("Folder that will receive the moved or copied files");
    modeRow->Add(m_destination, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_browseDestination = new wxButton(boxParent, wxID_ANY, "Browse...");
    modeRow->Add(m_browseDestination, 0, wxALIGN_CENTER_VERTICAL);
    box->Add(modeRow, 0, wxEXPAND | wxALL, 4);

    m_planHint = new wxStaticText(boxParent, wxID_ANY, wxEmptyString);
    m_planHint->SetForegroundColour(wxColour(205, 205, 210));
    box->Add(m_planHint, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    auto* actionRow = new wxBoxSizer(wxHORIZONTAL);
    m_previewButton = new wxButton(boxParent, wxID_ANY, "Preview plan...");
    m_previewButton->SetToolTip(
        "Build the plan from the current scan and show every single action before applying it");
    m_undoButton = new wxButton(boxParent, wxID_ANY, "Undo journaled changes...");
    m_undoButton->SetToolTip(
        "Reverse the changes recorded in the journal, newest first, verifying each file first");
    m_inspectButton = new wxButton(boxParent, wxID_ANY, "Check interrupted work...");
    m_inspectButton->SetToolTip(
        "Inspect the journal and the filesystem for a change that was interrupted mid-way");
    actionRow->Add(m_previewButton, 0, wxALL, 3);
    actionRow->Add(m_undoButton, 0, wxALL, 3);
    actionRow->Add(m_inspectButton, 0, wxALL, 3);
    box->Add(actionRow, 0, wxALL, 2);

    m_journalPathText = new wxStaticText(boxParent, wxID_ANY, wxEmptyString);
    m_journalPathText->SetForegroundColour(wxColour(170, 170, 176));
    box->Add(m_journalPathText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    panel->SetSizer(box);
    return panel;
}

void ScanTab::reload_settings() {
    Settings& settings = Settings::getInstance();
    m_hidden->SetValue(settings.show_hidden_entries);
    m_crossBoundaries->SetValue(settings.follow_filesystem_boundaries);
    m_compareTrees->SetValue(settings.compare_whole_trees);
    m_mode->SetSelection(mode_index_from_settings(settings.default_mode));
    m_journalPathText->SetLabel("Journal: " + fromUtf8(settings.journalPath()));
    update_plan_hint();
    update_controls();
}

void ScanTab::update_plan_hint() {
    wxString hint;
    switch (selected_mode()) {
    case ActionMode::CreateUniqueCollection:
        hint = "Copies files whose content appears exactly once into the destination. "
               "Duplicate files stay where they are, so nothing is lost if you change your mind.";
        break;
    case ActionMode::InPlaceMerge:
        hint = "Advanced: merges folders in place. Each affected action needs its own approval, "
               "and unlike the other operations this one cannot be reversed by re-running the "
               "program - only by restoring from your own backup.";
        break;
    case ActionMode::QuarantineDuplicates:
    default:
        hint = "Moves every duplicate except the suggested keeper into the destination, keeping "
               "enough path information to undo the move. Nothing is deleted.";
        break;
    }
    m_planHint->SetLabel(hint);
    m_planHint->Wrap(1000);
}

std::vector<std::filesystem::path> ScanTab::roots() const {
    std::vector<std::filesystem::path> result;
    for (unsigned int i = 0; i < m_roots->GetCount(); ++i) {
        result.push_back(pathFromWx(m_roots->GetString(i)));
    }
    return result;
}

std::filesystem::path ScanTab::destination() const {
    const wxString value = m_destination->GetValue().Strip(wxString::both);
    if (value.empty()) return {};
    return pathFromWx(value);
}

ActionMode ScanTab::selected_mode() const {
    const int index = m_mode->GetSelection();
    if (index < 0 || index >= static_cast<int>(sizeof(kModes) / sizeof(kModes[0]))) {
        return ActionMode::QuarantineDuplicates;
    }
    return kModes[index].mode;
}

void ScanTab::set_status(const wxString& text) { m_status->SetLabel(text); }

void ScanTab::mark_results_stale(const wxString& reason) {
    m_results_stale = true;
    m_resultView->clear(reason);
    update_controls();
}

void ScanTab::update_controls() {
    const bool idle = !m_busy;
    const bool have_results = m_scan != nullptr && !m_results_stale;

    m_addFolder->Enable(idle);
    m_removeFolder->Enable(idle);
    m_clearFolders->Enable(idle);
    m_addExclude->Enable(idle);
    m_removeExclude->Enable(idle);
    m_hidden->Enable(idle);
    m_crossBoundaries->Enable(idle);
    m_compareTrees->Enable(idle);
    m_mode->Enable(idle);
    m_destination->Enable(idle);
    m_browseDestination->Enable(idle);
    m_roots->Enable(idle);
    m_excludes->Enable(idle);

    m_scanButton->Enable(idle && m_roots->GetCount() > 0);
    m_cancelButton->Enable(m_busy && m_cancellable);
    m_saveReportButton->Enable(idle && m_report != nullptr);
    m_previewButton->Enable(idle && have_results);
    m_undoButton->Enable(idle);
    m_inspectButton->Enable(idle);
}

void ScanTab::start_operation(OperationRequest request, const wxString& status, bool cancellable) {
    if (m_busy) return;
    m_busy = true;
    m_cancellable = cancellable;
    set_status(status);
    m_gauge->SetValue(0);
    m_pulseTimer.Start(200);
    update_controls();

    m_thread = new OperationThread(this, std::move(request));
    if (m_thread->Run() != wxTHREAD_NO_ERROR) {
        delete m_thread;
        m_thread = nullptr;
        finish_operation();
        set_status("Could not start a background worker; nothing was changed.");
    }
}

void ScanTab::finish_operation() {
    m_busy = false;
    m_cancellable = false;
    m_pulseTimer.Stop();
    // The gauge is a "work in progress" indicator, not a percentage: it drops
    // back to zero when the worker stops so a finished or cancelled operation
    // never looks like 100% success.
    m_gauge->SetValue(0);
    update_controls();
}

void ScanTab::on_pulse(wxTimerEvent&) {
    if (m_busy) m_gauge->Pulse();
}

void ScanTab::on_add_folder(wxCommandEvent&) {
    const std::string path = FileDialogHelper::ShowBrowseFolderDialog(this, "Choose a folder to scan");
    if (path.empty()) return;
    for (unsigned int i = 0; i < m_roots->GetCount(); ++i) {
        if (toUtf8(m_roots->GetString(i)) == path) {
            wxMessageBox("That folder is already in the list.", "Already added",
                         wxOK | wxICON_INFORMATION, this);
            return;
        }
    }
    m_roots->Append(fromUtf8(path));
    update_controls();
}

void ScanTab::on_remove_folder(wxCommandEvent&) {
    const int selection = m_roots->GetSelection();
    if (selection == wxNOT_FOUND) return;
    m_roots->Delete(static_cast<unsigned int>(selection));
    update_controls();
}

void ScanTab::on_clear_folders(wxCommandEvent&) {
    m_roots->Clear();
    update_controls();
}

void ScanTab::on_add_exclude(wxCommandEvent&) {
    const std::string path = FileDialogHelper::ShowBrowseFolderDialog(this, "Choose a folder to exclude");
    if (path.empty()) return;
    m_excludes->Append(fromUtf8(path));
}

void ScanTab::on_remove_exclude(wxCommandEvent&) {
    const int selection = m_excludes->GetSelection();
    if (selection == wxNOT_FOUND) return;
    m_excludes->Delete(static_cast<unsigned int>(selection));
}

void ScanTab::on_mode_changed(wxCommandEvent&) { update_plan_hint(); }

void ScanTab::on_browse_destination(wxCommandEvent&) {
    const std::string path =
        FileDialogHelper::ShowBrowseFolderDialog(this, "Choose the destination folder");
    if (path.empty()) return;
    m_destination->SetValue(fromUtf8(path));
}

void ScanTab::on_save_report(wxCommandEvent&) {
    if (!m_report) return;
    wxFileDialog dialog(this, "Save the scan report", wxEmptyString, "dupli-report.json",
                        "JSON report (*.json)|*.json|All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;

    RealFileSystem fs;
    const std::filesystem::path path = pathFromWx(dialog.GetPath());
    auto saved = m_report->save(fs, path);
    if (saved.has_error()) {
        wxMessageBox("Could not write the report:\n" + fromUtf8(saved.error().message), "Save failed",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    set_status("Report written to " + wxFromPath(path));
}

void ScanTab::on_scan(wxCommandEvent&) {
    if (roots().empty()) {
        wxMessageBox("Add at least one folder to scan.", "No folders selected",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    // Snapshot everything the scan needs now: changing a checkbox or folder
    // while it runs must not alter the running scan.
    OperationRequest request;
    request.kind = OperationKind::Scan;
    request.roots = roots();
    for (unsigned int i = 0; i < m_excludes->GetCount(); ++i) {
        request.excludes.push_back(pathFromWx(m_excludes->GetString(i)));
    }
    request.include_hidden = m_hidden->IsChecked();
    request.cross_filesystem_boundaries = m_crossBoundaries->IsChecked();
    request.include_tree_fingerprints = m_compareTrees->IsChecked();
    request.journal_path = Settings::getInstance().journalPath();

    const size_t rootCount = request.roots.size();
    start_operation(std::move(request),
                    wxString::Format("Scanning %zu folder(s) with BLAKE3; this can take a while "
                                     "for large trees...",
                                     rootCount),
                    true);
}

void ScanTab::on_cancel(wxCommandEvent&) {
    if (!m_thread || !m_cancellable) return;
    m_thread->Cancel();
    set_status("Cancelling... the current file or phase must finish before the scan stops.");
}

void ScanTab::on_preview_plan(wxCommandEvent&) {
    if (!m_scan || !m_classification) return;
    const ActionMode mode = selected_mode();
    if (destination().empty()) {
        wxMessageBox("Choose a destination folder first.",
                     "Destination required", wxOK | wxICON_INFORMATION, this);
        return;
    }

    OperationRequest request;
    request.kind = OperationKind::Plan;
    request.mode = mode;
    request.destination = destination();
    request.previous_scan = m_scan;
    request.previous_classification = m_classification;
    request.journal_path = Settings::getInstance().journalPath();
    start_operation(std::move(request), "Building the plan from the current scan...", false);
}

void ScanTab::on_undo_journal(wxCommandEvent&) {
    Settings& settings = Settings::getInstance();
    const wxString journal = fromUtf8(settings.journalPath());
    const wxString question =
        "Reverse every change recorded in this journal, newest first?\n\nJournal: " + journal +
        "\n\n"
        "Each move is renamed back and each copy is removed only after its content is verified to "
        "still match what was recorded. An occupied original path is never overwritten: such an "
        "entry is skipped and reported. Files that other programs changed since the journal entry "
        "are left untouched.";
    if (wxMessageBox(question, "Undo journaled changes",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        return;
    }

    OperationRequest request;
    request.kind = OperationKind::Undo;
    request.journal_path = settings.journalPath();
    start_operation(std::move(request), "Undoing journaled changes...", false);
}

void ScanTab::on_inspect_journal(wxCommandEvent&) {
    OperationRequest request;
    request.kind = OperationKind::Recover;
    request.journal_path = Settings::getInstance().journalPath();
    start_operation(std::move(request), "Inspecting the journal and the filesystem...", false);
}

void ScanTab::on_operation_done(wxThreadEvent& event) {
    auto payload = event.GetPayload<std::shared_ptr<OperationResult>>();
    if (m_thread) {
        m_thread->Wait();
        delete m_thread;
        m_thread = nullptr;
    }
    finish_operation();
    if (!payload) return;
    const OperationResult& result = *payload;

    if (result.kind == OperationKind::Scan) {
        if (result.cancelled) {
            set_status("Scan cancelled. Nothing was changed.");
            return;
        }
        if (!result.success || !result.scan || !result.classification || !result.report) {
            set_status("Scan failed.");
            wxMessageBox("The scan could not be completed:\n" + fromUtf8(result.error),
                         "Scan failed", wxOK | wxICON_ERROR, this);
            return;
        }
        m_scan = result.scan;
        m_classification = result.classification;
        m_report = result.report;
        m_results_stale = false;
        m_resultView->show_scan(m_scan, m_classification, m_report);

        const ReportSummary& summary = m_report->summary;
        wxString status = format_count(summary.duplicate_groups) + " duplicate set(s), " +
                          format_count(summary.duplicate_files) + " duplicate file(s), " +
                          format_count(summary.unique_files) + " unique, " +
                          format_bytes(summary.reclaimable_bytes) + " reclaimable.";
        if (!m_report->scan_complete) {
            status += " The scan was NOT complete - see Unreadable entries before deleting anything.";
        }
        set_status(status);
        update_controls();

        if (summary.duplicate_groups == 0) {
            wxMessageBox(
                m_report->scan_complete
                    ? "No duplicate files were found in the selected folders."
                    : "No duplicate files were found, but the scan could not read every entry, so "
                      "this result is not proof that the folders contain no duplicates.",
                "Scan complete", wxOK | wxICON_INFORMATION, this);
        }
        return;
    }

    if (result.kind == OperationKind::Plan) {
        if (result.cancelled) {
            set_status("Planning cancelled.");
            return;
        }
        if (!result.success || !result.plan) {
            set_status("Could not build the plan.");
            wxMessageBox("The plan could not be built:\n" + fromUtf8(result.error),
                         "Planning failed", wxOK | wxICON_ERROR, this);
            return;
        }
        set_status(wxString::Format("Plan ready: %zu action(s). Review it before applying.",
                                    result.plan->actions.size()));

        PlanDialog dialog(this, result.plan,
                          "After applying, use \"Undo journaled changes\" on this tab (or the "
                          "Task History tab) to reverse the moves. Undo refuses to overwrite a "
                          "file that appeared at an original path in the meantime.");
        if (dialog.ShowModal() == wxID_OK && dialog.applyRequested()) {
            OperationRequest request;
            request.kind = OperationKind::Execute;
            request.plan = result.plan;
            request.approvals = dialog.approvals();
            request.journal_path = Settings::getInstance().journalPath();
            start_operation(std::move(request),
                            "Applying the plan: re-verifying each source, then moving or copying...",
                            false);
        }
        return;
    }

    if (result.kind == OperationKind::Execute) {
        if (result.cancelled) {
            set_status("Execution cancelled before any change was made.");
            return;
        }
        if (!result.execution) {
            set_status("Execution failed before anything was changed.");
            wxMessageBox("The plan was not applied:\n" + fromUtf8(result.error),
                         "Nothing was changed", wxOK | wxICON_ERROR, this);
            mark_results_stale("The plan could not be applied; the results above are from the "
                               "previous scan.");
            return;
        }
        const ExecutionReport& execution = *result.execution;
        wxString text = "Result: " + human_result(execution.success, execution.partial) + "\n\n" +
                        "Succeeded: " + format_count(execution.succeeded_count()) +
                        "\nFailed: " + format_count(execution.failed_count()) +
                        "\nSkipped: " + format_count(execution.skipped_count()) + "\n";
        if (execution.bytes_transferred > 0) {
            text += "Bytes copied: " + format_bytes(execution.bytes_transferred) + "\n";
        }
        for (const auto& path : execution.sources_retained) {
            text += "\nSource kept (destination is on another filesystem): " + fromUtf8(path);
        }
        for (const auto& error : execution.errors) text += "\nError: " + fromUtf8(error);
        for (const auto& warning : execution.warnings) text += "\nWarning: " + fromUtf8(warning);
        if (execution.recovery_required) {
            text += "\n\nRECOVERY REQUIRED: use \"Check interrupted work...\" before retrying.";
        }
        text += "\n\nJournal: " + fromUtf8(Settings::getInstance().journalPath());
        text += "\nUse \"Undo journaled changes...\" (or the Task History tab) to reverse this.";

        set_status("Operation " + human_result(execution.success, execution.partial) + ": " +
                   format_count(execution.succeeded_count()) + " succeeded, " +
                   format_count(execution.failed_count()) + " failed, " +
                   format_count(execution.skipped_count()) + " skipped.");
        wxMessageBox(text, "Plan applied", wxOK | (execution.success ? wxICON_INFORMATION
                                                                     : wxICON_WARNING),
                     this);
        mark_results_stale("The files changed since the results above were produced. "
                           "Scan again to see the current state.");
        return;
    }

    if (result.kind == OperationKind::Undo) {
        if (!result.undo) {
            set_status("Undo failed.");
            wxMessageBox("Undo could not run:\n" + fromUtf8(result.error), "Undo failed",
                         wxOK | wxICON_ERROR, this);
            return;
        }
        const UndoReport& undo = *result.undo;
        wxString text = "Restored: " + format_count(undo.undone) +
                        "\nFailed: " + format_count(undo.failed) +
                        "\nSkipped: " + format_count(undo.skipped) + "\n";
        for (const auto& outcome : undo.outcomes) {
            if (outcome.message.empty()) continue;
            text += "\n" + fromUtf8(outcome.action_id) + ": " + fromUtf8(outcome.message);
        }
        text += undo.complete()
                    ? "\n\nEvery journaled change was reversed."
                    : "\n\nThe journal was kept: some entries could not be reversed safely.";
        set_status("Undo finished: " + format_count(undo.undone) + " restored, " +
                   format_count(undo.failed) + " failed, " + format_count(undo.skipped) + " skipped.");
        wxMessageBox(text, "Undo result",
                     wxOK | (undo.complete() ? wxICON_INFORMATION : wxICON_WARNING), this);
        mark_results_stale("Filesystem state changed during undo. Scan again to refresh results.");
        return;
    }

    // Recover
    if (!result.recovery) {
        set_status("Could not inspect the journal.");
        wxMessageBox("The journal could not be inspected:\n" + fromUtf8(result.error),
                     "Inspection failed", wxOK | wxICON_ERROR, this);
        return;
    }
    const RecoveryReport& recovery = *result.recovery;
    if (recovery.findings.empty()) {
        set_status("No interrupted operations were found.");
        wxMessageBox("No interrupted operations were found. The journal and the filesystem agree, "
                     "so there is nothing to repair.",
                     "Nothing to repair", wxOK | wxICON_INFORMATION, this);
        return;
    }
    wxString text = recovery.clean()
                        ? "Interrupted operations were found, but each one can be retried safely:\n"
                        : "These operations were interrupted and need a decision:\n";
    for (const auto& finding : recovery.findings) {
        text += "\n" + fromUtf8(finding.record.action_id) + " (" + fromUtf8(finding.record.kind) +
                ")\n  " + wxFromPath(finding.record.source) + "\n  -> " +
                wxFromPath(finding.record.destination) + "\n  " + fromUtf8(finding.explanation);
    }
    set_status(format_count(recovery.needs_attention) + " interrupted operation(s) need review.");
    wxMessageBox(text, "Interrupted work", wxOK | wxICON_WARNING, this);
}

} // namespace gui
} // namespace dupli
