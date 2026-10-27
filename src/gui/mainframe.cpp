#include "dupli/gui/mainframe.hpp"
#include "dupli/gui/taskhistory_panel.hpp"
#include "dupli/gui/platform_paths.hpp"

DupliMainFrame::DupliMainFrame(const wxString& title)
    : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1024, 768)) {
    
    // Initialize platform paths
    dupli::gui::PlatformPaths::ensure_directories_exist();
    
    SetBackgroundColour(wxColour(45, 45, 48));
    
    CreateTabs();
    
    Centre();
}

void DupliMainFrame::CreateTabs() {
    m_notebook = new wxNotebook(this, wxID_ANY);
    
    wxScrolledWindow* dupFilesPanel = new wxScrolledWindow(m_notebook);
    dupFilesPanel->SetBackgroundColour(wxColour(30, 30, 30));
    dupFilesPanel->SetScrollRate(5, 5);
    wxBoxSizer* dupFilesSizer = new wxBoxSizer(wxVERTICAL);
    dupFilesSizer->Add(new wxStaticText(dupFilesPanel, wxID_ANY, "Duplicate Files"), 0, wxALL, 10);
    dupFilesSizer->Add(new wxStaticText(dupFilesPanel, wxID_ANY, "Coming soon..."), 0, wxALL, 10);
    dupFilesPanel->SetSizer(dupFilesSizer);
    
    wxScrolledWindow* uniqueSentencesPanel = new wxScrolledWindow(m_notebook);
    uniqueSentencesPanel->SetBackgroundColour(wxColour(30, 30, 30));
    uniqueSentencesPanel->SetScrollRate(5, 5);
    wxBoxSizer* uniqueSizer = new wxBoxSizer(wxVERTICAL);
    uniqueSizer->Add(new wxStaticText(uniqueSentencesPanel, wxID_ANY, "Unique Sentences"), 0, wxALL, 10);
    uniqueSizer->Add(new wxStaticText(uniqueSentencesPanel, wxID_ANY, "Coming soon..."), 0, wxALL, 10);
    uniqueSentencesPanel->SetSizer(uniqueSizer);
    
    wxScrolledWindow* uselessFilesPanel = new wxScrolledWindow(m_notebook);
    uselessFilesPanel->SetBackgroundColour(wxColour(30, 30, 30));
    uselessFilesPanel->SetScrollRate(5, 5);
    wxBoxSizer* uselessSizer = new wxBoxSizer(wxVERTICAL);
    uselessSizer->Add(new wxStaticText(uselessFilesPanel, wxID_ANY, "Useless Files"), 0, wxALL, 10);
    uselessSizer->Add(new wxStaticText(uselessFilesPanel, wxID_ANY, "Coming soon..."), 0, wxALL, 10);
    uselessFilesPanel->SetSizer(uselessSizer);
    
    wxScrolledWindow* resizeImagesPanel = new wxScrolledWindow(m_notebook);
    resizeImagesPanel->SetBackgroundColour(wxColour(30, 30, 30));
    resizeImagesPanel->SetScrollRate(5, 5);
    wxBoxSizer* resizeSizer = new wxBoxSizer(wxVERTICAL);
    resizeSizer->Add(new wxStaticText(resizeImagesPanel, wxID_ANY, "Resize Images"), 0, wxALL, 10);
    resizeSizer->Add(new wxStaticText(resizeImagesPanel, wxID_ANY, "Coming soon..."), 0, wxALL, 10);
    resizeImagesPanel->SetSizer(resizeSizer);
    
    wxScrolledWindow* settingsPanel = new wxScrolledWindow(m_notebook);
    settingsPanel->SetBackgroundColour(wxColour(30, 30, 30));
    settingsPanel->SetScrollRate(5, 5);
    wxBoxSizer* settingsSizer = new wxBoxSizer(wxVERTICAL);
    settingsSizer->Add(new wxStaticText(settingsPanel, wxID_ANY, "Settings"), 0, wxALL, 10);
    settingsSizer->Add(new wxStaticText(settingsPanel, wxID_ANY, "Coming soon..."), 0, wxALL, 10);
    settingsPanel->SetSizer(settingsSizer);
    
    m_notebook->AddPage(dupFilesPanel, "Duplicate Files");
    m_notebook->AddPage(uniqueSentencesPanel, "Unique Sentences");
    m_notebook->AddPage(uselessFilesPanel, "Useless Files");
    m_notebook->AddPage(resizeImagesPanel, "Resize Images");
    m_notebook->AddPage(settingsPanel, "Settings");
    
    // Add Task History tab as per requirements
    TaskHistoryPanel* historyPanel = new TaskHistoryPanel(m_notebook);
    historyPanel->SetBackgroundColour(wxColour(30, 30, 30));
    m_notebook->AddPage(historyPanel, "Task History");
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(m_notebook, 1, wxEXPAND);
    SetSizer(mainSizer);
}
