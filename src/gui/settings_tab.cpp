#include "dupli/gui/settings_tab.hpp"

#include "dupli/gui/logger.hpp"
#include "dupli/gui/platform_paths.hpp"
#include "dupli/gui/settings.hpp"
#include "dupli/gui/wx_utils.hpp"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/utils.h>

#include <algorithm>
#include <array>
#include <utility>

namespace dupli {
namespace gui {

namespace {

// Hash buffer sizes are bounded on both ends: too small is slow, too large can
// exhaust memory on a constrained machine. The settings loader re-checks these
// bounds, so a hand-edited file cannot bypass them either.
constexpr int kMinBufferSize = 4096;
constexpr int kMaxBufferSize = 16 * 1024 * 1024;

// Only the tail of the log is ever read: a log file can be arbitrarily large and
// loading it whole would freeze the UI or exhaust memory.
constexpr size_t kMaxLogBytesToShow = 512 * 1024;
constexpr size_t kMaxLogLinesToShow = 5000;

std::string ascii_lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

/// Log viewer: bounded read, level and substring filtering, explicit clear.
///
/// Clearing the log never touches the operation journal, which is separate and
/// required for undo; the dialog says so before doing anything.
class LogViewerDialog : public wxDialog {
public:
    explicit LogViewerDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "Application log", wxDefaultPosition, wxSize(920, 620),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        SetBackgroundColour(wxColour(45, 45, 48));
        auto* outer = new wxBoxSizer(wxVERTICAL);
        m_path = new wxStaticText(this, wxID_ANY, wxFromPath(Logger::getInstance().log_path()));
        m_path->SetForegroundColour(wxColour(170, 170, 176));
        outer->Add(m_path, 0, wxALL, 8);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(new wxStaticText(this, wxID_ANY, "Minimum level:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_level = new wxChoice(this, wxID_ANY);
        for (const char* name : {"All", "Trace", "Debug", "Info", "Warning", "Error", "Critical"}) {
            m_level->Append(fromUtf8(name));
        }
        m_level->SetSelection(0);
        row->Add(m_level, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        row->Add(new wxStaticText(this, wxID_ANY, "Contains:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        m_search = new wxTextCtrl(this, wxID_ANY);
        m_search->SetHint("substring filter (case-insensitive)");
        row->Add(m_search, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
        auto* refresh = new wxButton(this, wxID_ANY, "Refresh");
        auto* clear = new wxButton(this, wxID_ANY, "Clear log...");
        row->Add(refresh, 0, wxRIGHT, 4);
        row->Add(clear, 0);
        outer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

        m_view = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
        m_view->SetBackgroundColour(wxColour(24, 24, 26));
        m_view->SetForegroundColour(wxColour(225, 225, 228));
        outer->Add(m_view, 1, wxEXPAND | wxALL, 8);

        m_info = new wxStaticText(this, wxID_ANY, wxEmptyString);
        m_info->SetForegroundColour(wxColour(200, 200, 205));
        outer->Add(m_info, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer(1);
        buttons->Add(new wxButton(this, wxID_CANCEL, "Close"), 0);
        outer->Add(buttons, 0, wxEXPAND | wxALL, 8);
        SetSizer(outer);

        refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh_view(); });
        m_level->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { refresh_view(); });
        m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { refresh_view(); });
        clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            const wxString question =
                "Delete the contents of the application log?\n\nThis does NOT affect the "
                "operation journal, so undo keeps working. Rotated log files are left alone.";
            if (wxMessageBox(question, "Clear log", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                             this) != wxYES) {
                return;
            }
            if (!Logger::getInstance().clear()) {
                wxMessageBox("The log could not be cleared.", "Clear log",
                             wxOK | wxICON_ERROR, this);
            }
            refresh_view();
        });
        refresh_view();
    }

private:
    void refresh_view() {
        const std::string content =
            read_log_tail(Logger::getInstance().log_path(), kMaxLogBytesToShow);
        const int level_index = m_level->GetSelection();
        LogLevel minimum = LogLevel::Trace;
        bool filter_level = level_index > 0;
        if (filter_level) {
            const char* names[] = {"Trace", "Debug", "Info", "Warning", "Error", "Critical"};
            const int index = std::min(level_index - 1, 5);
            parse_log_level(names[index], minimum);
        }
        const std::string needle = ascii_lower(toUtf8(m_search->GetValue().Strip(wxString::both)));

        std::string shown;
        size_t lines = 0;
        size_t total = 0;
        size_t line_start = 0;
        while (line_start <= content.size()) {
            const size_t newline = content.find('\n', line_start);
            const size_t line_end = newline == std::string::npos ? content.size() : newline;
            const std::string line = content.substr(line_start, line_end - line_start);
            if (!line.empty()) {
                ++total;
                bool keep = true;
                if (filter_level) {
                    const size_t open = line.find('[');
                    const size_t close = line.find(']', open == std::string::npos ? 0 : open);
                    LogLevel level = LogLevel::Info;
                    if (open == std::string::npos || close == std::string::npos ||
                        !parse_log_level(line.substr(open + 1, close - open - 1), level)) {
                        keep = false;
                    } else if (static_cast<int>(level) < static_cast<int>(minimum)) {
                        keep = false;
                    }
                }
                if (keep && !needle.empty() && ascii_lower(line).find(needle) == std::string::npos) {
                    keep = false;
                }
                if (keep && lines < kMaxLogLinesToShow) {
                    shown += line;
                    shown += '\n';
                    ++lines;
                }
            }
            if (newline == std::string::npos) break;
            line_start = newline + 1;
        }

        m_view->SetValue(fromUtf8(shown));
        if (!Logger::getInstance().enabled()) {
            m_info->SetLabel("Logging is currently disabled; existing entries are shown. "
                             "The operation journal is unaffected.");
        } else {
            m_info->SetLabel(wxString::Format("Showing %zu of %zu line(s) (bounded to the last %zu KiB).",
                                              lines, total, kMaxLogBytesToShow / 1024));
        }
    }

