#pragma once

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/gauge.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "dupli/gui/operation_thread.hpp"
#include "dupli/gui/result_view.hpp"

namespace dupli {
namespace gui {

// The "Duplicate Files" workbench: choose folders, scan, review the findings,
// preview a plan, apply it, and undo or inspect journaled work.
//
// Threading: every scan/plan/apply/undo/recover runs on an OperationThread. The
// widget code only ever runs on the UI thread; results arrive as one event that
// carries immutable shared_ptr payloads. The tab can therefore keep working
// while the user switches tabs.
class ScanTab : public wxPanel {
public:
    explicit ScanTab(wxWindow* parent);
    ~ScanTab() override;

    // True while a background operation owns the shared state. The main frame
    // refuses to close the window in that state rather than destroying widgets
    // a worker outcome is still going to reference.
    bool is_busy() const { return m_busy; }

    // Re-read saved settings (called after the Settings tab saves).
    void reload_settings();

private:
    void create_controls();
    wxWindow* create_input_panel(wxWindow* parent);
    wxWindow* create_plan_panel(wxWindow* parent);

    void on_add_folder(wxCommandEvent& event);
    void on_remove_folder(wxCommandEvent& event);
    void on_clear_folders(wxCommandEvent& event);
    void on_add_exclude(wxCommandEvent& event);
    void on_remove_exclude(wxCommandEvent& event);
    void on_scan(wxCommandEvent& event);
    void on_cancel(wxCommandEvent& event);
    void on_preview_plan(wxCommandEvent& event);
    void on_save_report(wxCommandEvent& event);
    void on_undo_journal(wxCommandEvent& event);
    void on_inspect_journal(wxCommandEvent& event);
    void on_mode_changed(wxCommandEvent& event);
    void on_browse_destination(wxCommandEvent& event);
    void on_operation_done(wxThreadEvent& event);
    void on_pulse(wxTimerEvent& event);

    void start_operation(OperationRequest request, const wxString& status, bool cancellable);
    void finish_operation();
    void update_controls();
    std::vector<std::filesystem::path> roots() const;
    std::filesystem::path destination() const;
    ActionMode selected_mode() const;
    void update_plan_hint();
    void set_status(const wxString& text);
    void mark_results_stale(const wxString& reason);

    // Inputs
    wxListBox* m_roots = nullptr;
    wxListBox* m_excludes = nullptr;
    wxCheckBox* m_hidden = nullptr;
    wxCheckBox* m_crossBoundaries = nullptr;
    wxCheckBox* m_compareTrees = nullptr;
    wxButton* m_addFolder = nullptr;
    wxButton* m_removeFolder = nullptr;
    wxButton* m_clearFolders = nullptr;
    wxButton* m_addExclude = nullptr;
    wxButton* m_removeExclude = nullptr;

    // Action bar
    wxButton* m_scanButton = nullptr;
    wxButton* m_cancelButton = nullptr;
    wxButton* m_saveReportButton = nullptr;
    wxGauge* m_gauge = nullptr;
    wxStaticText* m_status = nullptr;

    // Plan controls
    wxChoice* m_mode = nullptr;
    wxTextCtrl* m_destination = nullptr;
    wxButton* m_browseDestination = nullptr;
    wxButton* m_previewButton = nullptr;
    wxButton* m_undoButton = nullptr;
    wxButton* m_inspectButton = nullptr;
    wxStaticText* m_planHint = nullptr;
    wxStaticText* m_journalPathText = nullptr;

    // Results
    ResultView* m_resultView = nullptr;

    // Operation state
    OperationThread* m_thread = nullptr;
    bool m_busy = false;
    bool m_cancellable = false;
    bool m_results_stale = false;
    wxTimer m_pulseTimer;

    std::shared_ptr<ScanResult> m_scan;
    std::shared_ptr<ClassificationResult> m_classification;
    std::shared_ptr<Report> m_report;
};

} // namespace gui
} // namespace dupli
