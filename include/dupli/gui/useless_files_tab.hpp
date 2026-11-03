#pragma once

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "dupli/useless_files.hpp"

namespace dupli {
namespace gui {

class AsyncTask;

// Useless Files: find OS-generated cache/metadata files and move the ones the
// user ticks into a quarantine folder.
//
// Safety properties enforced here (and by the backend):
//  * Only files matching a name pattern are offered; symlinks are never followed
//    or moved.
//  * Nothing is deleted. The selected files go through the shared planner,
//    validator and executor, so each source is re-hashed before the move and the
//    move is journaled for undo.
//  * Potentially useful metadata (Desktop.ini, AppleDouble "._*" sidecars,
//    "*.tmp") is never selected by default and explains its risk.
class UselessFilesTab : public wxScrolledWindow {
public:
    explicit UselessFilesTab(wxWindow* parent);
    ~UselessFilesTab() override;

    bool is_busy() const { return m_busy; }

    /// Restore saved defaults (custom pattern list, hidden-file policy).
    void reload_settings();

private:
    struct ScanOutcome {
        bool ok = false;
        bool cancelled = false;
        std::string error;
        dupli::UselessFileScanResult result;
    };

    struct PlanOutcome {
        bool ok = false;
        std::string error;
        std::shared_ptr<dupli::ActionPlan> plan;
    };

    /// What the single background worker is currently doing. Both operations use
    /// the same completion event; this says how to interpret the result.
    enum class Pending { None, Scan, Plan };

    void create_controls();
    void on_add_root(wxCommandEvent& event);
    void on_remove_root(wxCommandEvent& event);
    void on_clear_roots(wxCommandEvent& event);
    void on_browse_destination(wxCommandEvent& event);
    void on_scan(wxCommandEvent& event);
    void on_cancel(wxCommandEvent& event);
    void on_select_all(wxCommandEvent& event);
    void on_select_none(wxCommandEvent& event);
    void on_quarantine(wxCommandEvent& event);
    void on_undo(wxCommandEvent& event);
    void on_pattern_help(wxCommandEvent& event);
    void on_task_done(wxThreadEvent& event);
    void on_operation_done(wxThreadEvent& event);

    std::vector<std::string> selected_patterns() const;
    std::vector<std::filesystem::path> roots() const;
    void apply_scan();
    void update_summary();
    void update_controls();
    void set_status(const wxString& text, bool error = false);

    wxListBox* m_roots = nullptr;
    wxButton* m_addRoot = nullptr;
    wxButton* m_removeRoot = nullptr;
    wxButton* m_clearRoots = nullptr;
    std::vector<wxCheckBox*> m_presetChecks; // parallel to builtin_useless_patterns()
    wxTextCtrl* m_customPatterns = nullptr;
    wxTextCtrl* m_destination = nullptr;
    wxButton* m_browseDestination = nullptr;
    wxCheckBox* m_includeHidden = nullptr;
    wxCheckBox* m_crossBoundaries = nullptr;
    wxCheckListBox* m_matches = nullptr;
    wxButton* m_scan = nullptr;
    wxButton* m_cancel = nullptr;
    wxButton* m_selectAll = nullptr;
    wxButton* m_selectNone = nullptr;
    wxButton* m_quarantine = nullptr;
    wxButton* m_undo = nullptr;
    wxStaticText* m_status = nullptr;
    wxStaticText* m_summary = nullptr;
    wxGauge* m_gauge = nullptr;

    std::shared_ptr<ScanOutcome> m_outcome;
    std::shared_ptr<PlanOutcome> m_planOutcome;
    Pending m_pending = Pending::None;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag =
        std::make_shared<std::atomic<bool>>(false);
    AsyncTask* m_task = nullptr;
    bool m_busy = false;
};

} // namespace gui
} // namespace dupli