    wxStaticText* m_path = nullptr;
    wxChoice* m_level = nullptr;
    wxTextCtrl* m_search = nullptr;
    wxTextCtrl* m_view = nullptr;
    wxStaticText* m_info = nullptr;
};

} // namespace

SettingsTab::SettingsTab(wxWindow* parent, std::function<void()> on_saved)
    : wxPanel(parent, wxID_ANY), m_onSaved(std::move(on_saved)) {
    SetBackgroundColour(wxColour(45, 45, 48));
    create_controls();
    load_controls();
}

void SettingsTab::create_controls() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY, "Settings");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(235, 235, 240));
    outer->Add(title, 0, wxALL, 8);

    auto* explanation = new wxStaticText(
        this, wxID_ANY,
        "Settings are validated and saved atomically to your user configuration folder. They apply "
        "to operations started after saving; a scan or apply already running keeps the settings it "
        "started with. Logging is separate from the operation journal: turning logs off never "
        "disables the journal that makes undo possible.");
    explanation->SetForegroundColour(wxColour(200, 200, 205));
    explanation->Wrap(1000);
    outer->Add(explanation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* generalBox = new wxStaticBoxSizer(wxVERTICAL, this, "General");
    auto* generalParent = generalBox->GetStaticBox();
    m_confirmations = new wxCheckBox(generalParent, wxID_ANY,
                                     "Ask for confirmation before applying a plan");
    m_confirmations->SetToolTip(
        "Recommended. The plan preview is always shown; this adds the final confirmation step.");
    m_includeHidden = new wxCheckBox(generalParent, wxID_ANY,
                                     "Include hidden files and folders in new scans");
    m_crossBoundaries = new wxCheckBox(generalParent, wxID_ANY,
                                       "Allow scanning across filesystem/mount boundaries");
    m_compareTrees = new wxCheckBox(generalParent, wxID_ANY,
                                    "Compare whole directory trees in new scans");
    m_compareTrees->SetToolTip(
        "Detects folders that are duplicates as a whole. Slower, because every file is hashed.");

    auto* bufferRow = new wxBoxSizer(wxHORIZONTAL);
    bufferRow->Add(new wxStaticText(generalParent, wxID_ANY, "Hash buffer size (bytes):"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_hashBuffer = new wxSpinCtrl(generalParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                  wxSize(140, -1), wxSP_ARROW_KEYS, kMinBufferSize, kMaxBufferSize,
                                  65536);
    m_hashBuffer->SetToolTip("Larger buffers read faster but use more memory while hashing");
    bufferRow->Add(m_hashBuffer, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    bufferRow->Add(new wxStaticText(generalParent, wxID_ANY, "(4 KiB - 16 MiB)"), 0,
                   wxALIGN_CENTER_VERTICAL);

    generalBox->Add(m_confirmations, 0, wxALL, 3);
    generalBox->Add(m_includeHidden, 0, wxALL, 3);
    generalBox->Add(m_crossBoundaries, 0, wxALL, 3);
    generalBox->Add(m_compareTrees, 0, wxALL, 3);
    generalBox->Add(bufferRow, 0, wxALL, 3);
    outer->Add(generalBox, 0, wxEXPAND | wxALL, 8);

    auto* duplicatesBox = new wxStaticBoxSizer(wxVERTICAL, this, "Duplicate handling");
    auto* duplicatesParent = duplicatesBox->GetStaticBox();
    auto* modeRow = new wxBoxSizer(wxHORIZONTAL);
    modeRow->Add(new wxStaticText(duplicatesParent, wxID_ANY, "Default operation:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_defaultMode = new wxChoice(duplicatesParent, wxID_ANY);
    m_defaultMode->Append("Move duplicates to a quarantine folder");
    m_defaultMode->Append("Copy unique files into a collection folder");
    m_defaultMode->Append("Merge folders in place (advanced)");
    m_defaultMode->SetToolTip("Pre-selected operation on the Duplicate Files tab");
    modeRow->Add(m_defaultMode, 0, wxALIGN_CENTER_VERTICAL);
    duplicatesBox->Add(modeRow, 0, wxALL, 3);
    duplicatesBox->Add(
        new wxStaticText(duplicatesParent, wxID_ANY,
                         "The destination folder is always chosen per operation, never guessed."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 3);
    outer->Add(duplicatesBox, 0, wxEXPAND | wxALL, 8);

    // Defaults for the three workflow tabs. They are snapshotted when a task
    // starts, so changing them never alters a running operation.
    auto* toolsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Unique Sentences, Useless Files and image defaults");
    auto* toolsParent = toolsBox->GetStaticBox();
    m_sentenceTrim = new wxCheckBox(toolsParent, wxID_ANY,
                                    "Unique Sentences: trim boundary whitespace by default");
    m_sentenceCollapse = new wxCheckBox(
        toolsParent, wxID_ANY,
        "Unique Sentences: treat internal whitespace runs as one space");
    m_sentenceCaseInsensitive = new wxCheckBox(
        toolsParent, wxID_ANY,
        "Unique Sentences: compare without ASCII letter case by default");
    toolsBox->Add(m_sentenceTrim, 0, wxALL, 3);
    toolsBox->Add(m_sentenceCollapse, 0, wxALL, 3);
    toolsBox->Add(m_sentenceCaseInsensitive, 0, wxALL, 3);

    auto* patternsRow = new wxBoxSizer(wxHORIZONTAL);
    patternsRow->Add(new wxStaticText(toolsParent, wxID_ANY,
                                      "Useless Files: default custom patterns (comma separated):"),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_uselessPatterns = new wxTextCtrl(toolsParent, wxID_ANY);
    m_uselessPatterns->SetHint("e.g. *.log, mytemp*");
    patternsRow->Add(m_uselessPatterns, 1, wxALIGN_CENTER_VERTICAL);
    toolsBox->Add(patternsRow, 0, wxEXPAND | wxALL, 3);

    auto* resizeRow = new wxBoxSizer(wxHORIZONTAL);
    resizeRow->Add(new wxStaticText(toolsParent, wxID_ANY, "Images: default output"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_resizeMode = new wxChoice(toolsParent, wxID_ANY);
    m_resizeMode->Append("Create copies");
    m_resizeMode->Append("Resize in place (destructive)");
    resizeRow->Add(m_resizeMode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    resizeRow->Add(new wxStaticText(toolsParent, wxID_ANY, "Max width"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_resizeMaxWidth = new wxSpinCtrl(toolsParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                      wxSize(80, -1), wxSP_ARROW_KEYS, 1, 20000, 1920);
    resizeRow->Add(m_resizeMaxWidth, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    resizeRow->Add(new wxStaticText(toolsParent, wxID_ANY, "Max height"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_resizeMaxHeight = new wxSpinCtrl(toolsParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                       wxSize(80, -1), wxSP_ARROW_KEYS, 1, 20000, 1080);
    resizeRow->Add(m_resizeMaxHeight, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    resizeRow->Add(new wxStaticText(toolsParent, wxID_ANY, "Percent"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_resizePercent = new wxSpinCtrlDouble(toolsParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                           wxSize(80, -1), wxSP_ARROW_KEYS, 1.0, 1000.0, 50.0, 5.0);
    resizeRow->Add(m_resizePercent, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    resizeRow->Add(new wxStaticText(toolsParent, wxID_ANY, "Quality"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_resizeQuality = new wxSpinCtrl(toolsParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                     wxSize(70, -1), wxSP_ARROW_KEYS, 1, 100, 90);
    resizeRow->Add(m_resizeQuality, 0, wxALIGN_CENTER_VERTICAL);
    toolsBox->Add(resizeRow, 0, wxEXPAND | wxALL, 3);
    m_resizeAllowUpscale = new wxCheckBox(toolsParent, wxID_ANY,
                                          "Images: allow enlarging by default");
    toolsBox->Add(m_resizeAllowUpscale, 0, wxALL, 3);
    toolsBox->Add(new wxStaticText(toolsParent, wxID_ANY,
                                   "In-place image resizing keeps a verified backup of every original."),
                  0, wxLEFT | wxRIGHT | wxBOTTOM, 3);
    outer->Add(toolsBox, 0, wxEXPAND | wxALL, 8);

    auto* logsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Application log");
    auto* logsParent = logsBox->GetStaticBox();
    m_loggingEnabled = new wxCheckBox(logsParent, wxID_ANY, "Write an application log");
    m_loggingEnabled->SetToolTip("Diagnostic only. The safety journal is always written.");
    auto* levelRow = new wxBoxSizer(wxHORIZONTAL);
    levelRow->Add(new wxStaticText(logsParent, wxID_ANY, "Minimum level:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_logLevel = new wxChoice(logsParent, wxID_ANY);
    for (const char* level : {"Trace", "Debug", "Info", "Warning", "Error", "Critical"}) {
        m_logLevel->Append(fromUtf8(level));
    }
    levelRow->Add(m_logLevel, 0, wxALIGN_CENTER_VERTICAL);
    logsBox->Add(m_loggingEnabled, 0, wxALL, 3);
    logsBox->Add(levelRow, 0, wxALL, 3);

    auto* rotationRow = new wxBoxSizer(wxHORIZONTAL);
    rotationRow->Add(new wxStaticText(logsParent, wxID_ANY, "Rotate at (KiB):"), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_logMaxSizeKb = new wxSpinCtrl(logsParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                    wxSize(110, -1), wxSP_ARROW_KEYS, 64, 1048576, 10240);
    rotationRow->Add(m_logMaxSizeKb, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    rotationRow->Add(new wxStaticText(logsParent, wxID_ANY, "Keep rotated files:"), 0,
                     wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_logMaxFiles = new wxSpinCtrl(logsParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                   wxSize(70, -1), wxSP_ARROW_KEYS, 1, 50, 5);
    rotationRow->Add(m_logMaxFiles, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    auto* viewLog = new wxButton(logsParent, wxID_ANY, "View log...");
    rotationRow->Add(viewLog, 0, wxALIGN_CENTER_VERTICAL);
    logsBox->Add(rotationRow, 0, wxALL, 3);
    logsBox->Add(
        new wxStaticText(logsParent, wxID_ANY,
                         "Logging is diagnostic only. Turning it off never disables the operation "
                         "journal that makes undo possible."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 3);
    outer->Add(logsBox, 0, wxEXPAND | wxALL, 8);

    viewLog->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        LogViewerDialog dialog(this);
        dialog.ShowModal();
    });

    auto* buttonRow = new wxBoxSizer(wxHORIZONTAL);
    auto* saveButton = new wxButton(this, wxID_SAVE, "Save settings");
    auto* resetButton = new wxButton(this, wxID_ANY, "Reset to defaults");
    auto* openConfig = new wxButton(this, wxID_ANY, "Open settings folder");
    auto* openData = new wxButton(this, wxID_ANY, "Open journal folder");
    auto* openLog = new wxButton(this, wxID_ANY, "Open log folder");
    buttonRow->Add(saveButton, 0, wxALL, 3);
    buttonRow->Add(resetButton, 0, wxALL, 3);
    buttonRow->Add(openConfig, 0, wxALL, 3);
    buttonRow->Add(openData, 0, wxALL, 3);
    buttonRow->Add(openLog, 0, wxALL, 3);
    outer->Add(buttonRow, 0, wxLEFT | wxRIGHT, 6);

    m_paths = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_paths->SetForegroundColour(wxColour(170, 170, 176));
    outer->Add(m_paths, 0, wxALL, 8);

    m_feedback = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_feedback->SetForegroundColour(wxColour(205, 205, 210));
    outer->Add(m_feedback, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(outer);

    saveButton->Bind(wxEVT_BUTTON, &SettingsTab::on_save, this);
    resetButton->Bind(wxEVT_BUTTON, &SettingsTab::on_reset, this);
    openConfig->Bind(wxEVT_BUTTON, &SettingsTab::on_open_config, this);
    openData->Bind(wxEVT_BUTTON, &SettingsTab::on_open_data, this);
    openLog->Bind(wxEVT_BUTTON, &SettingsTab::on_open_log, this);
}

void SettingsTab::load_controls() {
    Settings& settings = Settings::getInstance();
    m_confirmations->SetValue(settings.confirmations_enabled);
    m_includeHidden->SetValue(settings.show_hidden_entries);
    m_crossBoundaries->SetValue(settings.follow_filesystem_boundaries);
    m_compareTrees->SetValue(settings.compare_whole_trees);
    m_hashBuffer->SetValue(static_cast<int>(settings.hash_buffer_size));

    int mode = 0;
    if (settings.default_mode == "unique") mode = 1;
    if (settings.default_mode == "merge") mode = 2;
    m_defaultMode->SetSelection(mode);

    m_sentenceTrim->SetValue(settings.sentences_trim_whitespace);
    m_sentenceCollapse->SetValue(settings.sentences_collapse_whitespace);
    m_sentenceCaseInsensitive->SetValue(settings.sentences_case_insensitive);
    m_uselessPatterns->SetValue(fromUtf8(settings.useless_custom_patterns));
    m_resizeMode->SetSelection(settings.resize_mode == "inplace" ? 1 : 0);
    m_resizeMaxWidth->SetValue(settings.resize_max_width);
    m_resizeMaxHeight->SetValue(settings.resize_max_height);
    m_resizePercent->SetValue(settings.resize_percent);
    m_resizeQuality->SetValue(settings.resize_quality);
    m_resizeAllowUpscale->SetValue(settings.resize_allow_upscale);

    m_loggingEnabled->SetValue(settings.logging_enabled);
    const int level = m_logLevel->FindString(fromUtf8(settings.log_level));
    m_logLevel->SetSelection(level == wxNOT_FOUND ? 2 : level);
    m_logMaxSizeKb->SetValue(static_cast<int>(settings.log_max_file_size / 1024));
    m_logMaxFiles->SetValue(settings.log_max_files);

    wxString paths = "Settings file: " +
                     wxFromPath(std::filesystem::path(PlatformPaths::get_config_dir() /
                                                      "settings.json")) +
                     "\nJournal: " + fromUtf8(settings.journalPath()) +
                     "\nLog: " + wxFromPath(PlatformPaths::get_data_dir() / "app.log");
    m_paths->SetLabel(paths);

    if (!settings.loadError().empty()) {
        set_feedback(fromUtf8(settings.loadError()), true);
    }
}

void SettingsTab::set_feedback(const wxString& text, bool error) {
    m_feedback->SetLabel(text);
    m_feedback->SetForegroundColour(error ? wxColour(250, 150, 150) : wxColour(150, 230, 160));
}

void SettingsTab::on_save(wxCommandEvent&) {
    Settings& settings = Settings::getInstance();
    settings.confirmations_enabled = m_confirmations->IsChecked();
    settings.show_hidden_entries = m_includeHidden->IsChecked();
    settings.follow_filesystem_boundaries = m_crossBoundaries->IsChecked();
    settings.compare_whole_trees = m_compareTrees->IsChecked();
    settings.hash_buffer_size = static_cast<size_t>(m_hashBuffer->GetValue());

    static const std::array<const char*, 3> modes = {"quarantine", "unique", "merge"};
    const int mode = m_defaultMode->GetSelection();
    settings.default_mode = modes[mode < 0 || mode > 2 ? 0 : static_cast<size_t>(mode)];

    settings.sentences_trim_whitespace = m_sentenceTrim->IsChecked();
    settings.sentences_collapse_whitespace = m_sentenceCollapse->IsChecked();
    settings.sentences_case_insensitive = m_sentenceCaseInsensitive->IsChecked();
    settings.useless_custom_patterns = toUtf8(m_uselessPatterns->GetValue());
    settings.resize_mode = m_resizeMode->GetSelection() == 1 ? "inplace" : "copies";
    settings.resize_max_width = m_resizeMaxWidth->GetValue();
    settings.resize_max_height = m_resizeMaxHeight->GetValue();
    settings.resize_percent = m_resizePercent->GetValue();
    settings.resize_quality = m_resizeQuality->GetValue();
    settings.resize_allow_upscale = m_resizeAllowUpscale->IsChecked();

    settings.logging_enabled = m_loggingEnabled->IsChecked();
    settings.log_level = toUtf8(m_logLevel->GetStringSelection());
    settings.log_max_file_size = static_cast<size_t>(m_logMaxSizeKb->GetValue()) * 1024;
    settings.log_max_files = m_logMaxFiles->GetValue();

    if (!settings.save()) {
        set_feedback("Settings could not be saved: " + fromUtf8(settings.saveError()), true);
        wxMessageBox("Settings could not be saved:\n" + fromUtf8(settings.saveError()),
                     "Save failed", wxOK | wxICON_ERROR, this);
        return;
    }

    Logger::getInstance().reload_settings();
    set_feedback("Settings saved. They apply to operations started from now on.", false);
    if (m_onSaved) m_onSaved();
}

void SettingsTab::on_reset(wxCommandEvent&) {
    if (wxMessageBox("Reset every setting to its default value?\n\n"
                     "This does not touch files, the journal, or any scan result.",
                     "Reset settings", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
        return;
    }
    Settings& settings = Settings::getInstance();
    settings.resetToDefaults();
    if (!settings.save()) {
        set_feedback("Defaults were restored in memory but could not be saved: " +
                         fromUtf8(settings.saveError()),
                     true);
    } else {
        set_feedback("Defaults restored and saved.", false);
    }
    load_controls();
    if (m_onSaved) m_onSaved();
}

void SettingsTab::on_open_config(wxCommandEvent&) {
    PlatformPaths::ensure_directories_exist();
    if (!wxLaunchDefaultApplication(wxFromPath(PlatformPaths::get_config_dir()))) {
        wxMessageBox("Could not open " + wxFromPath(PlatformPaths::get_config_dir()),
                     "Open folder", wxOK | wxICON_INFORMATION, this);
    }
}

void SettingsTab::on_open_data(wxCommandEvent&) {
    PlatformPaths::ensure_directories_exist();
    if (!wxLaunchDefaultApplication(wxFromPath(PlatformPaths::get_data_dir()))) {
        wxMessageBox("Could not open " + wxFromPath(PlatformPaths::get_data_dir()),
                     "Open folder", wxOK | wxICON_INFORMATION, this);
    }
}

void SettingsTab::on_open_log(wxCommandEvent&) {
    PlatformPaths::ensure_directories_exist();
    if (!wxLaunchDefaultApplication(wxFromPath(PlatformPaths::get_data_dir()))) {
        wxMessageBox("Could not open " + wxFromPath(PlatformPaths::get_data_dir()),
                     "Open folder", wxOK | wxICON_INFORMATION, this);
    }
}

} // namespace gui
} // namespace dupli
