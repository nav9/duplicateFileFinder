#include "dupli/gui/duplicate_files_tab.hpp"
#include "dupli/gui/file_dialog_helper.hpp"
#include "dupli/gui/scan_thread.hpp"
#include <wx/msgdlg.h>

wxBEGIN_EVENT_TABLE(DuplicateFilesTab, wxScrolledWindow)
    EVT_BUTTON(wxID_ANY+1, DuplicateFilesTab::OnAddRoot)
    EVT_BUTTON(wxID_ANY+2, DuplicateFilesTab::OnRemoveRoot)
    EVT_BUTTON(wxID_ANY+3, DuplicateFilesTab::OnScan)
    EVT_BUTTON(wxID_ANY+4, DuplicateFilesTab::OnCancelScan)
    EVT_BUTTON(wxID_ANY+5, DuplicateFilesTab::OnPreviewPlan)
    EVT_LISTBOX(wxID_ANY, DuplicateFilesTab::OnFolderSelection)
    EVT_COMMAND(wxID_ANY, wxEVT_SCAN_COMPLETE, DuplicateFilesTab::OnScanComplete)
    EVT_COMMAND(wxID_ANY, wxEVT_SCAN_ERROR, DuplicateFilesTab::OnScanError)
wxEND_EVENT_TABLE()

DuplicateFilesTab::DuplicateFilesTab(wxWindow* parent)
    : wxScrolledWindow(parent, wxID_ANY), m_scanThread(nullptr) {
    SetBackgroundColour(wxColour(30, 30, 30));
    SetScrollRate(5, 5);
    CreateControls();
}

void DuplicateFilesTab::CreateControls() {
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Duplicate Files");
    wxFont titleFont = title->GetFont();
    titleFont.MakeBold();
    titleFont.SetPointSize(titleFont.GetPointSize() + 1);
    title->SetFont(titleFont);
    title->SetForegroundColour(wxColour(220, 220, 220));
    mainSizer->Add(title, 0, wxALL, 5);
    
    wxStaticText* explanation = new wxStaticText(this, wxID_ANY, 
        "Scan for exact duplicate files using BLAKE3 hashing. Preview before any destructive operations.");
    explanation->SetForegroundColour(wxColour(200, 200, 200));
    mainSizer->Add(explanation, 0, wxALL, 5);
    
    wxStaticBoxSizer* foldersSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Folders to Scan");
    m_rootsList = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 80));
    m_rootsList->SetToolTip("List of folders to search for duplicates");
    foldersSizer->Add(m_rootsList, 1, wxALL | wxEXPAND, 2);
    
    wxBoxSizer* foldersBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_addRootBtn = new wxButton(this, wxID_ANY+1, "Add Folder");
    m_removeRootBtn = new wxButton(this, wxID_ANY+2, "Remove Folder");
    m_addRootBtn->SetToolTip("Add a folder to scan for duplicates");
    m_removeRootBtn->SetToolTip("Remove selected folder from scan list");
    m_removeRootBtn->Enable(false);
    foldersBtnSizer->Add(m_addRootBtn, 0, wxALL, 2);
    foldersBtnSizer->Add(m_removeRootBtn, 0, wxALL, 2);
    foldersSizer->Add(foldersBtnSizer, 0, wxALL, 2);
    
    mainSizer->Add(foldersSizer, 0, wxALL | wxEXPAND, 5);
    
    wxStaticBoxSizer* optionsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Scan Options");
    m_hiddenEntriesChk = new wxCheckBox(this, wxID_ANY, "Include hidden entries");
    m_followBoundariesChk = new wxCheckBox(this, wxID_ANY, "Follow filesystem boundaries");
    m_hiddenEntriesChk->SetToolTip("Include hidden files and folders in the scan");
    m_followBoundariesChk->SetToolTip("Allow scanning across different filesystem boundaries");
    optionsSizer->Add(m_hiddenEntriesChk, 0, wxALL, 2);
    optionsSizer->Add(m_followBoundariesChk, 0, wxALL, 2);
    mainSizer->Add(optionsSizer, 0, wxALL | wxEXPAND, 5);
    
    wxBoxSizer* actionsSizer = new wxBoxSizer(wxHORIZONTAL);
    m_scanBtn = new wxButton(this, wxID_ANY+3, "Scan and Analyse");
    m_cancelBtn = new wxButton(this, wxID_ANY+4, "Cancel Scan");
    m_previewBtn = new wxButton(this, wxID_ANY+5, "Preview Plan");
    m_scanBtn->SetToolTip("Start scanning folders for duplicate files");
    m_cancelBtn->SetToolTip("Cancel the current scan operation");
    m_previewBtn->SetToolTip("Preview the action plan before making changes");
    m_scanBtn->Enable(false);
    m_cancelBtn->Enable(false);
    m_previewBtn->Enable(false);
    actionsSizer->Add(m_scanBtn, 0, wxALL, 2);
    actionsSizer->Add(m_cancelBtn, 0, wxALL, 2);
    actionsSizer->Add(m_previewBtn, 0, wxALL, 2);
    mainSizer->Add(actionsSizer, 0, wxALL, 5);
    
    wxStaticBoxSizer* progressSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Progress");
    m_progressGauge = new wxGauge(this, wxID_ANY, 100);
    m_statusText = new wxStaticText(this, wxID_ANY, "Ready - Add folders to scan");
    m_statusText->SetForegroundColour(wxColour(200, 200, 200));
    progressSizer->Add(m_progressGauge, 0, wxALL | wxEXPAND, 2);
    progressSizer->Add(m_statusText, 0, wxALL, 2);
    mainSizer->Add(progressSizer, 0, wxALL | wxEXPAND, 5);
    
    wxStaticBoxSizer* resultsSizer = new wxStaticBoxSizer(wxVERTICAL, this, "Results");
    wxStaticText* resultsLabel = new wxStaticText(this, wxID_ANY, "Duplicate groups will appear here after scanning");
    resultsLabel->SetForegroundColour(wxColour(200, 200, 200));
    resultsSizer->Add(resultsLabel, 0, wxALL, 2);
    mainSizer->Add(resultsSizer, 1, wxALL | wxEXPAND, 5);
    
    SetSizer(mainSizer);
}

