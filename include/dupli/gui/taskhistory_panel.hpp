#pragma once

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/sizer.h>

class TaskHistoryPanel : public wxPanel {
public:
    TaskHistoryPanel(wxWindow* parent);
    
private:
    void CreateControls();
    void AdjustColumnWidths();
    void OnRefresh(wxCommandEvent& event);
    void OnResume(wxCommandEvent& event);
    void OnUndo(wxCommandEvent& event);
    void OnDeleteMetadata(wxCommandEvent& event);
    void OnSelectionChanged(wxListEvent& event);
    void OnSize(wxSizeEvent& event);
    
    wxListCtrl* m_historyList;
    wxButton* m_resumeBtn;
    wxButton* m_undoBtn;
    wxButton* m_deleteBtn;
    wxStaticText* m_detailsText;
    
    wxDECLARE_EVENT_TABLE();
};
