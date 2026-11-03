#pragma once

#include <wx/wx.h>
#include <wx/notebook.h>

namespace dupli {
namespace gui {

class ScanTab;
class HistoryTab;
class SettingsTab;
class UniqueSentencesTab;
class UselessFilesTab;
class ResizeImagesTab;

// Main window: a horizontal tab row mapping one-to-one onto the program's
// capabilities.
//
//  * Duplicate Files - scan, review, plan, apply (the reading/decision stage)
//  * Unique Sentences - exact sentence de-duplication across text sources
//  * Useless Files    - OS cache/thumbnail file cleanup into a quarantine folder
//  * Resize Images    - copy or in-place image resizing
//  * Task History     - the durable journal, undo and interrupted-work inspection
//  * Settings         - the versioned user configuration
//
// All destructive work happens behind the same core API the CLI uses, on worker
// threads. The frame refuses to close while an operation is running so no worker
// can post a result to a destroyed window.
class DupliMainFrame : public wxFrame {
public:
    explicit DupliMainFrame(const wxString& title);

    bool has_running_task() const;

private:
    void create_menu_bar();
    void create_tabs();
    void on_close(wxCloseEvent& event);
    void on_exit(wxCommandEvent& event);
    void on_about(wxCommandEvent& event);
    void on_usage_help(wxCommandEvent& event);
    void on_open_data_folder(wxCommandEvent& event);
    void on_page_changed(wxBookCtrlEvent& event);

    wxNotebook* m_notebook = nullptr;
    ScanTab* m_scanTab = nullptr;
    UniqueSentencesTab* m_sentencesTab = nullptr;
    UselessFilesTab* m_uselessTab = nullptr;
    ResizeImagesTab* m_resizeTab = nullptr;
    HistoryTab* m_historyTab = nullptr;
    SettingsTab* m_settingsTab = nullptr;
};

} // namespace gui
} // namespace dupli
