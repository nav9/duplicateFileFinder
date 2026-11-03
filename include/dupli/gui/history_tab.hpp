#pragma once

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <vector>

#include "dupli/journal.hpp"
#include "dupli/gui/operation_thread.hpp"

namespace dupli {
namespace gui {

// Task History: the durable operation journal, rendered as a filterable list.
//
// The journal - not the in-memory task list - is the source of truth, because it
// is what survives a crash and what undo actually operates on. Reading it never
// mutates anything; undo and "check interrupted work" run through the same
// background worker the scan tab uses.
class HistoryTab : public wxPanel {
public:
    explicit HistoryTab(wxWindow* parent);
    ~HistoryTab() override;

    // Re-reads the journal from disk. Safe to call whenever the tab is shown.
    void reload();
    bool is_busy() const { return m_busy; }

private:
    void create_controls();
    void populate();
    void on_refresh(wxCommandEvent& event);
    void on_filter_changed(wxCommandEvent& event);
    void on_selection_changed(wxListEvent& event);
    void on_undo(wxCommandEvent& event);
    void on_inspect(wxCommandEvent& event);
    void on_open_folder(wxCommandEvent& event);
    void on_operation_done(wxThreadEvent& event);
    void start_operation(OperationRequest request, const wxString& status);
    void finish_operation();
    void update_controls();
    bool matches_filter(const JournalRecord& record) const;
    wxString describe(const JournalRecord& record) const;

    wxListCtrl* m_list = nullptr;
    wxTextCtrl* m_details = nullptr;
    wxChoice* m_filter = nullptr;
    wxStaticText* m_status = nullptr;
    wxStaticText* m_journalPath = nullptr;
    wxButton* m_refreshButton = nullptr;
    wxButton* m_undoButton = nullptr;
    wxButton* m_inspectButton = nullptr;
    wxButton* m_openFolderButton = nullptr;

    std::vector<JournalRecord> m_records;
    std::vector<size_t> m_visible; // indexes into m_records, in display order
    OperationThread* m_thread = nullptr;
    bool m_busy = false;
};

} // namespace gui
} // namespace dupli
