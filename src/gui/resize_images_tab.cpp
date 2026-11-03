#include "dupli/gui/resize_images_tab.hpp"

#include "dupli/gui/async_task.hpp"
#include "dupli/gui/file_dialog_helper.hpp"
#include "dupli/gui/settings.hpp"
#include "dupli/gui/wx_utils.hpp"
#include "dupli/real_filesystem.hpp"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/textdlg.h>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace dupli {
namespace gui {

namespace {

constexpr int kPreviewMaxWidth = 300;
constexpr int kPreviewMaxHeight = 220;

std::string describe_rule(const ImageResizeRule& rule) {
    if (rule.mode == ImageResizeRule::Mode::Percent) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.0f%% of the original size", rule.percent);
        return buffer;
    }
    return "maximum " + std::to_string(rule.max_width) + " x " +
           std::to_string(rule.max_height) + " pixels";
}

/// Convert core pixels to a wx bitmap for display only. Presentation scaling
/// never affects the file that is written; that goes through the core service.
wxBitmap raster_to_bitmap(const RasterImage& image, int max_width, int max_height) {
    if (!image.valid()) return wxBitmap();
    wxImage wx_image(static_cast<int>(image.width), static_cast<int>(image.height));
    unsigned char* rgb = wx_image.GetData();
    for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
        rgb[pixel * 3 + 0] = image.rgba[pixel * 4 + 0];
        rgb[pixel * 3 + 1] = image.rgba[pixel * 4 + 1];
        rgb[pixel * 3 + 2] = image.rgba[pixel * 4 + 2];
    }
    if (image.has_alpha) {
        wx_image.InitAlpha();
        unsigned char* alpha = wx_image.GetAlpha();
        for (size_t pixel = 0; pixel < static_cast<size_t>(image.width) * image.height; ++pixel) {
            alpha[pixel] = image.rgba[pixel * 4 + 3];
        }
    }
    const double scale = std::min(
        1.0, std::min(static_cast<double>(max_width) / image.width,
                      static_cast<double>(max_height) / image.height));
    if (scale < 1.0) {
        wx_image = wx_image.Scale(std::max(1, static_cast<int>(image.width * scale)),
                                  std::max(1, static_cast<int>(image.height * scale)),
                                  wxIMAGE_QUALITY_HIGH);
    }
    return wxBitmap(wx_image);
}

} // namespace

ResizeImagesTab::ResizeImagesTab(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL),
      m_timer(this) {
    SetBackgroundColour(wxColour(45, 45, 48));
    SetScrollRate(0, 12);
    create_controls();
    reload_settings();
    update_controls();
}

void ResizeImagesTab::reload_settings() {
    Settings& settings = Settings::getInstance();
    m_maxWidth->SetValue(settings.resize_max_width);
    m_maxHeight->SetValue(settings.resize_max_height);
    m_percent->SetValue(settings.resize_percent);
    m_quality->SetValue(settings.resize_quality);
    m_allowUpscale->SetValue(settings.resize_allow_upscale);
    m_outputMode->SetSelection(settings.resize_mode == "inplace" ? 1 : 0);
}

ResizeImagesTab::~ResizeImagesTab() {
    m_timer.Stop();
    if (m_task) {
        m_task->Wait();
        delete m_task;
        m_task = nullptr;
    }
}

