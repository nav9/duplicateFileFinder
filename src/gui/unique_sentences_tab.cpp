#include "dupli/gui/unique_sentences_tab.hpp"

#include "dupli/gui/async_task.hpp"
#include "dupli/gui/settings.hpp"
#include "dupli/gui/wx_utils.hpp"
#include "dupli/hasher.hpp"
#include "dupli/real_filesystem.hpp"

#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>

#include <algorithm>
#include <utility>

namespace dupli {
namespace gui {

namespace {

constexpr size_t kMaxPreviewRows = 5000;

wxString encoding_label(TextEncoding encoding) {
    switch (encoding) {
    case TextEncoding::Auto: return "Detect automatically";
    case TextEncoding::Utf8: return "UTF-8";
    case TextEncoding::Utf16LE: return "UTF-16 little endian";
    case TextEncoding::Utf16BE: return "UTF-16 big endian";
    case TextEncoding::Latin1: return "Latin-1 (ISO-8859-1)";
    }
    return "Detect automatically";
}

TextEncoding encoding_from_index(int index) {
    switch (index) {
    case 1: return TextEncoding::Utf8;
    case 2: return TextEncoding::Utf16LE;
    case 3: return TextEncoding::Utf16BE;
    case 4: return TextEncoding::Latin1;
    default: return TextEncoding::Auto;
    }
}

/// Small modal used for pasted text. Pasted text is ordinary data: it is never
/// interpreted as a command, a path or a pattern.
class PasteDialog : public wxDialog {
public:
    explicit PasteDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "Paste text", wxDefaultPosition, wxSize(680, 460),
                   wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
        SetBackgroundColour(wxColour(45, 45, 48));
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        auto* label = new wxStaticText(
            this, wxID_ANY,
            "Paste one or more sentences below. The text is only compared; it is not run or "
            "saved anywhere until you save the de-duplicated output.");
        label->SetForegroundColour(wxColour(215, 215, 220));
        label->Wrap(640);
        sizer->Add(label, 0, wxALL | wxEXPAND, 10);
        m_text = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                wxTE_MULTILINE | wxTE_RICH2);
        m_text->SetBackgroundColour(wxColour(28, 28, 30));
        m_text->SetForegroundColour(wxColour(230, 230, 235));
        sizer->Add(m_text, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer(1);
        buttons->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0, wxRIGHT, 8);
        auto* ok = new wxButton(this, wxID_OK, "Add text");
        buttons->Add(ok, 0);
        sizer->Add(buttons, 0, wxEXPAND | wxALL, 10);
        SetSizer(sizer);
        ok->SetDefault();
        m_text->SetFocus();
    }

    std::string text() const { return dupli::gui::toUtf8(m_text->GetValue()); }

private:
    wxTextCtrl* m_text = nullptr;
};

} // namespace

UniqueSentencesTab::UniqueSentencesTab(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL) {
    SetBackgroundColour(wxColour(45, 45, 48));
    SetScrollRate(0, 12);
    create_controls();
    reload_settings();
    update_controls();
}

void UniqueSentencesTab::reload_settings() {
    Settings& settings = Settings::getInstance();
    m_trim->SetValue(settings.sentences_trim_whitespace);
    m_collapse->SetValue(settings.sentences_collapse_whitespace);
    m_caseInsensitive->SetValue(settings.sentences_case_insensitive);
}

UniqueSentencesTab::~UniqueSentencesTab() {
    if (m_task) {
        m_task->Wait();
        delete m_task;
        m_task = nullptr;
    }
}

