#include "dupli/gui/mainframe.hpp"

#include "dupli/gui/history_tab.hpp"
#include "dupli/gui/platform_paths.hpp"
#include "dupli/gui/resize_images_tab.hpp"
#include "dupli/gui/scan_tab.hpp"
#include "dupli/gui/settings.hpp"
#include "dupli/gui/settings_tab.hpp"
#include "dupli/gui/unique_sentences_tab.hpp"
#include "dupli/gui/useless_files_tab.hpp"
#include "dupli/gui/wx_utils.hpp"

#include <wx/msgdlg.h>
#include <wx/statline.h>
#include <wx/utils.h>
#include <wx/version.h>

#include <cstdlib>

namespace dupli {
namespace gui {

namespace {

// Menu command ids start above wxID_HIGHEST so they never collide with the
// standard ids wxWidgets reserves for dialogs and stock buttons.
enum {
    ID_OPEN_DATA_FOLDER = wxID_HIGHEST + 100,
    ID_USAGE_HELP,
};

} // namespace

DupliMainFrame::DupliMainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(1200, 820)) {
    dupli::gui::PlatformPaths::ensure_directories_exist();
    SetBackgroundColour(wxColour(45, 45, 48));
    SetMinSize(wxSize(900, 600));

    create_menu_bar();
    create_tabs();

    CreateStatusBar(2);
    SetStatusText("Ready. Add folders on the Duplicate Files tab to begin.", 0);
    SetStatusText("Journal: " + dupli::gui::fromUtf8(dupli::gui::Settings::getInstance().journalPath()),
                  1);

    Bind(wxEVT_CLOSE_WINDOW, &DupliMainFrame::on_close, this);
    Centre();
}

void DupliMainFrame::create_menu_bar() {
    auto* fileMenu = new wxMenu();
    fileMenu->Append(ID_OPEN_DATA_FOLDER, "Open journal folder",
                     "Open the per-user folder that holds the operation journal");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "Exit", "Close the application");

    auto* helpMenu = new wxMenu();
    helpMenu->Append(ID_USAGE_HELP, "How to use this program",
                     "What each tab does and how the safety checks work");
    helpMenu->Append(wxID_ABOUT, "About", "Version and build information");

    auto* menuBar = new wxMenuBar();
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(helpMenu, "&Help");
    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &DupliMainFrame::on_exit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &DupliMainFrame::on_about, this, wxID_ABOUT);
    Bind(wxEVT_MENU, &DupliMainFrame::on_usage_help, this, ID_USAGE_HELP);
    Bind(wxEVT_MENU, &DupliMainFrame::on_open_data_folder, this, ID_OPEN_DATA_FOLDER);
}

void DupliMainFrame::create_tabs() {
    m_notebook = new wxNotebook(this, wxID_ANY);
    m_notebook->SetBackgroundColour(wxColour(45, 45, 48));

    m_scanTab = new ScanTab(m_notebook);
    m_notebook->AddPage(m_scanTab, "Duplicate Files", true);

    m_sentencesTab = new UniqueSentencesTab(m_notebook);
    m_notebook->AddPage(m_sentencesTab, "Unique Sentences");

    m_uselessTab = new UselessFilesTab(m_notebook);
    m_notebook->AddPage(m_uselessTab, "Useless Files");

    m_resizeTab = new ResizeImagesTab(m_notebook);
    m_notebook->AddPage(m_resizeTab, "Resize Images");

    m_historyTab = new HistoryTab(m_notebook);
    m_notebook->AddPage(m_historyTab, "Task History");

    // Settings changes (scan defaults) are pushed to the scan tab immediately;
    // a scan already running keeps its own snapshot.
    m_settingsTab = new SettingsTab(m_notebook, [this]() {
        if (m_scanTab) m_scanTab->reload_settings();
        if (m_sentencesTab) m_sentencesTab->reload_settings();
        if (m_uselessTab) m_uselessTab->reload_settings();
        if (m_resizeTab) m_resizeTab->reload_settings();
        if (m_historyTab) m_historyTab->reload();
    });
    m_notebook->AddPage(m_settingsTab, "Settings");

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_notebook, 1, wxEXPAND);
    SetSizer(sizer);

    // Development aid: DUPLI_GUI_START_TAB=<index> opens a specific tab, which
    // makes automated screenshot checks of each page possible. It is ignored
    // when unset, so normal startup always shows Duplicate Files.
    if (const char* start = std::getenv("DUPLI_GUI_START_TAB")) {
        const long index = std::strtol(start, nullptr, 10);
        if (index >= 0 && index < static_cast<long>(m_notebook->GetPageCount())) {
            m_notebook->SetSelection(static_cast<size_t>(index));
        }
    }

    m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &DupliMainFrame::on_page_changed, this);
}

bool DupliMainFrame::has_running_task() const {
    return (m_scanTab && m_scanTab->is_busy()) ||
           (m_sentencesTab && m_sentencesTab->is_busy()) ||
           (m_uselessTab && m_uselessTab->is_busy()) ||
           (m_resizeTab && m_resizeTab->is_busy()) ||
           (m_historyTab && m_historyTab->is_busy());
}