void ResizeImagesTab::create_controls() {
    auto* outer = new wxBoxSizer(wxVERTICAL);

    auto* title = new wxStaticText(this, wxID_ANY,
                                   "Resize Images - create smaller copies, or resize in place");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(235, 235, 240));
    outer->Add(title, 0, wxALL, 8);

    auto* explanation = new wxStaticText(
        this, wxID_ANY,
        "Creating copies is the default and leaves every original untouched. Resizing in place is "
        "destructive: the program first keeps a verified backup of each original, and you can "
        "reverse the last in-place batch with the Undo button below. Animated and multi-page "
        "images are skipped rather than flattened. EXIF orientation is not applied, so some camera "
        "JPEGs may look differently after resizing.");
    explanation->SetForegroundColour(wxColour(200, 200, 205));
    explanation->Wrap(1000);
    outer->Add(explanation, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);

    // --- Input folder ----------------------------------------------------
    auto* inputBox = new wxStaticBoxSizer(wxVERTICAL, this, "Folder to search");
    auto* inputParent = inputBox->GetStaticBox();
    auto* rootRow = new wxBoxSizer(wxHORIZONTAL);
    m_root = new wxTextCtrl(inputParent, wxID_ANY);
    m_root->SetHint("Choose a folder; every image below it is listed");
    m_browseRoot = new wxButton(inputParent, wxID_ANY, "Browse...");
    rootRow->Add(m_root, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    rootRow->Add(m_browseRoot, 0, wxALIGN_CENTER_VERTICAL);
    inputBox->Add(rootRow, 0, wxEXPAND | wxALL, 4);
    outer->Add(inputBox, 0, wxEXPAND | wxALL, 8);

    // --- Rule ------------------------------------------------------------
    auto* ruleBox = new wxStaticBoxSizer(wxVERTICAL, this, "Size rule");
    auto* ruleParent = ruleBox->GetStaticBox();
    auto* modeRow = new wxBoxSizer(wxHORIZONTAL);
    modeRow->Add(new wxStaticText(ruleParent, wxID_ANY, "Resize by:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    m_mode = new wxChoice(ruleParent, wxID_ANY);
    m_mode->Append("Maximum width and height");
    m_mode->Append("Percentage");
    m_mode->SetSelection(0);
    modeRow->Add(m_mode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    modeRow->Add(new wxStaticText(ruleParent, wxID_ANY, "Max width:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_maxWidth = new wxSpinCtrl(ruleParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                wxSize(90, -1), wxSP_ARROW_KEYS, 1, 20000, 1920);
    modeRow->Add(m_maxWidth, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    modeRow->Add(new wxStaticText(ruleParent, wxID_ANY, "Max height:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_maxHeight = new wxSpinCtrl(ruleParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                 wxSize(90, -1), wxSP_ARROW_KEYS, 1, 20000, 1080);
    modeRow->Add(m_maxHeight, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    modeRow->Add(new wxStaticText(ruleParent, wxID_ANY, "Percent:"), 0,
                 wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_percent = new wxSpinCtrlDouble(ruleParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                     wxSize(90, -1), wxSP_ARROW_KEYS, 1.0, 1000.0, 50.0, 5.0);
    modeRow->Add(m_percent, 0, wxALIGN_CENTER_VERTICAL);
    ruleBox->Add(modeRow, 0, wxALL, 4);

    auto* optionRow = new wxBoxSizer(wxHORIZONTAL);
    m_allowUpscale = new wxCheckBox(ruleParent, wxID_ANY,
                                    "Allow enlarging images smaller than the rule");
    m_allowUpscale->SetValue(false);
    m_preserveMetadata = new wxCheckBox(ruleParent, wxID_ANY,
                                        "Preserve permissions and timestamps when supported");
    m_preserveMetadata->SetValue(true);
    optionRow->Add(m_allowUpscale, 0, wxALL, 3);
    optionRow->Add(m_preserveMetadata, 0, wxALL, 3);
    optionRow->Add(new wxStaticText(ruleParent, wxID_ANY, "Quality (lossy formats):"), 0,
                   wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    m_quality = new wxSpinCtrl(ruleParent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxSize(70, -1), wxSP_ARROW_KEYS, 1, 100, 90);
    optionRow->Add(m_quality, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    ruleBox->Add(optionRow, 0, wxALL, 4);
    ruleBox->Add(
        new wxStaticText(ruleParent, wxID_ANY,
                         "Aspect ratio is always preserved. Images already within the rule are "
                         "skipped, never enlarged (unless you allow it above)."),
        0, wxLEFT | wxRIGHT | wxBOTTOM, 4);
    outer->Add(ruleBox, 0, wxEXPAND | wxALL, 8);

    // --- Output ----------------------------------------------------------
    auto* outputBox = new wxStaticBoxSizer(wxVERTICAL, this, "Output");
    auto* outputParent = outputBox->GetStaticBox();
    auto* outputRow = new wxBoxSizer(wxHORIZONTAL);
    m_outputMode = new wxChoice(outputParent, wxID_ANY);
    m_outputMode->Append("Create resized copies (originals kept)");
    m_outputMode->Append("Resize in place (destructive, backup kept)");
    m_outputMode->SetSelection(0);
    outputRow->Add(m_outputMode, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    outputRow->Add(new wxStaticText(outputParent, wxID_ANY, "Copies folder:"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_outputDir = new wxTextCtrl(outputParent, wxID_ANY);
    m_outputDir->SetHint("Defaults to <chosen folder>/resized_images");
    m_browseOutput = new wxButton(outputParent, wxID_ANY, "Browse...");
    outputRow->Add(m_outputDir, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    outputRow->Add(m_browseOutput, 0, wxALIGN_CENTER_VERTICAL);
    outputBox->Add(outputRow, 0, wxEXPAND | wxALL, 4);
    outer->Add(outputBox, 0, wxEXPAND | wxALL, 8);

    // --- Actions ---------------------------------------------------------
    auto* actionRow = new wxBoxSizer(wxHORIZONTAL);
    m_discover = new wxButton(this, wxID_ANY, "Find images");
    m_cancel = new wxButton(this, wxID_ANY, "Cancel");
    m_previewButton = new wxButton(this, wxID_ANY, "Preview selected");
    m_resize = new wxButton(this, wxID_ANY, "Resize selected...");
    m_undo = new wxButton(this, wxID_ANY, "Undo last in-place batch");
    m_selectAll = new wxButton(this, wxID_ANY, "Select all");
    m_selectNone = new wxButton(this, wxID_ANY, "Select none");
    m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(140, -1));
    actionRow->Add(m_discover, 0, wxALL, 3);
    actionRow->Add(m_cancel, 0, wxALL, 3);
    actionRow->Add(m_selectAll, 0, wxALL, 3);
    actionRow->Add(m_selectNone, 0, wxALL, 3);
    actionRow->Add(m_previewButton, 0, wxALL, 3);
    actionRow->Add(m_resize, 0, wxALL, 3);
    actionRow->Add(m_undo, 0, wxALL, 3);
    actionRow->Add(m_gauge, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 8);
    outer->Add(actionRow, 0, wxLEFT | wxRIGHT, 6);

    m_status = new wxStaticText(this, wxID_ANY, "Ready. Choose a folder and press Find images.");
    m_status->SetForegroundColour(wxColour(205, 205, 210));
    outer->Add(m_status, 0, wxALL, 8);
    m_summary = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_summary->SetForegroundColour(wxColour(200, 200, 205));
    outer->Add(m_summary, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    m_images = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 220));
    m_images->SetBackgroundColour(wxColour(28, 28, 30));
    m_images->SetForegroundColour(wxColour(225, 225, 228));
    m_images->SetToolTip("Tick the images to resize. Nothing is selected automatically.");
    outer->Add(m_images, 1, wxEXPAND | wxALL, 8);

    outer->Add(create_preview_panel(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(outer);
    // Defer the virtual-size calculation until GTK has allocated the page; doing
    // it during construction yields zero-size scrollbars and GTK warnings.
    CallAfter([this] { FitInside(); });

    m_browseRoot->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const std::string path =
            FileDialogHelper::ShowBrowseFolderDialog(this, "Choose a folder with images");
        if (path.empty()) return;
        m_root->SetValue(fromUtf8(path));
        if (m_outputDir->GetValue().Strip(wxString::both).empty()) {
            m_outputDir->SetValue(fromUtf8(path + "/resized_images"));
        }
        update_controls();
    });
    m_browseOutput->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_browse_output, this);
    m_mode->Bind(wxEVT_CHOICE, &ResizeImagesTab::on_mode_changed, this);
    m_outputMode->Bind(wxEVT_CHOICE, &ResizeImagesTab::on_mode_changed, this);
    m_discover->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_discover, this);
    m_cancel->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_cancel, this);
    m_previewButton->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_preview, this);
    m_resize->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_resize, this);
    m_undo->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_undo_in_place, this);
    m_selectAll->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_select_all, this);
    m_selectNone->Bind(wxEVT_BUTTON, &ResizeImagesTab::on_select_none, this);
    Bind(EVT_DUPLI_ASYNC_DONE, &ResizeImagesTab::on_task_done, this);
    Bind(wxEVT_TIMER, &ResizeImagesTab::on_timer, this);
    m_images->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent&) { update_summary(); });
    m_images->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { update_summary(); });
}

wxWindow* ResizeImagesTab::create_preview_panel(wxWindow* parent) {
    auto* container = new wxPanel(parent, wxID_ANY);
    container->SetBackgroundColour(wxColour(45, 45, 48));
    auto* outer = new wxBoxSizer(wxVERTICAL);
    auto* panel = new wxStaticBoxSizer(wxHORIZONTAL, container, "Preview (presentation only)");
    auto* boxParent = panel->GetStaticBox();

    auto* originalColumn = new wxBoxSizer(wxVERTICAL);
    originalColumn->Add(new wxStaticText(boxParent, wxID_ANY, "Original"),
                        0, wxALIGN_CENTER_HORIZONTAL | wxALL, 4);
    m_originalPreview = new wxStaticBitmap(boxParent, wxID_ANY, wxBitmap());
    originalColumn->Add(m_originalPreview, 0, wxALIGN_CENTER | wxALL, 4);
    m_originalLabel = new wxStaticText(boxParent, wxID_ANY, wxEmptyString);
    m_originalLabel->SetForegroundColour(wxColour(200, 200, 205));
    originalColumn->Add(m_originalLabel, 0, wxALIGN_CENTER | wxALL, 4);

    auto* resizedColumn = new wxBoxSizer(wxVERTICAL);
    resizedColumn->Add(new wxStaticText(boxParent, wxID_ANY, "After resizing"),
                       0, wxALIGN_CENTER_HORIZONTAL | wxALL, 4);
    m_resizedPreview = new wxStaticBitmap(boxParent, wxID_ANY, wxBitmap());
    resizedColumn->Add(m_resizedPreview, 0, wxALIGN_CENTER | wxALL, 4);
    m_resizedLabel = new wxStaticText(boxParent, wxID_ANY, wxEmptyString);
    m_resizedLabel->SetForegroundColour(wxColour(200, 200, 205));
    resizedColumn->Add(m_resizedLabel, 0, wxALIGN_CENTER | wxALL, 4);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(originalColumn, 1, wxEXPAND | wxALL, 6);
    row->Add(resizedColumn, 1, wxEXPAND | wxALL, 6);
    panel->Add(row, 1, wxEXPAND);
    outer->Add(panel, 0, wxEXPAND | wxALL, 4);
    outer->Add(new wxStaticText(container, wxID_ANY,
                                "Thumbnails are scaled down for display. The written file uses "
                                "the size rule above; its exact encoded size is only known after "
                                "encoding."),
               0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
    container->SetSizer(outer);
    return container;
}

std::filesystem::path ResizeImagesTab::root() const {
    const wxString value = m_root->GetValue().Strip(wxString::both);
    return value.empty() ? std::filesystem::path() : pathFromWx(value);
}

std::filesystem::path ResizeImagesTab::output_directory() const {
    const wxString value = m_outputDir->GetValue().Strip(wxString::both);
    return value.empty() ? std::filesystem::path() : pathFromWx(value);
}

dupli::ImageResizeRule ResizeImagesTab::rule() const {
    dupli::ImageResizeRule result;
    if (m_mode->GetSelection() == 1) {
        result.mode = dupli::ImageResizeRule::Mode::Percent;
        result.percent = m_percent->GetValue();
    } else {
        result.mode = dupli::ImageResizeRule::Mode::MaxDimensions;
        result.max_width = static_cast<uint32_t>(m_maxWidth->GetValue());
        result.max_height = static_cast<uint32_t>(m_maxHeight->GetValue());
    }
    result.allow_upscale = m_allowUpscale->IsChecked();
    return result;
}

dupli::ImageOutputMode ResizeImagesTab::output_mode() const {
    return m_outputMode->GetSelection() == 1 ? dupli::ImageOutputMode::ResizeInPlace
                                             : dupli::ImageOutputMode::CreateCopies;
}

dupli::ImageLimits ResizeImagesTab::limits() const { return dupli::ImageLimits{}; }

void ResizeImagesTab::update_controls() {
    const bool idle = !m_busy;
    const bool percent = m_mode->GetSelection() == 1;
    m_root->Enable(idle);
    m_browseRoot->Enable(idle);
    m_mode->Enable(idle);
    m_maxWidth->Enable(idle && !percent);
    m_maxHeight->Enable(idle && !percent);
    m_percent->Enable(idle && percent);
    m_allowUpscale->Enable(idle);
    m_quality->Enable(idle);
    m_preserveMetadata->Enable(idle);
    m_outputMode->Enable(idle);
    const bool copies = output_mode() == dupli::ImageOutputMode::CreateCopies;
    m_outputDir->Enable(idle && copies);
    m_browseOutput->Enable(idle && copies);
    m_discover->Enable(idle && !root().empty());
    m_cancel->Enable(m_busy);
    m_selectAll->Enable(idle && m_images->GetCount() > 0);
    m_selectNone->Enable(idle && m_images->GetCount() > 0);
    m_previewButton->Enable(idle && m_images->GetCount() > 0);
    m_resize->Enable(idle && m_images->GetCount() > 0);
    m_undo->Enable(idle && !m_lastInPlace.empty());
    m_images->Enable(idle);
    if (!copies) {
        m_outputDir->SetToolTip("Not used when resizing in place; each original is backed up.");
    } else {
        m_outputDir->SetToolTip("Resized copies are written here; existing files are never overwritten.");
    }
}

void ResizeImagesTab::set_status(const wxString& text, bool error) {
    m_status->SetLabel(text);
    m_status->SetForegroundColour(error ? wxColour(250, 150, 150) : wxColour(205, 205, 210));
}

void ResizeImagesTab::on_browse_output(wxCommandEvent&) {
    const std::string path =
        FileDialogHelper::ShowBrowseFolderDialog(this, "Choose the folder for resized copies");
    if (path.empty()) return;
    m_outputDir->SetValue(fromUtf8(path));
}

void ResizeImagesTab::on_mode_changed(wxCommandEvent&) {
    update_controls();
    update_summary();
}

void ResizeImagesTab::update_summary() {
    if (!m_outcome) {
        m_summary->SetLabel(wxEmptyString);
        return;
    }
    size_t checked = 0;
    uintmax_t bytes = 0;
    for (unsigned int index = 0; index < m_images->GetCount() && index < m_outcome->result.candidates.size(); ++index) {
        if (!m_images->IsChecked(index)) continue;
        ++checked;
        bytes += m_outcome->result.candidates[index].file_size;
    }
    wxString text = wxString::Format("%zu of %zu image(s) selected (%s on disk). ", checked,
                                     m_images->GetCount(), format_bytes(bytes));
    text += "Rule: " + fromUtf8(describe_rule(rule())) + ". ";
    text += output_mode() == dupli::ImageOutputMode::CreateCopies ? "Originals are kept."
                                                                  : "In place: originals are replaced.";
    m_summary->SetLabel(text);
}

void ResizeImagesTab::on_discover(wxCommandEvent&) {
    if (m_busy) return;
    const std::filesystem::path folder = root();
    if (folder.empty()) {
        wxMessageBox("Choose a folder to search.", "No folder", wxOK | wxICON_INFORMATION, this);
        return;
    }

    ScanOptions options;
    options.roots = {folder};
    options.include_hidden = true;
    options.cross_filesystem_boundaries = false;
    options.max_entries = 200000;
    if (!output_directory().empty()) options.excludes.push_back(output_directory());

    auto outcome = std::make_shared<DiscoverOutcome>();
    m_outcome = outcome;
    m_pending = Pending::Discover;
    m_cancelFlag->store(false);
    m_busy = true;
    m_gauge->Pulse();
    set_status("Looking for images and reading their dimensions...");
    update_controls();

    auto cancel = m_cancelFlag;
    const ImageLimits current_limits = limits();
    m_task = new AsyncTask(this, [outcome, options, current_limits, cancel] {
        RealFileSystem fs;
        WxImageCodec codec;
        Blake3Hasher hasher;
        ImageResizeService service(fs, codec, hasher);
        auto discovered = service.discover(options, current_limits);
        if (cancel && cancel->load()) {
            outcome->cancelled = true;
            return;
        }
        if (discovered.has_error()) {
            outcome->error = discovered.error().message;
            return;
        }
        outcome->result = std::move(discovered.value());
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

void ResizeImagesTab::apply_discovery() {
    m_images->Clear();
    if (!m_outcome) return;
    const auto& candidates = m_outcome->result.candidates;
    for (const auto& candidate : candidates) {
        wxString label = wxFromPath(candidate.path) + "  -  ";
        if (candidate.width > 0 && candidate.height > 0) {
            label += wxString::Format("%u x %u  -  ", candidate.width, candidate.height);
        } else {
            label += "dimensions unknown  -  ";
        }
        label += format_bytes(candidate.file_size) + "  -  " + fromUtf8(to_string(candidate.format)) +
                 "  -  " + fromUtf8(candidate.status);
        m_images->Append(label);
    }
    update_summary();
    update_controls();

    wxString status = format_count(candidates.size()) + " image file(s) found.";
    if (!m_outcome->result.complete) {
        status += " The folder scan was NOT complete; some images may be missing.";
    }
    for (const auto& warning : m_outcome->result.warnings) status += " " + fromUtf8(warning);
    set_status(status, !m_outcome->result.complete);
}

void ResizeImagesTab::on_cancel(wxCommandEvent&) {
    if (!m_busy) return;
    m_cancelFlag->store(true);
    if (m_batch) m_batch->cancel.store(true);
    set_status("Cancelling... the current file must finish before the batch stops.");
}

void ResizeImagesTab::on_select_all(wxCommandEvent&) {
    for (unsigned int index = 0; index < m_images->GetCount(); ++index) m_images->Check(index, true);
    update_summary();
}

void ResizeImagesTab::on_select_none(wxCommandEvent&) {
    for (unsigned int index = 0; index < m_images->GetCount(); ++index) m_images->Check(index, false);
    update_summary();
}

void ResizeImagesTab::show_preview(size_t candidate_index) {
    if (!m_outcome || candidate_index >= m_outcome->result.candidates.size()) return;
    const ImageCandidate& candidate = m_outcome->result.candidates[candidate_index];
    if (!candidate.supported || candidate.status != "Ready") {
        m_originalLabel->SetLabel("No preview: this image cannot be decoded.");
        m_resizedLabel->SetLabel(wxEmptyString);
        m_originalPreview->SetBitmap(wxBitmap());
        m_resizedPreview->SetBitmap(wxBitmap());
        return;
    }

    set_status("Loading the selected image for preview...");
    RealFileSystem fs;
    auto size = fs.file_size(candidate.path);
    if (size.has_error()) {
        set_status("Preview failed: " + fromUtf8(size.error().message), true);
        return;
    }
    if (size.value() > limits().max_file_bytes) {
        set_status("Preview skipped: the image is larger than the file-size limit.", true);
        return;
    }
    auto opened = fs.open_read(candidate.path);
    if (opened.has_error()) {
        set_status("Preview failed: " + fromUtf8(opened.error().message), true);
        return;
    }
    auto bytes = opened.value()->read_all();
    (void)opened.value()->close();
    if (bytes.has_error()) {
        set_status("Preview failed: " + fromUtf8(bytes.error().message), true);
        return;
    }
    // Remember the content actually previewed: the batch refuses a file that
    // changed afterwards instead of resizing something the user never saw.
    Blake3Hasher hasher;
    auto digest = hasher.hash_data(bytes.value());
    if (digest.ok()) {
        m_previewDigests[candidate.path.generic_string()] = digest.value().hex;
    }

    auto decoded = m_codec.decode(bytes.value(), candidate.format, limits());
    if (decoded.has_error()) {
        set_status("Preview failed: " + fromUtf8(decoded.error().message), true);
        return;
    }

    const RasterImage& original = decoded.value();
    auto target = compute_target_size(original.width, original.height, rule());
    if (target.has_error()) {
        set_status("Cannot preview: " + fromUtf8(target.error().message), true);
        return;
    }
    RasterImage resized = resize_bilinear(original, target.value().width, target.value().height);
    if (!resized.valid()) resized = original;

    m_originalPreview->SetBitmap(raster_to_bitmap(original, kPreviewMaxWidth, kPreviewMaxHeight));
    m_resizedPreview->SetBitmap(raster_to_bitmap(resized, kPreviewMaxWidth, kPreviewMaxHeight));
    m_originalLabel->SetLabel(
        wxString::Format("%u x %u, %s on disk", original.width, original.height,
                         format_bytes(size.value())));
    m_resizedLabel->SetLabel(
        wxString::Format("%u x %u (exact encoded size is known after encoding)",
                         target.value().width, target.value().height));
    Layout();
    FitInside();
    set_status("Preview ready. No file has been written.");
}

void ResizeImagesTab::on_preview(wxCommandEvent&) {
    if (m_busy || !m_outcome) return;
    int index = m_images->GetSelection();
    if (index == wxNOT_FOUND) {
        for (unsigned int candidate = 0; candidate < m_images->GetCount(); ++candidate) {
            if (m_images->IsChecked(candidate)) {
                index = static_cast<int>(candidate);
                break;
            }
        }
    }
    if (index == wxNOT_FOUND) {
        wxMessageBox("Select or tick an image first.", "No image selected",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }
    show_preview(static_cast<size_t>(index));
}

std::vector<size_t> ResizeImagesTab::selected_ready_indices() const {
    std::vector<size_t> indices;
    if (!m_outcome) return indices;
    for (unsigned int index = 0; index < m_images->GetCount(); ++index) {
        if (!m_images->IsChecked(index)) continue;
        if (index >= m_outcome->result.candidates.size()) continue;
        const auto& candidate = m_outcome->result.candidates[index];
        if (!candidate.supported || candidate.status != "Ready") continue;
        indices.push_back(index);
    }
    return indices;
}

void ResizeImagesTab::on_resize(wxCommandEvent&) {
    if (m_busy || !m_outcome) return;
    const std::vector<size_t> indices = selected_ready_indices();
    if (indices.empty()) {
        wxMessageBox("Tick at least one image that can be decoded.", "Nothing to resize",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    // Validate the rule once so a bad rule is explained before any file is read.
    auto validation = compute_target_size(100, 100, rule());
    if (validation.has_error()) {
        wxMessageBox("The size rule is invalid:\n" + fromUtf8(validation.error().message),
                     "Invalid rule", wxOK | wxICON_ERROR, this);
        return;
    }

    const ImageOutputMode mode = output_mode();
    std::filesystem::path destination = output_directory();
    if (mode == ImageOutputMode::CreateCopies && destination.empty()) {
        // Default per the spec: "resized_images" under the selected folder.
        destination = root() / "resized_images";
        m_outputDir->SetValue(wxFromPath(destination));
    }

    std::vector<std::filesystem::path> paths;
    uintmax_t bytes = 0;
    for (size_t index : indices) {
        paths.push_back(m_outcome->result.candidates[index].path);
        bytes += m_outcome->result.candidates[index].file_size;
    }

    if (mode == ImageOutputMode::ResizeInPlace) {
        const wxString warning =
            "RESIZE IN PLACE replaces the " + format_count(paths.size()) +
            " selected original(s) (" + format_bytes(bytes) +
            ").\n\nEach original is first renamed to a \".dupli-backup\" file next to it, so the "
            "batch can be undone with the button on this tab. The backups stay on disk until you "
            "delete them; deleting them makes undo impossible.\n\nThis is destructive and cannot be "
            "undone by re-running the program.\n\nContinue?";
        if (wxMessageBox(warning, "Resize in place", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
                         this) != wxYES) {
            return;
        }
        wxTextEntryDialog confirm(this, "Type RESIZE to confirm the in-place batch:",
                                  "Confirm in-place resize");
        if (confirm.ShowModal() != wxID_OK ||
            confirm.GetValue().Strip(wxString::both) != "RESIZE") {
            set_status("In-place resize cancelled. Nothing was changed.", true);
            return;
        }
    }

    auto batch = std::make_shared<BatchState>();
    batch->total.store(paths.size());
    m_batch = batch;
    m_pending = Pending::Batch;
    m_busy = true;
    m_gauge->SetValue(0);
    m_timer.Start(200);
    set_status(wxString::Format("Resizing %zu image(s)...", paths.size()));
    update_controls();

    const ImageResizeRule current_rule = rule();
    const int quality = m_quality->GetValue();
    const bool preserve = m_preserveMetadata->IsChecked();
    const ImageLimits current_limits = limits();
    const std::map<std::string, std::string> preview_digests = m_previewDigests;

    m_task = new AsyncTask(this, [batch, paths, destination, current_rule, mode, quality, preserve,
                                  current_limits, preview_digests] {
        RealFileSystem fs;
        WxImageCodec codec;
        Blake3Hasher hasher;
        ImageResizeService service(fs, codec, hasher);

        for (const auto& path : paths) {
            if (batch->cancel.load()) break;
            ImageResizeRequest request;
            request.source = path;
            request.output_directory = destination;
            request.mode = mode;
            request.rule = current_rule;
            request.quality = quality;
            request.preserve_metadata = preserve;
            request.limits = current_limits;
            auto expected = preview_digests.find(path.generic_string());
            if (expected != preview_digests.end()) {
                request.expected_input_digest = expected->second;
            }

            auto resized = service.resize(request);
            if (resized.has_error()) {
                batch->failures.push_back(path_to_utf8(path) + ": " + resized.error().message);
            } else {
                batch->outcomes.push_back(std::move(resized.value()));
            }
            batch->done.fetch_add(1);
        }
    });
    if (m_task->Run() != wxTHREAD_NO_ERROR) {
        delete m_task;
        m_task = nullptr;
        m_timer.Stop();
        m_busy = false;
        m_pending = Pending::None;
        set_status("Could not start the batch worker; nothing was changed.", true);
        update_controls();
    }
}

void ResizeImagesTab::on_timer(wxTimerEvent&) {
    if (!m_busy) return;
    if (m_pending == Pending::Batch && m_batch) {
        const size_t done = m_batch->done.load();
        const size_t total = m_batch->total.load();
        set_status(wxString::Format("Resizing... %zu of %zu done.", done, total));
        if (total > 0) {
            m_gauge->SetValue(static_cast<int>(std::min<size_t>(100, done * 100 / total)));
        }
    } else {
        m_gauge->Pulse();
    }
}

void ResizeImagesTab::on_undo_in_place(wxCommandEvent&) {
    if (m_busy || m_lastInPlace.empty()) return;
    const wxString question =
        "Restore the " + format_count(m_lastInPlace.size()) +
        " original image(s) from their backups?\n\nEach resized file is verified against the "
        "digest recorded when it was produced; a file that changed since then is left alone.";
    if (wxMessageBox(question, "Undo in-place resize",
                     wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES) {
        return;
    }

    auto results = std::make_shared<std::vector<std::string>>();
    m_undoResults = results;
    m_pending = Pending::Undo;
    m_busy = true;
    set_status("Restoring originals from their backups...");
    update_controls();

    const std::vector<ImageResizeOutcome> outcomes = m_lastInPlace;
    m_task = new AsyncTask(this, [results, outcomes] {
        RealFileSystem fs;
        WxImageCodec codec;
        Blake3Hasher hasher;
        ImageResizeService service(fs, codec, hasher);
        for (const auto& outcome : outcomes) {
            auto restored = service.undo_in_place(outcome);
            if (restored.has_error()) {
                results->push_back(path_to_utf8(outcome.source) + ": " + restored.error().message);
            } else {
                results->push_back(path_to_utf8(outcome.source) + ": restored");
            }
        }
    });
    if (m_task->Run() != wxTHREAD_NO_ERROR) {
        delete m_task;
        m_task = nullptr;
        m_busy = false;
        m_pending = Pending::None;
        set_status("Could not start the undo worker.", true);
        update_controls();
    }
}

void ResizeImagesTab::on_task_done(wxThreadEvent&) {
    if (m_task) {
        m_task->Wait();
        const std::string error = m_task->error();
        delete m_task;
        m_task = nullptr;
        if (!error.empty()) {
            if (m_pending == Pending::Discover && m_outcome && m_outcome->error.empty()) {
                m_outcome->error = error;
            }
            if (m_pending == Pending::Batch && m_batch && m_batch->failures.empty()) {
                m_batch->failures.push_back(error);
            }
            if (m_pending == Pending::Undo && m_undoResults) {
                m_undoResults->push_back(error);
            }
        }
    }
    m_timer.Stop();
    const Pending pending = m_pending;
    m_pending = Pending::None;
    m_busy = false;
    m_gauge->SetValue(0);
    update_controls();

    if (pending == Pending::Discover) {
        if (!m_outcome) return;
        if (m_outcome->cancelled) {
            set_status("Image search cancelled.");
            return;
        }
        if (!m_outcome->ok) {
            set_status("Image search failed: " + fromUtf8(m_outcome->error), true);
            wxMessageBox("The image search failed:\n" + fromUtf8(m_outcome->error), "Search failed",
                         wxOK | wxICON_ERROR, this);
            return;
        }
        apply_discovery();
        return;
    }

    if (pending == Pending::Batch) {
        if (!m_batch) return;
        size_t succeeded = 0, skipped = 0;
        wxString details;
        m_lastInPlace.clear();
        for (const auto& outcome : m_batch->outcomes) {
            if (outcome.skipped) {
                ++skipped;
            } else if (outcome.ok) {
                ++succeeded;
                if (!outcome.backup.empty()) m_lastInPlace.push_back(outcome);
            }
            if (!outcome.message.empty()) {
                details += "\n" + wxFromPath(outcome.source) + ": " + fromUtf8(outcome.message);
            }
        }
        const size_t failed = m_batch->failures.size();
        wxString text = "Resized: " + format_count(succeeded) + "\nSkipped (no change needed): " +
                        format_count(skipped) + "\nFailed: " + format_count(failed) + "\n";
        for (const auto& failure : m_batch->failures) text += "\n" + fromUtf8(failure);
        if (!details.empty()) text += "\n" + details;
        if (m_batch->cancel.load()) text += "\n\nThe batch was cancelled; the files already done are listed above.";
        if (!m_lastInPlace.empty()) {
            text += "\n\nBackups of the originals were kept next to each file. Use \"Undo last "
                    "in-place batch\" to restore them.";
        }
        text += "\n\nThe list above reflects the state before this batch. Press \"Find images\" "
                "again to refresh it.";

        const bool all_ok = failed == 0 && !m_batch->cancel.load();
        set_status(wxString::Format("Resize %s: %zu resized, %zu skipped, %zu failed.",
                                    all_ok ? "completed" : "partially completed", succeeded,
                                    skipped, failed),
                   !all_ok);
        wxMessageBox(text, "Resize result", wxOK | (all_ok ? wxICON_INFORMATION : wxICON_WARNING),
                     this);
        m_images->Clear();
        m_outcome.reset();
        m_batch.reset();
        update_summary();
        update_controls();
        return;
    }

    if (pending == Pending::Undo) {
        if (!m_undoResults) return;
        wxString text;
        size_t failures = 0;
        for (const auto& message : *m_undoResults) {
            text += "\n" + fromUtf8(message);
            if (message.find("restored") == std::string::npos) ++failures;
        }
        set_status(failures == 0 ? "Originals restored from backups."
                                 : "Some originals could not be restored; see the message.",
                   failures != 0);
        wxMessageBox(text.empty() ? "No backup could be restored." : text, "Undo in-place resize",
                     wxOK | (failures == 0 ? wxICON_INFORMATION : wxICON_WARNING), this);
        if (failures == 0) m_lastInPlace.clear();
        update_controls();
        m_undoResults.reset();
    }
}

} // namespace gui
} // namespace dupli
