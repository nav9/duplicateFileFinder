#include "dupli/gui/useless_files_tab.hpp"

#include "dupli/gui/async_task.hpp"
#include "dupli/gui/file_dialog_helper.hpp"
#include "dupli/gui/operation_thread.hpp"
#include "dupli/gui/plan_dialog.hpp"
#include "dupli/gui/settings.hpp"
#include "dupli/gui/wx_utils.hpp"
#include "dupli/real_filesystem.hpp"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>

#include <algorithm>
#include <utility>

namespace dupli {
namespace gui {

UselessFilesTab::UselessFilesTab(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL) {
    SetBackgroundColour(wxColour(45, 45, 48));
    SetScrollRate(0, 12);
    create_controls();
    reload_settings();
    update_controls();
}

void UselessFilesTab::reload_settings() {
    Settings& settings = Settings::getInstance();
    m_customPatterns->SetValue(fromUtf8(settings.useless_custom_patterns));
    m_includeHidden->SetValue(settings.show_hidden_entries);
}

UselessFilesTab::~UselessFilesTab() {
    if (m_task) {
        m_task->Wait();
        delete m_task;
        m_task = nullptr;
    }
}

void UselessFilesTab::create_controls() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(
        this, wxID_ANY, "Useless Files - find generated cache files and move them to quarantine");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(235, 235, 240));
    outer->Add(title, 0, wxALL, 8);