void UniqueSentencesTab::create_controls() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(
        this, wxID_ANY, "Unique Sentences - remove sentences that occur more than once");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(235, 235, 240));
    outer->Add(title, 0, wxALL, 8);

    auto* explanation = new wxStaticText(
        this, wxID_ANY,
        "This is exact duplicate removal, not paraphrasing or translation. A sentence is kept the "
        "first time it appears, in the order the sources were added. Case, punctuation and internal "
        "spacing are significant unless you change the options below. Output is one sentence per "
        "line, so the original paragraph layout is not preserved.");
    explanation->SetForegroundColour(wxColour(200, 200, 205));
    explanation->Wrap(1000);
    outer->Add(explanation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    auto* sourcesBox = new wxStaticBoxSizer(wxVERTICAL, this, "Text sources (read-only)");
    auto* sourcesParent = sourcesBox->GetStaticBox();
    m_sources = new wxListBox(sourcesParent, wxID_ANY, wxDefaultPosition, wxSize(-1, 120));
    m_sources->SetToolTip("Files and pasted text, in the order their sentences are considered");
    m_sources->SetBackgroundColour(wxColour(28, 28, 30));
    m_sources->SetForegroundColour(wxColour(225, 225, 228));
    sourcesBox->Add(m_sources, 1, wxEXPAND | wxALL, 4);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    m_addFiles = new wxButton(sourcesParent, wxID_ANY, "Add text files...");
    m_addPasted = new wxButton(sourcesParent, wxID_ANY, "Add pasted text...");
    m_remove = new wxButton(sourcesParent, wxID_ANY, "Remove");
    m_clear = new wxButton(sourcesParent, wxID_ANY, "Clear all");
    row->Add(m_addFiles, 0, wxALL, 2);
    row->Add(m_addPasted, 0, wxALL, 2);
    row->Add(m_remove, 0, wxALL, 2);
    row->Add(m_clear, 0, wxALL, 2);
    row->AddStretchSpacer(1);
    row->Add(new wxStaticText(sourcesParent, wxID_ANY, "File encoding:"), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_encoding = new wxChoice(sourcesParent, wxID_ANY);
    for (int index = 0; index < 5; ++index) {
        m_encoding->Append(encoding_label(encoding_from_index(index)));
    }
    m_encoding->SetSelection(0);
    m_encoding->SetToolTip(
        "Used when a file is added. Automatic detection refuses to guess when the bytes are not "
        "valid UTF-8, so choose the encoding explicitly for such files.");
    row->Add(m_encoding, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    sourcesBox->Add(row, 0, wxEXPAND, 2);
    outer->Add(sourcesBox, 0, wxEXPAND | wxALL, 8);

    auto* optionsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Comparison policy");
    auto* optionsParent = optionsBox->GetStaticBox();
    m_trim = new wxCheckBox(optionsParent, wxID_ANY,
                            "Trim whitespace at the start and end of each sentence");
    m_trim->SetValue(true);
    m_collapse = new wxCheckBox(optionsParent, wxID_ANY,
                                "Treat runs of internal whitespace as one space");
    m_collapse->SetValue(false);
    m_caseInsensitive = new wxCheckBox(optionsParent, wxID_ANY,
                                       "Ignore letter case when comparing (ASCII letters only)");
    m_caseInsensitive->SetValue(false);
    optionsBox->Add(m_trim, 0, wxALL, 3);
    optionsBox->Add(m_collapse, 0, wxALL, 3);
    optionsBox->Add(m_caseInsensitive, 0, wxALL, 3);
    optionsBox->Add(
        new wxStaticText(optionsParent, wxID_ANY,
                         "Turning case-insensitivity on keeps the first spelling in the output."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 3);
    outer->Add(optionsBox, 0, wxEXPAND | wxALL, 8);

    auto* actionRow = new wxBoxSizer(wxHORIZONTAL);
    m_preview = new wxButton(this, wxID_ANY, "Preview sentences");
    m_save = new wxButton(this, wxID_ANY, "Save output...");
    m_cancel = new wxButton(this, wxID_ANY, "Cancel");
    auto* help = new wxButton(this, wxID_ANY, "How sentences are split");
    m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(140, -1));
    actionRow->Add(m_preview, 0, wxALL, 3);
    actionRow->Add(m_save, 0, wxALL, 3);
    actionRow->Add(m_cancel, 0, wxALL, 3);
    actionRow->Add(help, 0, wxALL, 3);
    actionRow->Add(m_gauge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    outer->Add(actionRow, 0, wxLEFT | wxRIGHT, 6);

    m_status = new wxStaticText(this, wxID_ANY, "Ready. Add at least one text source.");
    m_status->SetForegroundColour(wxColour(205, 205, 210));
    outer->Add(m_status, 0, wxALL, 8);

    m_stats = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_stats->SetForegroundColour(wxColour(200, 200, 205));
    outer->Add(m_stats, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_results = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 240),
                               wxLC_REPORT | wxLC_SINGLE_SEL);
    m_results->InsertColumn(0, "#", wxLIST_FORMAT_RIGHT, 60);
    m_results->InsertColumn(1, "Status", wxLIST_FORMAT_LEFT, 80);
    m_results->InsertColumn(2, "Sentence", wxLIST_FORMAT_LEFT, 480);
    m_results->InsertColumn(3, "Source", wxLIST_FORMAT_LEFT, 240);
    m_results->InsertColumn(4, "Kept occurrence", wxLIST_FORMAT_LEFT, 220);
    m_results->SetBackgroundColour(wxColour(28, 28, 30));
    m_results->SetForegroundColour(wxColour(225, 225, 228));
    outer->Add(m_results, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_details = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 100),
                               wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_details->SetBackgroundColour(wxColour(28, 28, 30));
    m_details->SetForegroundColour(wxColour(210, 210, 215));
    m_details->SetToolTip("Full text of the selected sentence and why it was kept or removed");
    outer->Add(m_details, 0, wxEXPAND | wxALL, 8);

    SetSizer(outer);
    // Virtual size is computed after the first layout pass: doing it during
    // construction (before GTK allocates the page) produces zero-size scrollbars.
    CallAfter([this] { FitInside(); });

    m_addFiles->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_add_files, this);
    m_addPasted->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_add_pasted, this);
    m_remove->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_remove, this);
    m_clear->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_clear, this);
    m_preview->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_preview, this);
    m_save->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_save, this);
    m_cancel->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_cancel, this);
    help->Bind(wxEVT_BUTTON, &UniqueSentencesTab::on_help, this);
    Bind(EVT_DUPLI_ASYNC_DONE, &UniqueSentencesTab::on_task_done, this);
    m_results->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& event) {
        if (!m_outcome) return;
        const long row = event.GetIndex();
        if (row < 0) return;
        const size_t index = static_cast<size_t>(m_results->GetItemData(row));
        if (index >= m_outcome->result.sentences.size()) return;
        const auto& sentence = m_outcome->result.sentences[index];
        wxString detail = sentence.text + "\n\nSource: " + fromUtf8(sentence.source_label) +
                          " (sentence " + wxString::Format("%zu", sentence.sentence_index + 1) +
                          ")\n";
        if (sentence.duplicate) {
            detail += "Status: duplicate; first kept from " +
                      fromUtf8(sentence.kept_source_label) + " (sentence " +
                      wxString::Format("%zu", sentence.kept_sentence_index + 1) + ").";
        } else {
            detail += "Status: kept (first occurrence).";
        }
        m_details->SetValue(detail);
    });
}

