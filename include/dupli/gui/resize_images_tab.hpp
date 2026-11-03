#pragma once

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/choice.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "dupli/gui/image_codec_wx.hpp"
#include "dupli/image_resize.hpp"

namespace dupli {
namespace gui {

class AsyncTask;

// Resize Images: discover images below a folder, preview one, then resize a
// selected batch either into a new output folder (default) or in place.
//
// The tab is presentation only. Sizing arithmetic, limits, verification, backup
// and no-overwrite publication all live in dupli::ImageResizeService. The
// wxWidgets codec converts bytes <-> pixels; it never decides where a file goes.
//
// Cancellation is cooperative and only checked between images. A single image is
// never interrupted mid-encode because the service publishes atomically: an
// interrupted image would leave a temporary file, which is cleaned up.
class ResizeImagesTab : public wxScrolledWindow {
public:
    explicit ResizeImagesTab(wxWindow* parent);
    ~ResizeImagesTab() override;

    bool is_busy() const { return m_busy; }

    /// Restore saved resize defaults (rule, quality, output mode). A running
    /// batch keeps the snapshot it started with.
    void reload_settings();

private:
    struct DiscoverOutcome {
        bool ok = false;
        bool cancelled = false;
        std::string error;
        dupli::ImageDiscoveryResult result;
    };

    /// Shared between the UI thread and the batch worker. Only the atomic
    /// counters and the cancel flag are touched while the worker runs; the
    /// outcome vectors are read after the thread has been joined.
    struct BatchState {
        std::atomic<size_t> done{0};
        std::atomic<size_t> total{0};
        std::atomic<bool> cancel{false};
        std::vector<dupli::ImageResizeOutcome> outcomes;
        std::vector<std::string> failures;
    };

    enum class Pending { None, Discover, Batch, Undo };

    void create_controls();
    wxWindow* create_preview_panel(wxWindow* parent);
    void on_browse_output(wxCommandEvent& event);
    void on_mode_changed(wxCommandEvent& event);
    void on_discover(wxCommandEvent& event);
    void on_cancel(wxCommandEvent& event);
    void on_preview(wxCommandEvent& event);
    void on_resize(wxCommandEvent& event);
    void on_undo_in_place(wxCommandEvent& event);
    void on_select_all(wxCommandEvent& event);
    void on_select_none(wxCommandEvent& event);
    void on_task_done(wxThreadEvent& event);
    void on_timer(wxTimerEvent& event);

    std::filesystem::path root() const;
    std::filesystem::path output_directory() const;
    dupli::ImageResizeRule rule() const;
    dupli::ImageOutputMode output_mode() const;
    dupli::ImageLimits limits() const;
    std::vector<size_t> selected_ready_indices() const;
    void apply_discovery();
    void update_controls();
    void update_summary();
    void set_status(const wxString& text, bool error = false);
    void show_preview(size_t candidate_index);

    wxTextCtrl* m_root = nullptr;
    wxButton* m_browseRoot = nullptr;
    wxChoice* m_mode = nullptr;
    wxSpinCtrl* m_maxWidth = nullptr;
    wxSpinCtrl* m_maxHeight = nullptr;
    wxSpinCtrlDouble* m_percent = nullptr;
    wxCheckBox* m_allowUpscale = nullptr;
    wxSpinCtrl* m_quality = nullptr;
    wxCheckBox* m_preserveMetadata = nullptr;
    wxChoice* m_outputMode = nullptr;
    wxTextCtrl* m_outputDir = nullptr;
    wxButton* m_browseOutput = nullptr;
    wxButton* m_discover = nullptr;
    wxButton* m_cancel = nullptr;
    wxButton* m_previewButton = nullptr;
    wxButton* m_resize = nullptr;
    wxButton* m_undo = nullptr;
    wxButton* m_selectAll = nullptr;
    wxButton* m_selectNone = nullptr;
    wxCheckListBox* m_images = nullptr;
    wxStaticText* m_status = nullptr;
    wxStaticText* m_summary = nullptr;
    wxStaticBitmap* m_originalPreview = nullptr;
    wxStaticBitmap* m_resizedPreview = nullptr;
    wxStaticText* m_originalLabel = nullptr;
    wxStaticText* m_resizedLabel = nullptr;
    wxGauge* m_gauge = nullptr;

    /// Used for the preview only (UI thread). Batch work constructs its own
    /// codec inside the worker so the two never share decoder state.
    WxImageCodec m_codec;
    std::shared_ptr<DiscoverOutcome> m_outcome;
    std::shared_ptr<BatchState> m_batch;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag =
        std::make_shared<std::atomic<bool>>(false);
    /// Digests recorded when an image was previewed, keyed by path. The batch
    /// passes them to the service so a file changed after preview is skipped.
    std::map<std::string, std::string> m_previewDigests;
    std::vector<dupli::ImageResizeOutcome> m_lastInPlace;
    std::shared_ptr<std::vector<std::string>> m_undoResults;
    AsyncTask* m_task = nullptr;
    Pending m_pending = Pending::None;
    wxTimer m_timer;
    bool m_busy = false;
};

} // namespace gui
} // namespace dupli
