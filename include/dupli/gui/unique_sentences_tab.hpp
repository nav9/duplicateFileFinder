#pragma once

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/scrolwin.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "dupli/text_dedup.hpp"

namespace dupli {
namespace gui {

class AsyncTask;

// Unique Sentences: exact de-duplication of sentences across several text files
// and pasted text.
//
// The tab owns no de-duplication logic. It collects sources, shows them back to
// the user for provenance, and calls dupli::deduplicate_sentences() on a worker
// thread. Saving uses dupli::write_text_output(), which writes a temporary file
// and publishes it atomically, so a failed write never leaves a half output.
//
// Sources are never modified: this tab only reads them and creates a new output
// file. That is why undo is not offered for the output (there is nothing to
// restore) - the user can simply delete the generated file.
class UniqueSentencesTab : public wxScrolledWindow {
public:
    explicit UniqueSentencesTab(wxWindow* parent);
    ~UniqueSentencesTab() override;

    bool is_busy() const { return m_busy; }

    /// Re-apply saved defaults after the Settings tab saved. Only the comparison
    /// options are touched; already-loaded sources are unaffected.
    void reload_settings();

private:
    struct SourceEntry {
        std::string label;
        std::filesystem::path path;   // empty for pasted text
        dupli::TextEncoding encoding = dupli::TextEncoding::Auto;
        dupli::TextEncoding detected = dupli::TextEncoding::Utf8;
        std::string text;
    };

    struct PreviewOutcome {
        bool ok = false;
        std::string error;
        dupli::SentenceDedupResult result;
    };

    void create_controls();
    void on_add_files(wxCommandEvent& event);
    void on_add_pasted(wxCommandEvent& event);
    void on_remove(wxCommandEvent& event);
    void on_clear(wxCommandEvent& event);
    void on_preview(wxCommandEvent& event);
    void on_save(wxCommandEvent& event);
    void on_cancel(wxCommandEvent& event);
    void on_task_done(wxThreadEvent& event);
    void on_help(wxCommandEvent& event);

    void add_source(SourceEntry entry);
    void update_controls();
    void apply_results();
    dupli::SentencePolicy policy() const;
    void set_status(const wxString& text, bool error = false);

    wxListBox* m_sources = nullptr;
    wxChoice* m_encoding = nullptr;
    wxCheckBox* m_trim = nullptr;
    wxCheckBox* m_collapse = nullptr;
    wxCheckBox* m_caseInsensitive = nullptr;
    wxButton* m_addFiles = nullptr;
    wxButton* m_addPasted = nullptr;
    wxButton* m_remove = nullptr;
    wxButton* m_clear = nullptr;
    wxButton* m_preview = nullptr;
    wxButton* m_save = nullptr;
    wxButton* m_cancel = nullptr;
    wxListCtrl* m_results = nullptr;
    wxTextCtrl* m_details = nullptr;
    wxStaticText* m_status = nullptr;
    wxStaticText* m_stats = nullptr;
    wxGauge* m_gauge = nullptr;

    std::vector<SourceEntry> m_entries;
    std::shared_ptr<PreviewOutcome> m_outcome;
    /// Cooperative cancellation for the comparison worker. The pure splitter
    /// cannot be interrupted mid-call, so the flag is checked at phase boundaries.
    std::shared_ptr<std::atomic<bool>> m_cancelFlag =
        std::make_shared<std::atomic<bool>>(false);
    AsyncTask* m_task = nullptr;
    bool m_busy = false;
};

} // namespace gui
} // namespace dupli