void UniqueSentencesTab::add_source(SourceEntry entry) {
    const wxString display =
        (entry.path.empty() ? "Pasted text" : wxFromPath(entry.path)) + "  [" +
        encoding_label(entry.detected) + "]";
    m_entries.push_back(std::move(entry));
    m_sources->Append(display);
    update_controls();
}

void UniqueSentencesTab::update_controls() {
    const bool idle = !m_busy;
    m_addFiles->Enable(idle);
    m_addPasted->Enable(idle);
    m_remove->Enable(idle && m_sources->GetSelection() != wxNOT_FOUND);
    m_clear->Enable(idle && !m_entries.empty());
    m_encoding->Enable(idle);
    m_trim->Enable(idle);
    m_collapse->Enable(idle);
    m_caseInsensitive->Enable(idle);
    m_preview->Enable(idle && !m_entries.empty());
    m_cancel->Enable(m_busy);
    m_save->Enable(idle && m_outcome && m_outcome->ok &&
                   !m_outcome->result.output().empty());
}

void UniqueSentencesTab::set_status(const wxString& text, bool error) {
    m_status->SetLabel(text);
    m_status->SetForegroundColour(error ? wxColour(250, 150, 150) : wxColour(205, 205, 210));
}

dupli::SentencePolicy UniqueSentencesTab::policy() const {
    dupli::SentencePolicy policy;
    policy.trim_boundary_whitespace = m_trim->IsChecked();
    policy.normalize_internal_whitespace = m_collapse->IsChecked();
    policy.case_sensitive = !m_caseInsensitive->IsChecked();
    return policy;
}