    auto* explanation = new wxStaticText(
        this, wxID_ANY,
        "Patterns match the file name only; they are globs, never shell commands or regular "
        "expressions. Nothing is deleted: the files you tick are moved to the quarantine folder "
        "you choose, preserving their relative paths, and every move is journaled so it can be "
        "undone from the Task History tab. Files that changed or cannot be read are skipped.");
    explanation->SetForegroundColour(wxColour(200, 200, 205));
    explanation->Wrap(1000);
    outer->Add(explanation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    // --- Folders ---------------------------------------------------------
    auto* foldersBox = new wxStaticBoxSizer(wxVERTICAL, this, "Folders to search");
    auto* foldersParent = foldersBox->GetStaticBox();
    m_roots = new wxListBox(foldersParent, wxID_ANY, wxDefaultPosition, wxSize(-1, 90));
    m_roots->SetBackgroundColour(wxColour(28, 28, 30));
    m_roots->SetForegroundColour(wxColour(225, 225, 228));
    foldersBox->Add(m_roots, 1, wxEXPAND | wxALL, 4);
    auto* folderButtons = new wxBoxSizer(wxHORIZONTAL);
    m_addRoot = new wxButton(foldersParent, wxID_ANY, "Add folder...");
    m_removeRoot = new wxButton(foldersParent, wxID_ANY, "Remove");
    m_clearRoots = new wxButton(foldersParent, wxID_ANY, "Clear");
    folderButtons->Add(m_addRoot, 0, wxALL, 2);
    folderButtons->Add(m_removeRoot, 0, wxALL, 2);
    folderButtons->Add(m_clearRoots, 0, wxALL, 2);
    m_includeHidden = new wxCheckBox(foldersParent, wxID_ANY, "Include hidden files and folders");
    m_includeHidden->SetValue(true);
    m_crossBoundaries = new wxCheckBox(foldersParent, wxID_ANY,
                                       "Cross filesystem/mount boundaries");
    folderButtons->Add(m_includeHidden, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    folderButtons->Add(m_crossBoundaries, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    foldersBox->Add(folderButtons, 0, wxEXPAND, 2);
    outer->Add(foldersBox, 1, wxEXPAND | wxALL, 8);

    // --- Patterns --------------------------------------------------------
    auto* patternsBox = new wxStaticBoxSizer(wxVERTICAL, this, "What counts as a useless file");
    auto* patternsParent = patternsBox->GetStaticBox();
    const auto& presets = builtin_useless_patterns();

    // Four OS-labelled columns so a folder copied from another OS is handled
    // too. Every preset is opt-in by checkbox, and risky metadata is unchecked.
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    const UselessPatternGroup groups[] = {UselessPatternGroup::Windows,
                                         UselessPatternGroup::MacOs,
                                         UselessPatternGroup::Linux,
                                         UselessPatternGroup::CrossPlatform};
    for (UselessPatternGroup group : groups) {
        auto* column = new wxStaticBoxSizer(wxVERTICAL, patternsParent, fromUtf8(to_string(group)));
        for (const auto& preset : presets) {
            if (preset.group != group) continue;
            auto* check = new wxCheckBox(column->GetStaticBox(), wxID_ANY,
                                         fromUtf8(preset.pattern + "  -  " + preset.label));
            check->SetValue(preset.default_enabled);
            wxString tooltip = fromUtf8(preset.description);
            if (!preset.warning.empty()) tooltip += "\n\nCAUTION: " + fromUtf8(preset.warning);
            check->SetToolTip(tooltip);
            column->Add(check, 0, wxALL, 2);
            m_presetChecks.push_back(check);
        }
        columns->Add(column, 1, wxEXPAND | wxALL, 2);
    }
    patternsBox->Add(columns, 0, wxEXPAND, 2);

    auto* customRow = new wxBoxSizer(wxHORIZONTAL);
    customRow->Add(new wxStaticText(patternsParent, wxID_ANY, "Custom patterns (comma separated):"),
                   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_customPatterns = new wxTextCtrl(patternsParent, wxID_ANY);
    m_customPatterns->SetHint("e.g. *.log, mytemp*, .cache-*");
    customRow->Add(m_customPatterns, 1, wxALIGN_CENTER_VERTICAL);
    auto* patternHelp = new wxButton(patternsParent, wxID_ANY, "Pattern help");
    customRow->Add(patternHelp, 0, wxLEFT, 6);
    patternsBox->Add(customRow, 0, wxEXPAND | wxALL, 4);
    outer->Add(patternsBox, 0, wxEXPAND | wxALL, 8);

    // --- Destination and actions -----------------------------------------
    auto* destinationBox = new wxStaticBoxSizer(wxVERTICAL, this, "Quarantine folder");
    auto* destinationParent = destinationBox->GetStaticBox();
    auto* destinationRow = new wxBoxSizer(wxHORIZONTAL);
    m_destination = new wxTextCtrl(destinationParent, wxID_ANY);
    m_destination->SetHint("Files are moved here; pick a folder outside the scanned roots");
    m_browseDestination = new wxButton(destinationParent, wxID_ANY, "Browse...");
    destinationRow->Add(m_destination, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    destinationRow->Add(m_browseDestination, 0, wxALIGN_CENTER_VERTICAL);
    destinationBox->Add(destinationRow, 0, wxEXPAND | wxALL, 4);
    destinationBox->Add(
        new wxStaticText(destinationParent, wxID_ANY,
                         "The destination is always excluded from the scan, so moved files are "
                         "never picked up again in the same run."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
    outer->Add(destinationBox, 0, wxEXPAND | wxALL, 8);

    auto* actionRow = new wxBoxSizer(wxHORIZONTAL);
    m_scan = new wxButton(this, wxID_ANY, "Scan folders");
    m_cancel = new wxButton(this, wxID_ANY, "Cancel");
    m_selectAll = new wxButton(this, wxID_ANY, "Select all");
    m_selectNone = new wxButton(this, wxID_ANY, "Select none");
    m_quarantine = new wxButton(this, wxID_ANY, "Quarantine selected...");
    m_undo = new wxButton(this, wxID_ANY, "Undo journaled moves...");
    m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(140, -1));
    actionRow->Add(m_scan, 0, wxALL, 3);
    actionRow->Add(m_cancel, 0, wxALL, 3);
    actionRow->Add(m_selectAll, 0, wxALL, 3);
    actionRow->Add(m_selectNone, 0, wxALL, 3);
    actionRow->Add(m_quarantine, 0, wxALL, 3);
    actionRow->Add(m_undo, 0, wxALL, 3);
    actionRow->Add(m_gauge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    outer->Add(actionRow, 0, wxLEFT | wxRIGHT, 6);

    m_status = new wxStaticText(this, wxID_ANY, "Ready. Choose folders and press Scan.");
    m_status->SetForegroundColour(wxColour(205, 205, 210));
    outer->Add(m_status, 0, wxALL, 8);

    m_summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_summary->SetForegroundColour(wxColour(200, 200, 205));
    outer->Add(m_summary, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_matches = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 260));
    m_matches->SetBackgroundColour(wxColour(28, 28, 30));
    m_matches->SetForegroundColour(wxColour(225, 225, 228));
    m_matches->SetToolTip("Tick the files you want moved. Nothing is selected automatically.");
    outer->Add(m_matches, 1, wxEXPAND | wxALL, 8);

    SetSizer(outer);
    // Defer the virtual-size calculation until GTK has allocated the page; doing
    // it during construction yields zero-size scrollbars and GTK warnings.
    CallAfter([this] { FitInside(); });

    m_addRoot->Bind(wxEVT_BUTTON, &UselessFilesTab::on_add_root, this);
    m_removeRoot->Bind(wxEVT_BUTTON, &UselessFilesTab::on_remove_root, this);
    m_clearRoots->Bind(wxEVT_BUTTON, &UselessFilesTab::on_clear_roots, this);
    m_browseDestination->Bind(wxEVT_BUTTON, &UselessFilesTab::on_browse_destination, this);
    m_scan->Bind(wxEVT_BUTTON, &UselessFilesTab::on_scan, this);
    m_cancel->Bind(wxEVT_BUTTON, &UselessFilesTab::on_cancel, this);
    m_selectAll->Bind(wxEVT_BUTTON, &UselessFilesTab::on_select_all, this);
    m_selectNone->Bind(wxEVT_BUTTON, &UselessFilesTab::on_select_none, this);
    m_quarantine->Bind(wxEVT_BUTTON, &UselessFilesTab::on_quarantine, this);
    m_undo->Bind(wxEVT_BUTTON, &UselessFilesTab::on_undo, this);
    patternHelp->Bind(wxEVT_BUTTON, &UselessFilesTab::on_pattern_help, this);
    Bind(EVT_DUPLI_ASYNC_DONE, &UselessFilesTab::on_task_done, this);
    Bind(EVT_DUPLI_OPERATION_DONE, &UselessFilesTab::on_operation_done, this);
    m_matches->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent&) { update_summary(); });
    m_roots->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { update_controls(); });
}

std::vector<std::filesystem::path> UselessFilesTab::roots() const {
    std::vector<std::filesystem::path> result;
    for (unsigned int index = 0; index < m_roots->GetCount(); ++index) {
        result.push_back(pathFromWx(m_roots->GetString(index)));
    }
    return result;
}

std::vector<std::string> UselessFilesTab::selected_patterns() const {
    std::vector<std::string> patterns;
    const auto& presets = builtin_useless_patterns();
    for (size_t index = 0; index < m_presetChecks.size() && index < presets.size(); ++index) {
        if (m_presetChecks[index]->IsChecked()) patterns.push_back(presets[index].pattern);
    }
    const auto custom = parse_useless_pattern_list(toUtf8(m_customPatterns->GetValue()));
    patterns.insert(patterns.end(), custom.patterns.begin(), custom.patterns.end());
    // Duplicate patterns (a preset repeated in the custom list) are harmless but
    // would produce duplicate reasons and duplicate matches, so fold them.
    std::vector<std::string> unique;
    for (const auto& pattern : patterns) {
        if (std::find(unique.begin(), unique.end(), pattern) == unique.end()) {
            unique.push_back(pattern);
        }
    }
    return unique;
}

void UselessFilesTab::update_controls() {
    const bool idle = !m_busy;
    m_addRoot->Enable(idle);
    m_removeRoot->Enable(idle && m_roots->GetSelection() != wxNOT_FOUND);
    m_clearRoots->Enable(idle && m_roots->GetCount() > 0);
    m_destination->Enable(idle);
    m_browseDestination->Enable(idle);
    m_includeHidden->Enable(idle);
    m_crossBoundaries->Enable(idle);
    m_customPatterns->Enable(idle);
    for (auto* check : m_presetChecks) check->Enable(idle);
    m_scan->Enable(idle && m_roots->GetCount() > 0);
    m_cancel->Enable(m_busy);
    m_selectAll->Enable(idle && m_matches->GetCount() > 0);
    m_selectNone->Enable(idle && m_matches->GetCount() > 0);
    m_quarantine->Enable(idle && m_matches->GetCount() > 0);
    m_undo->Enable(idle);
    m_roots->Enable(idle);
}

void UselessFilesTab::set_status(const wxString& text, bool error) {
    m_status->SetLabel(text);
    m_status->SetForegroundColour(error ? wxColour(250, 150, 150) : wxColour(205, 205, 210));
}

void UselessFilesTab::on_add_root(wxCommandEvent&) {
    const std::string path = FileDialogHelper::ShowBrowseFolderDialog(this, "Choose a folder to search");
    if (path.empty()) return;
    for (unsigned int index = 0; index < m_roots->GetCount(); ++index) {
        if (pathFromWx(m_roots->GetString(index)) == pathFromWx(fromUtf8(path))) {
            wxMessageBox("That folder is already in the list.", "Already added",
                         wxOK | wxICON_INFORMATION, this);
            return;
        }
    }
    m_roots->Append(fromUtf8(path));
    update_controls();
}

void UselessFilesTab::on_remove_root(wxCommandEvent&) {
    const int selection = m_roots->GetSelection();
    if (selection == wxNOT_FOUND) return;
    m_roots->Delete(static_cast<unsigned int>(selection));
    update_controls();
}

void UselessFilesTab::on_clear_roots(wxCommandEvent&) {
    m_roots->Clear();
    update_controls();
}

void UselessFilesTab::on_browse_destination(wxCommandEvent&) {
    const std::string path =
        FileDialogHelper::ShowBrowseFolderDialog(this, "Choose the quarantine folder");
    if (path.empty()) return;
    m_destination->SetValue(fromUtf8(path));
}

void UselessFilesTab::on_pattern_help(wxCommandEvent&) {
    wxMessageBox(
        "Patterns match the file name, not the path, and are not shell commands:\n\n"
        "  Thumbs.db     an exact name\n"
        "  .bashrc       a hidden name (the leading dot is part of the name)\n"
        "  *.tmp         any name ending in .tmp\n"
        "  .bash*        any name starting with .bash\n"
        "  *_cache       any name ending with _cache\n"
        "  a?c           exactly one character between a and c\n\n"
        "Commas separate patterns. Literal commas cannot be escaped in this version. "
        "Matching ignores ASCII letter case by default, because the same presets must work on "
        "Windows/macOS filesystems. A pattern containing a path separator is rejected: use the "
        "folder list to limit where the scan looks. Symlinks and reparse points are never "
        "traversed or moved.",
        "Pattern syntax", wxOK | wxICON_INFORMATION, this);
}

void UselessFilesTab::on_scan(wxCommandEvent&) {
    if (m_busy) return;
    if (roots().empty()) {
        wxMessageBox("Add at least one folder to search.", "No folders", wxOK | wxICON_INFORMATION,
                     this);
        return;
    }
    const auto custom = parse_useless_pattern_list(toUtf8(m_customPatterns->GetValue()));
    if (!custom.problems.empty()) {
        wxString text = "These custom patterns are invalid and were not used:\n";
        for (const auto& problem : custom.problems) text += "\n• " + fromUtf8(problem);
        wxMessageBox(text, "Invalid patterns", wxOK | wxICON_ERROR, this);
        return;
    }
    const auto patterns = selected_patterns();
    if (patterns.empty()) {
        wxMessageBox("Tick at least one preset or enter a custom pattern.", "No patterns",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    ScanOptions options;
    options.roots = roots();
    options.include_hidden = m_includeHidden->IsChecked();
    options.cross_filesystem_boundaries = m_crossBoundaries->IsChecked();
    const std::filesystem::path destination = pathFromWx(
        m_destination->GetValue().Strip(wxString::both));
    if (!destination.empty()) options.excludes.push_back(destination);
    options.max_entries = 500000;

    auto outcome = std::make_shared<ScanOutcome>();
    m_outcome = outcome;
    m_pending = Pending::Scan;
    m_cancelFlag->store(false);
    m_busy = true;
    set_status("Scanning for matching files...");
    m_gauge->Pulse();
    update_controls();

    auto cancel = m_cancelFlag;
    m_task = new AsyncTask(this, [outcome, options, patterns, cancel] {
        RealFileSystem fs;
        UselessFileScanner scanner(fs);
        auto scanned = scanner.scan(options, patterns);
        if (cancel && cancel->load()) {
            outcome->cancelled = true;
            return;
        }
        if (scanned.has_error()) {
            outcome->error = scanned.error().message;
            return;
        }
        outcome->result = std::move(scanned.value());
        outcome->ok = true;
    });
    if (m_task->Run() != wxTHREAD_NO_ERROR) {
        delete m_task;
        m_task = nullptr;
        m_busy = false;
        m_pending = Pending::None;
        set_status("Could not start a background worker.", true);
        update_controls();
    }
}

void UselessFilesTab::on_cancel(wxCommandEvent&) {
    if (!m_busy) return;
    m_cancelFlag->store(true);
    set_status("Cancelling... the current folder traversal must finish before the scan stops.");
}

void UselessFilesTab::on_task_done(wxThreadEvent&) {
    if (m_task) {
        m_task->Wait();
        const std::string error = m_task->error();
        delete m_task;
        m_task = nullptr;
        if (!error.empty()) {
            if (m_pending == Pending::Scan && m_outcome && m_outcome->error.empty()) {
                m_outcome->error = error;
            }
            if (m_pending == Pending::Plan && m_planOutcome && m_planOutcome->error.empty()) {
                m_planOutcome->error = error;
            }
        }
    }
    const Pending pending = m_pending;
    m_pending = Pending::None;
    m_busy = false;
    update_controls();

    if (pending == Pending::Scan) {
        if (!m_outcome) return;
        if (m_outcome->cancelled) {
            set_status("Scan cancelled. Nothing was changed.");
            return;
        }
        if (!m_outcome->ok) {
            set_status("Scan failed: " + fromUtf8(m_outcome->error), true);
            wxMessageBox("The folder scan failed:\n" + fromUtf8(m_outcome->error), "Scan failed",
                         wxOK | wxICON_ERROR, this);
            return;
        }
        apply_scan();
        return;
    }

    if (pending == Pending::Plan) {
        if (!m_planOutcome) return;
        if (!m_planOutcome->ok || !m_planOutcome->plan) {
            set_status("The quarantine plan could not be built.", true);
            wxMessageBox("The plan could not be built:\n" + fromUtf8(m_planOutcome->error),
                         "Planning failed", wxOK | wxICON_ERROR, this);
            return;
        }
        PlanDialog dialog(
            this, m_planOutcome->plan,
            "After applying, use \"Undo journaled moves...\" or the Task History tab to move the "
            "files back. Undo refuses to overwrite a file that appeared at an original path in "
            "the meantime.");
        if (dialog.ShowModal() != wxID_OK || !dialog.applyRequested()) {
            set_status("Plan closed without applying it. Nothing was changed.");
            return;
        }

        OperationRequest request;
        request.kind = OperationKind::Execute;
        request.plan = m_planOutcome->plan;
        request.approvals = dialog.approvals();
        request.journal_path = Settings::getInstance().journalPath();
        m_busy = true;
        m_gauge->Pulse();
        set_status("Applying the plan: re-verifying each file before moving it...");
        update_controls();
        auto* thread = new OperationThread(this, std::move(request));
        if (thread->Run() != wxTHREAD_NO_ERROR) {
            delete thread;
            m_busy = false;
            set_status("Could not start the background executor; nothing was changed.", true);
            update_controls();
        }
    }
}

void UselessFilesTab::apply_scan() {
    m_matches->Clear();
    if (!m_outcome) return;
    const auto& result = m_outcome->result;
    for (const auto& match : result.matches) {
        wxString label = wxFromPath(match.entry.path) + "  -  " +
                         format_bytes(match.entry.size) + "  -  " + fromUtf8(match.reason);
        m_matches->Append(label);
    }
    update_summary();
    update_controls();

    wxString status = format_count(result.matches.size()) + " matching file(s) in " +
                      format_bytes(result.total_bytes) + ".";
    if (!result.complete) {
        status += " The scan was NOT complete; some folders could not be read.";
    }
    for (const auto& warning : result.warnings) status += " " + fromUtf8(warning);
    set_status(status, !result.complete);
}

void UselessFilesTab::update_summary() {
    if (!m_outcome) {
        m_summary->SetLabel(wxEmptyString);
        return;
    }
    size_t checked = 0;
    uintmax_t bytes = 0;
    const auto& matches = m_outcome->result.matches;
    for (unsigned int index = 0; index < m_matches->GetCount(); ++index) {
        if (!m_matches->IsChecked(index)) continue;
        ++checked;
        if (index < matches.size()) bytes += matches[index].entry.size;
    }
    m_summary->SetLabel(wxString::Format("%zu of %zu file(s) selected (%s).", checked,
                                         m_matches->GetCount(), format_bytes(bytes)));
}

void UselessFilesTab::on_select_all(wxCommandEvent&) {
    for (unsigned int index = 0; index < m_matches->GetCount(); ++index) {
        m_matches->Check(index, true);
    }
    update_summary();
}

void UselessFilesTab::on_select_none(wxCommandEvent&) {
    for (unsigned int index = 0; index < m_matches->GetCount(); ++index) {
        m_matches->Check(index, false);
    }
    update_summary();
}

void UselessFilesTab::on_quarantine(wxCommandEvent&) {
    if (m_busy) return;
    if (!m_outcome) return;
    const std::filesystem::path destination =
        pathFromWx(m_destination->GetValue().Strip(wxString::both));
    if (destination.empty()) {
        wxMessageBox("Choose the quarantine folder first.\n\nFiles are moved there, never deleted.",
                     "Destination required", wxOK | wxICON_INFORMATION, this);
        return;
    }

    std::vector<std::filesystem::path> sources;
    uintmax_t bytes = 0;
    const auto& matches = m_outcome->result.matches;
    for (unsigned int index = 0; index < m_matches->GetCount(); ++index) {
        if (!m_matches->IsChecked(index) || index >= matches.size()) continue;
        sources.push_back(matches[index].entry.path);
        bytes += matches[index].entry.size;
    }
    if (sources.empty()) {
        wxMessageBox("Tick the files you want to move.", "Nothing selected",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    const wxString question =
        "Move " + format_count(sources.size()) + " file(s) (" + format_bytes(bytes) +
        ") to:\n" + wxFromPath(destination) +
        "\n\nNothing is deleted. Relative paths are preserved and an existing file is never "
        "overwritten. Each file is re-read immediately before the move; a changed file is "
        "skipped and reported.\n\nContinue to the plan preview?";
    if (wxMessageBox(question, "Quarantine files", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION,
                     this) != wxYES) {
        return;
    }

    UselessMoveRequest request;
    request.sources = sources;
    request.destination = destination;
    request.roots = roots();
    request.scan_roots = request.roots;

    auto outcome = std::make_shared<PlanOutcome>();
    m_planOutcome = outcome;
    m_pending = Pending::Plan;
    m_busy = true;
    set_status("Verifying the selected files and building the plan...");
    m_gauge->Pulse();
    update_controls();

    m_task = new AsyncTask(this, [outcome, request] {
        RealFileSystem fs;
        Blake3Hasher hasher;
        UselessFilePlanner planner(fs, hasher);
        auto planned = planner.plan_moves(request);
        if (planned.has_error()) {
            outcome->error = planned.error().message;
            return;
        }
        outcome->plan = std::make_shared<ActionPlan>(std::move(planned.value()));
        outcome->ok = true;
    });
    if (m_task->Run() != wxTHREAD_NO_ERROR) {
        delete m_task;
        m_task = nullptr;
        m_busy = false;
        m_pending = Pending::None;
        set_status("Could not start a background worker.", true);
        update_controls();
    }
}

void UselessFilesTab::on_undo(wxCommandEvent&) {
    if (m_busy) return;
    const wxString question =
        "Reverse every change recorded in the journal, newest first?\n\n"
        "This also affects moves made from the Duplicate Files tab. Each file is re-verified "
        "first; an occupied original path is never overwritten, and entries that cannot be "
        "reversed safely are skipped and reported.";
    if (wxMessageBox(question, "Undo journaled moves",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        return;
    }
    OperationRequest request;
    request.kind = OperationKind::Undo;
    request.journal_path = Settings::getInstance().journalPath();
    m_busy = true;
    set_status("Undoing journaled moves...");
    update_controls();
    auto* thread = new OperationThread(this, std::move(request));
    if (thread->Run() != wxTHREAD_NO_ERROR) {
        delete thread;
        m_busy = false;
        set_status("Could not start the background worker.", true);
        update_controls();
    }
}

void UselessFilesTab::on_operation_done(wxThreadEvent& event) {
    auto payload = event.GetPayload<std::shared_ptr<OperationResult>>();
    m_busy = false;
    m_gauge->SetValue(0);
    update_controls();
    if (!payload) return;
    const OperationResult& result = *payload;

    if (result.kind == OperationKind::Execute) {
        if (!result.execution) {
            set_status("The plan was not applied.", true);
            wxMessageBox("The plan was not applied:\n" + fromUtf8(result.error),
                         "Nothing was changed", wxOK | wxICON_ERROR, this);
            return;
        }
        const ExecutionReport& execution = *result.execution;
        wxString text = "Succeeded: " + format_count(execution.succeeded_count()) +
                        "\nFailed: " + format_count(execution.failed_count()) +
                        "\nSkipped: " + format_count(execution.skipped_count()) + "\n";
        for (const auto& path : execution.sources_retained) {
            text += "\nSource kept (other filesystem): " + fromUtf8(path);
        }
        for (const auto& error : execution.errors) text += "\nError: " + fromUtf8(error);
        for (const auto& warning : execution.warnings) text += "\nWarning: " + fromUtf8(warning);
        if (execution.recovery_required) {
            text += "\n\nRECOVERY REQUIRED: inspect the Task History tab before retrying.";
        }
        text += "\n\nJournal: " + fromUtf8(Settings::getInstance().journalPath()) +
                "\nUse \"Undo journaled moves...\" to reverse this.";
        set_status(wxString::Format("Quarantine %s: %zu succeeded, %zu failed, %zu skipped.",
                                    execution.success ? "completed" : "partially completed",
                                    execution.succeeded_count(), execution.failed_count(),
                                    execution.skipped_count()),
                   !execution.success);
        wxMessageBox(text, "Quarantine result",
                     wxOK | (execution.success ? wxICON_INFORMATION : wxICON_WARNING), this);
        // The listed files no longer match the disk, so the results are cleared
        // instead of being left as a misleading "still there" list.
        m_matches->Clear();
        m_outcome.reset();
        update_summary();
        update_controls();
        return;
    }

    if (result.kind == OperationKind::Undo) {
        if (!result.undo) {
            set_status("Undo failed: " + fromUtf8(result.error), true);
            return;
        }
        wxString text = "Restored: " + format_count(result.undo->undone) +
                        "\nFailed: " + format_count(result.undo->failed) +
                        "\nSkipped: " + format_count(result.undo->skipped) + "\n";
        for (const auto& outcome : result.undo->outcomes) {
            if (!outcome.message.empty()) text += "\n" + fromUtf8(outcome.message);
        }
        set_status("Undo finished.", !result.undo->complete());
        wxMessageBox(text, "Undo result", wxOK | wxICON_INFORMATION, this);
        m_matches->Clear();
        m_outcome.reset();
        update_summary();
        update_controls();
    }
}

} // namespace gui
} // namespace dupli