void DupliMainFrame::on_page_changed(wxBookCtrlEvent& event) {
    // The journal can change from the scan tab, another program instance, or a
    // previous session, so the history is re-read whenever it becomes visible.
    // Task History is the fifth page. The journal can change from any tab, so it
    // is re-read whenever it becomes visible.
    if (event.GetSelection() == 4 && m_historyTab && !m_historyTab->is_busy()) {
        m_historyTab->reload();
    }
    event.Skip();
}

void DupliMainFrame::on_close(wxCloseEvent& event) {
    if (has_running_task()) {
        // Closing now would destroy the widgets a worker is about to post to.
        // Refusing is safer than terminating a worker mid-mutation.
        wxMessageBox(
            "An operation is still running.\n\n"
            "Wait for it to finish (or cancel it on the Duplicate Files tab) before closing, so "
            "the result is not lost and the journal stays consistent.",
            "Please wait", wxOK | wxICON_INFORMATION, this);
        event.Veto();
        return;
    }
    event.Skip();
}

void DupliMainFrame::on_exit(wxCommandEvent&) { Close(); }

void DupliMainFrame::on_open_data_folder(wxCommandEvent&) {
    const std::filesystem::path directory = dupli::gui::PlatformPaths::get_data_dir();
    if (!wxLaunchDefaultApplication(dupli::gui::wxFromPath(directory))) {
        wxMessageBox("Could not open " + dupli::gui::wxFromPath(directory),
                     "Open folder", wxOK | wxICON_INFORMATION, this);
    }
}

void DupliMainFrame::on_about(wxCommandEvent&) {
    wxString text = "Duplicate Manager\n\n"
                    "Finds duplicate files by complete file content (BLAKE3), previews a plan, "
                    "then moves or copies only what you approve. Every change is journaled and "
                    "can be undone.\n\n"
                    "The same engine is available headlessly as dupli_cli.\n\n";
    text += wxString::Format("wxWidgets %s", wxVERSION_STRING);
    wxMessageBox(text, "About Duplicate Manager", wxOK | wxICON_INFORMATION, this);
}

void DupliMainFrame::on_usage_help(wxCommandEvent&) {
    const wxString text =
        "Duplicate Files tab\n"
        "  1. Add one or more folders (and optionally exclusions).\n"
        "  2. Choose how hidden files, mount boundaries and whole-tree comparison are handled.\n"
        "  3. Press \"Scan and analyse\". Files are compared by their complete content.\n"
        "  4. Review the categories on the right. Nothing is selected for removal for you.\n"
        "  5. Pick an operation and a destination, then press \"Preview plan...\".\n"
        "  6. The preview lists every single source and destination. Only then can it be applied.\n\n"
        "What the operations mean\n"
        "  * Quarantine moves duplicates (one copy always stays behind) to the destination.\n"
        "  * Unique collection copies files whose content appears once; nothing is moved.\n"
        "  * In-place merge is advanced, needs per-action approval, and is not undoable by the "
        "program.\n\n"
        "Safety\n"
        "  * Nothing is deleted, ever.\n"
        "  * Every source is re-hashed immediately before it is touched; a changed file is refused.\n"
        "  * An existing destination file is never overwritten; a free name is chosen instead.\n"
        "  * Files on another filesystem are copied, verified, and the source is kept.\n"
        "  * Incomplete scans never claim that a file is unique.\n\n"
        "Task History tab\n"
        "  * Shows the journal, newest first, and lets you reverse journaled changes.\n"
        "  * \"Check interrupted work\" compares the journal with the disk after a crash.\n\n"
        "Unique Sentences tab\n"
        "  * Add text files or pasted text; each source keeps a label you can remove.\n"
        "  * Preview shows every sentence, whether it is kept or a duplicate, and where the kept\n"
        "    copy came from. Comparison is exact text; case and punctuation matter unless you\n"
        "    change the options.\n"
        "  * Saving writes a new file atomically (temporary file, then rename) and verifies it.\n"
        "    Source files are never modified.\n\n"
        "Useless Files tab\n"
        "  * Tick OS presets and/or enter glob patterns (file names only, never shell commands).\n"
        "  * Scanning lists every match with its size and why it matched; nothing is selected for\n"
        "    you. Desktop.ini, AppleDouble sidecars and *.tmp are off by default because they can\n"
        "    carry real configuration.\n"
        "  * Quarantine moves the ticked files to a folder you choose, preserving relative paths,\n"
        "    never overwriting, and journaling every move so it can be undone.\n\n"
        "Resize Images tab\n"
        "  * Find images below a folder, preview original vs resized, then resize the ticked ones.\n"
        "  * Copies are the default; originals stay untouched. Resize in place needs a typed\n"
        "    confirmation and keeps a verified \".dupli-backup\" of every original.\n"
        "  * Animated/multi-page images and formats the codec cannot read are skipped with an\n"
        "    explanation, never silently flattened.\n\n"
        "Settings tab\n"
        "  * Saved atomically to your user configuration folder. Only new tasks use new values.";
    wxMessageBox(text, "How to use Duplicate Manager", wxOK | wxICON_INFORMATION, this);
}

} // namespace gui
} // namespace dupli