void UniqueSentencesTab::on_add_files(wxCommandEvent&) {
    wxFileDialog dialog(
        this, "Add text files", wxEmptyString, wxEmptyString,
        "Text files (*.txt;*.md;*.csv;*.log;*.json;*.html)|*.txt;*.md;*.csv;*.log;*.json;*.html|"
        "All files (*.*)|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
    if (dialog.ShowModal() != wxID_OK) return;

    wxArrayString paths;
    dialog.GetPaths(paths);
    const TextEncoding requested = encoding_from_index(m_encoding->GetSelection());
    RealFileSystem fs;
    wxString problems;
    size_t added = 0;

    for (const auto& path : paths) {
        const std::filesystem::path file = pathFromWx(path);
        auto decoded = read_text_source(fs, file, requested);
        if (decoded.has_error()) {
            problems += "\n" + path + ": " + fromUtf8(decoded.error().message);
            continue;
        }
        if (decoded.value().binary_looking) {
            problems += "\n" + path + ": skipped because it looks like a binary file.";
            continue;
        }
        SourceEntry entry;
        entry.path = file;
        entry.label = path_to_utf8(file);
        entry.encoding = requested;
        entry.detected = decoded.value().encoding;
        entry.text = decoded.value().utf8;
        add_source(std::move(entry));
        ++added;
    }

    if (!problems.empty()) {
        wxMessageBox("Some files could not be added:" + problems +
                         "\n\nChoose the file's encoding explicitly with the \"File encoding\" "
                         "list and add it again if it is really a text file.",
                     "Text sources", wxOK | wxICON_WARNING, this);
    }
    set_status(wxString::Format("%zu source(s) added.", m_entries.size()), added == 0);
    update_controls();
}

void UniqueSentencesTab::on_add_pasted(wxCommandEvent&) {
    PasteDialog dialog(this);
    if (dialog.ShowModal() != wxID_OK) return;
    const std::string text = dialog.text();
    if (text.empty()) {
        set_status("The pasted text was empty; nothing was added.", true);
        return;
    }
    SourceEntry entry;
    entry.label = "Pasted text " + std::to_string(m_entries.size() + 1);
    entry.encoding = TextEncoding::Utf8;
    entry.detected = TextEncoding::Utf8;
    entry.text = text;
    add_source(std::move(entry));
    set_status("Pasted text added as UTF-8.");
    update_controls();
}

void UniqueSentencesTab::on_remove(wxCommandEvent&) {
    const int selection = m_sources->GetSelection();
    if (selection == wxNOT_FOUND) return;
    m_sources->Delete(static_cast<unsigned int>(selection));
    m_entries.erase(m_entries.begin() + selection);
    m_outcome.reset();
    m_results->DeleteAllItems();
    m_stats->SetLabel(wxEmptyString);
    update_controls();
}

void UniqueSentencesTab::on_clear(wxCommandEvent&) {
    if (m_entries.empty()) return;
    if (wxMessageBox("Remove every source from this tab?\n\nThe files themselves are not touched.",
                     "Clear sources", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
        return;
    }
    m_entries.clear();
    m_sources->Clear();
    m_outcome.reset();
    m_results->DeleteAllItems();
    m_stats->SetLabel(wxEmptyString);
    set_status("Sources cleared.");
    update_controls();
}

void UniqueSentencesTab::on_preview(wxCommandEvent&) {
    if (m_entries.empty() || m_busy) return;

    std::vector<SentenceSource> snapshot;
    snapshot.reserve(m_entries.size());
    for (const auto& entry : m_entries) snapshot.push_back({entry.label, entry.text});

    auto outcome = std::make_shared<PreviewOutcome>();
    m_outcome = outcome;
    m_cancelFlag->store(false);
    m_busy = true;
    m_gauge->Pulse();
    set_status("Extracting and comparing sentences...");
    update_controls();

    auto cancel = m_cancelFlag;
    const dupli::SentencePolicy current_policy = policy();
    (void)cancel;
    m_task = new AsyncTask(this, [outcome, snapshot, current_policy, cancel] {
        if (cancel && cancel->load()) {
            outcome->error = "Cancelled";
            return;
        }
        auto deduped = deduplicate_sentences(snapshot, current_policy);
        if (deduped.has_error()) {
            outcome->error = deduped.error().message;
            return;
        }
        outcome->result = std::move(deduped.value());
        outcome->ok = true;
    });
    if (m_task->Run() != wxTHREAD_NO_ERROR) {
        delete m_task;
        m_task = nullptr;
        m_busy = false;
        set_status("Could not start a background worker; nothing was changed.", true);
        update_controls();
    }
}

void UniqueSentencesTab::on_task_done(wxThreadEvent&) {
    if (m_task) {
        m_task->Wait();
        const std::string error = m_task->error();
        delete m_task;
        m_task = nullptr;
        if (!error.empty() && m_outcome && m_outcome->error.empty()) m_outcome->error = error;
    }
    m_busy = false;
    apply_results();
    update_controls();
}

void UniqueSentencesTab::apply_results() {
    m_results->Freeze();
    m_results->DeleteAllItems();
    m_details->Clear();

    if (!m_outcome) {
        m_results->Thaw();
        return;
    }
    if (!m_outcome->ok) {
        m_results->Thaw();
        set_status("Preview failed: " + fromUtf8(m_outcome->error), true);
        m_stats->SetLabel(wxEmptyString);
        return;
    }

    const auto& result = m_outcome->result;
    size_t shown = 0;
    for (size_t index = 0; index < result.sentences.size() && shown < kMaxPreviewRows;
         ++index, ++shown) {
        const auto& sentence = result.sentences[index];
        const long row = m_results->InsertItem(
            m_results->GetItemCount(), wxString::Format("%zu", index + 1));
        m_results->SetItem(row, 1, sentence.duplicate ? "Duplicate" : "Keep");
        wxString text = fromUtf8(sentence.text);
        // Very long lines would make the list unreadable; the details pane keeps
        // the full text.
        if (text.length() > 180) text = text.Left(177) + "...";
        m_results->SetItem(row, 2, text);
        m_results->SetItem(row, 3, fromUtf8(sentence.source_label));
        m_results->SetItem(row, 4,
                           fromUtf8(sentence.kept_source_label) +
                               wxString::Format(" #%zu", sentence.kept_sentence_index + 1));
        m_results->SetItemData(row, static_cast<long>(index));
    }
    m_results->Thaw();

    wxString stats = format_count(result.total_sentences) + " sentence(s): " +
                     format_count(result.unique_sentences) + " kept, " +
                     format_count(result.duplicate_sentences) + " duplicate(s) removed.";
    if (result.sentences.size() > kMaxPreviewRows) {
        stats += wxString::Format(" Only the first %zu are listed.", kMaxPreviewRows);
    }
    m_stats->SetLabel(stats);

    wxString warnings;
    for (const auto& warning : result.warnings) warnings += " " + fromUtf8(warning);
    set_status("Preview ready. Nothing has been written yet." + warnings,
               !result.warnings.empty());
}

void UniqueSentencesTab::on_save(wxCommandEvent&) {
    if (!m_outcome || !m_outcome->ok) return;
    const std::string output = m_outcome->result.output();
    if (output.empty()) {
        wxMessageBox("There are no unique sentences to save.", "Nothing to save",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    wxFileDialog dialog(this, "Save the unique sentences", wxEmptyString, "unique-sentences.txt",
                        "Text file (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;
    const std::filesystem::path path = pathFromWx(dialog.GetPath());

    RealFileSystem fs;
    auto written = write_text_output(fs, path, output, true);
    if (written.has_error()) {
        wxMessageBox("The output could not be written:\n" + fromUtf8(written.error().message) +
                         "\n\nNo partial output was left behind.",
                     "Save failed", wxOK | wxICON_ERROR, this);
        set_status("Save failed: " + fromUtf8(written.error().message), true);
        return;
    }

    // Read the file back and compare digests: a successful write() alone is not
    // proof that the complete content reached the disk.
    Blake3Hasher hasher;
    auto opened = fs.open_read(path);
    std::string problem;
    if (opened.has_error()) {
        problem = opened.error().message;
    } else {
        auto bytes = opened.value()->read_all();
        (void)opened.value()->close();
        if (bytes.has_error()) {
            problem = bytes.error().message;
        } else {
            auto digest = hasher.hash_data(bytes.value());
            if (digest.has_error() || digest.value().hex != text_digest_hex(output)) {
                problem = "the written file does not match the generated text";
            }
        }
    }
    if (!problem.empty()) {
        wxMessageBox("The file was written but could not be verified: " + fromUtf8(problem) +
                         "\n\nCheck the destination before relying on it.",
                     "Verification failed", wxOK | wxICON_WARNING, this);
        set_status("Saved, but verification failed: " + fromUtf8(problem), true);
        return;
    }

    set_status("Saved " + format_count(m_outcome->result.unique_sentences) +
               " sentence(s) to " + wxFromPath(path) + " (verified).");
    if (wxMessageBox("Output saved and verified:\n" + wxFromPath(path) +
                         "\n\nOpen the containing folder?",
                     "Saved", wxYES_NO | wxICON_INFORMATION, this) == wxYES) {
        wxLaunchDefaultApplication(wxFromPath(path.parent_path()));
    }
}

void UniqueSentencesTab::on_cancel(wxCommandEvent&) {
    if (!m_busy) return;
    m_cancelFlag->store(true);
    set_status("Cancelling... the current comparison has to reach a safe point first.");
}

void UniqueSentencesTab::on_help(wxCommandEvent&) {
    wxMessageBox(
        "Sentences end at '.', '!' or '?' followed by a space, a closing quote/bracket, or the end "
        "of the text. This is a conservative rule, not a linguistic parser:\n\n"
        "  * A period inside a number (3.14) does not split.\n"
        "  * Common abbreviations (Mr., Dr., e.g., i.e., etc.) do not split.\n"
        "  * A single capital initial (J. R. R. Tolkien) does not split.\n"
        "  * An ellipsis (...) counts as one boundary, not three.\n"
        "  * Text directly after a terminator without a space (example.com, Hi!there) does not "
        "split.\n"
        "  * A single line break inside a sentence is treated as a space, so wrapped text stays "
        "one sentence.\n\n"
        "Because the rule is conservative, it can merge two sentences when an abbreviation is not "
        "in the built-in list. It never silently drops text: anything left at the end is kept.",
        "How sentences are split", wxOK | wxICON_INFORMATION, this);
}

} // namespace gui
} // namespace dupli
