#pragma once

#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>

#include <functional>

namespace dupli {
namespace gui {

// Settings tab: edits the durable, versioned settings file.
//
// Values are validated before they are written (the settings class repeats that
// validation when it loads), and saving is atomic. Changes apply to new tasks
// only: a running scan keeps the snapshot it started with.
class SettingsTab : public wxPanel {
public:
    explicit SettingsTab(wxWindow* parent, std::function<void()> on_saved);

private:
    void create_controls();
    void load_controls();
    void on_save(wxCommandEvent& event);
    void on_reset(wxCommandEvent& event);
    void on_open_config(wxCommandEvent& event);
    void on_open_data(wxCommandEvent& event);
    void on_open_log(wxCommandEvent& event);
    void set_feedback(const wxString& text, bool error);

    wxCheckBox* m_confirmations = nullptr;
    wxCheckBox* m_includeHidden = nullptr;
    wxCheckBox* m_crossBoundaries = nullptr;
    wxCheckBox* m_compareTrees = nullptr;
    wxSpinCtrl* m_hashBuffer = nullptr;
    wxChoice* m_defaultMode = nullptr;
    wxCheckBox* m_sentenceTrim = nullptr;
    wxCheckBox* m_sentenceCollapse = nullptr;
    wxCheckBox* m_sentenceCaseInsensitive = nullptr;
    wxTextCtrl* m_uselessPatterns = nullptr;
    wxChoice* m_resizeMode = nullptr;
    wxSpinCtrl* m_resizeMaxWidth = nullptr;
    wxSpinCtrl* m_resizeMaxHeight = nullptr;
    wxSpinCtrlDouble* m_resizePercent = nullptr;
    wxSpinCtrl* m_resizeQuality = nullptr;
    wxCheckBox* m_resizeAllowUpscale = nullptr;
    wxCheckBox* m_loggingEnabled = nullptr;
    wxChoice* m_logLevel = nullptr;
    wxSpinCtrl* m_logMaxSizeKb = nullptr;
    wxSpinCtrl* m_logMaxFiles = nullptr;
    wxStaticText* m_paths = nullptr;
    wxStaticText* m_feedback = nullptr;

    std::function<void()> m_onSaved;
};

} // namespace gui
} // namespace dupli
