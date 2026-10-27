#pragma once

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>
#include <wx/gauge.h>
#include <wx/checkbox.h>
#include <wx/statbox.h>
#include <wx/scrolwin.h>
#include <wx/listbox.h>
#include <filesystem>
#include "dupli/gui/scan_thread.hpp"

class DuplicateFilesTab : public wxScrolledWindow {
public:
    DuplicateFilesTab(wxWindow* parent);
    
private:
    void CreateControls();
    void OnAddRoot(wxCommandEvent& event);
    void OnRemoveRoot(wxCommandEvent& event);
    void OnScan(wxCommandEvent& event);
    void OnCancelScan(wxCommandEvent& event);
    void OnPreviewPlan(wxCommandEvent& event);
    void OnFolderSelection(wxCommandEvent& event);
    void OnScanComplete(wxCommandEvent& event);
    void OnScanError(wxCommandEvent& event);
    bool IsFolderNested(const std::string& newFolder);
    void UpdateScanButtonState();
    
    wxListBox* m_rootsList;
    wxButton* m_addRootBtn;
    wxButton* m_removeRootBtn;
    wxButton* m_scanBtn;
    wxButton* m_cancelBtn;
    wxButton* m_previewBtn;
    wxCheckBox* m_hiddenEntriesChk;
    wxCheckBox* m_followBoundariesChk;
    wxGauge* m_progressGauge;
    wxStaticText* m_statusText;
    ScanThread* m_scanThread;
    
    wxDECLARE_EVENT_TABLE();
};