bool DuplicateFilesTab::IsFolderNested(const std::string& newFolder) {
    std::filesystem::path newPath(newFolder);
    for (unsigned int i = 0; i < m_rootsList->GetCount(); ++i) {
        std::string existing = m_rootsList->GetString(i).ToStdString();
        std::filesystem::path existingPath(existing);
        if (newPath.string().find(existingPath.string() + "/") == 0 && newPath != existingPath) {
            wxMessageBox("Folder '" + newPath.filename().string() + "' is nested within '" + existingPath.filename().string() + "'\nNested folders are not allowed.", 
                        "Nested Folder Not Allowed", wxOK | wxICON_WARNING);
            return true;
        }
        if (existingPath.string().find(newPath.string() + "/") == 0 && existingPath != newPath) {
            wxMessageBox("Folder '" + existingPath.filename().string() + "' is nested within '" + newPath.filename().string() + "'\nNested folders are not allowed.", 
                        "Nested Folder Not Allowed", wxOK | wxICON_WARNING);
            return true;
        }
    }
    return false;
}

void DuplicateFilesTab::UpdateScanButtonState() {
    if (m_rootsList->GetCount() > 0) {
        m_scanBtn->Enable(true);
        m_statusText->SetLabel("Ready to scan");
    } else {
        m_scanBtn->Enable(false);
        m_statusText->SetLabel("Ready - Add folders to scan");
    }
}

void DuplicateFilesTab::OnAddRoot(wxCommandEvent& WXUNUSED(event)) {
    std::string path = FileDialogHelper::ShowBrowseFolderDialog(this, "Choose folder to scan");
    if (!path.empty()) {
        if (IsFolderNested(path)) {
            return;
        }
        for (unsigned int i = 0; i < m_rootsList->GetCount(); ++i) {
            if (m_rootsList->GetString(i).ToStdString() == path) {
                wxMessageBox("This folder has already been added.", "Duplicate Folder", wxOK | wxICON_INFORMATION);
                return;
            }
        }
        m_rootsList->Append(wxString(path.c_str(), wxConvUTF8));
        UpdateScanButtonState();
    }
}

void DuplicateFilesTab::OnRemoveRoot(wxCommandEvent& WXUNUSED(event)) {
    int sel = m_rootsList->GetSelection();
    if (sel != wxNOT_FOUND) {
        m_rootsList->Delete(sel);
        UpdateScanButtonState();
    }
}

void DuplicateFilesTab::OnFolderSelection(wxCommandEvent& WXUNUSED(event)) {
    m_removeRootBtn->Enable(m_rootsList->GetSelection() != wxNOT_FOUND);
}

void DuplicateFilesTab::OnScan(wxCommandEvent& WXUNUSED(event)) {
    if (m_rootsList->GetCount() == 0) {
        wxMessageBox("Please add at least one folder to scan", "Error", wxOK | wxICON_ERROR);
        return;
    }
    std::vector<std::string> roots;
    for (unsigned int i = 0; i < m_rootsList->GetCount(); ++i) {
        roots.push_back(m_rootsList->GetString(i).ToStdString());
    }
    m_statusText->SetLabel("Scanning for duplicates...");
    m_progressGauge->SetValue(0);
    m_scanBtn->Enable(false);
    m_cancelBtn->Enable(true);
    m_previewBtn->Enable(false);
    m_scanThread = new ScanThread(this, roots);
    m_scanThread->Run();
}

void DuplicateFilesTab::OnCancelScan(wxCommandEvent& WXUNUSED(event)) {
    m_statusText->SetLabel("Scan cancelled");
    m_progressGauge->SetValue(0);
    m_scanBtn->Enable(true);
    m_cancelBtn->Enable(false);
    UpdateScanButtonState();
}

void DuplicateFilesTab::OnPreviewPlan(wxCommandEvent& WXUNUSED(event)) {
    wxMessageBox("Plan preview will be available after scanning", "Info", wxOK | wxICON_INFORMATION);
}

void DuplicateFilesTab::OnScanComplete(wxCommandEvent& event) {
    m_statusText->SetLabel("Scan complete - " + std::to_string(event.GetInt()) + " duplicate groups found");
    m_progressGauge->SetValue(100);
    m_scanBtn->Enable(true);
    m_cancelBtn->Enable(false);
    m_previewBtn->Enable(event.GetInt() > 0);
}

void DuplicateFilesTab::OnScanError(wxCommandEvent& event) {
    m_statusText->SetLabel("Scan error");
    m_progressGauge->SetValue(0);
    m_scanBtn->Enable(true);
    m_cancelBtn->Enable(false);
    UpdateScanButtonState();
    wxMessageBox("Scan failed: " + event.GetString(), "Error", wxOK | wxICON_ERROR);
}
