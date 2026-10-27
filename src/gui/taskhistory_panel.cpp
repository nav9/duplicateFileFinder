#include "dupli/gui/taskhistory_panel.hpp"

wxBEGIN_EVENT_TABLE(TaskHistoryPanel, wxPanel)
    EVT_BUTTON(wxID_REFRESH, TaskHistoryPanel::OnRefresh)
    EVT_LIST_ITEM_SELECTED(wxID_ANY, TaskHistoryPanel::OnSelectionChanged)
wxEND_EVENT_TABLE()

TaskHistoryPanel::TaskHistoryPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY) {
    SetBackgroundColour(wxColour(40, 40, 40));
    CreateControls();
}

void TaskHistoryPanel::CreateControls() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Task History");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(220, 220, 220));
    mainSizer->Add(title, 0, wxALL, 5);
    
    m_historyList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 
                                   wxLC_REPORT | wxLC_SINGLE_SEL);
    m_historyList->InsertColumn(0, "Task Name", wxLIST_FORMAT_LEFT, 150);
    m_historyList->InsertColumn(1, "Date/Time", wxLIST_FORMAT_LEFT, 120);
    m_historyList->InsertColumn(2, "Tab/Operation", wxLIST_FORMAT_LEFT, 120);
    m_historyList->InsertColumn(3, "Status", wxLIST_FORMAT_LEFT, 100);
    m_historyList->InsertColumn(4, "Undo Available", wxLIST_FORMAT_LEFT, 100);
    
    mainSizer->Add(m_historyList, 1, wxALL, 5);
    
    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_resumeBtn = new wxButton(this, wxID_ANY, "Resume Task");
    m_undoBtn = new wxButton(this, wxID_ANY, "Undo");
    m_deleteBtn = new wxButton(this, wxID_ANY, "Delete Metadata");
    wxButton* refreshBtn = new wxButton(this, wxID_REFRESH, "Refresh");
    
    m_resumeBtn->Enable(false);
    m_undoBtn->Enable(false);
    m_deleteBtn->Enable(false);
    
    btnSizer->Add(m_resumeBtn, 0, wxALL, 2);
    btnSizer->Add(m_undoBtn, 0, wxALL, 2);
    btnSizer->Add(m_deleteBtn, 0, wxALL, 2);
    btnSizer->Add(refreshBtn, 0, wxALL, 2);
    
    mainSizer->Add(btnSizer, 0, wxALL, 5);
    
    wxStaticText* detailsLabel = new wxStaticText(this, wxID_ANY, "Task Details:");
    detailsLabel->SetForegroundColour(wxColour(220, 220, 220));
    mainSizer->Add(detailsLabel, 0, wxALL, 5);
    
    m_detailsText = new wxStaticText(this, wxID_ANY, "Select a task to view details");
    m_detailsText->SetForegroundColour(wxColour(200, 200, 200));
    mainSizer->Add(m_detailsText, 0, wxALL, 5);
    
    SetSizer(mainSizer);
}

void TaskHistoryPanel::OnRefresh(wxCommandEvent& WXUNUSED(event)) { wxMessageBox("Task history will be loaded from backend in Milestone 8", "Info", wxOK | wxICON_INFORMATION); }
void TaskHistoryPanel::OnResume(wxCommandEvent& WXUNUSED(event)) { wxMessageBox("Resume functionality will be implemented in Milestone 8", "Info", wxOK | wxICON_INFORMATION); }
void TaskHistoryPanel::OnUndo(wxCommandEvent& WXUNUSED(event)) { wxMessageBox("Undo functionality will be implemented in Milestone 8", "Info", wxOK | wxICON_INFORMATION); }
void TaskHistoryPanel::OnDeleteMetadata(wxCommandEvent& WXUNUSED(event)) {
    int result = wxMessageBox("Delete metadata only? This will NOT undo the operation and may remove undo capability.", "Warning", wxYES_NO | wxICON_WARNING);
    if (result == wxYES) wxMessageBox("Metadata deletion will be implemented in Milestone 8", "Info", wxOK | wxICON_INFORMATION);
}
void TaskHistoryPanel::OnSelectionChanged(wxListEvent& WXUNUSED(event)) {
    m_resumeBtn->Enable(true);
    m_undoBtn->Enable(true);
    m_deleteBtn->Enable(true);
    m_detailsText->SetLabel("Task details will be shown here (backend integration in Milestone 8)");
}
