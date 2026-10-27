#include "dupli/gui/mainframe.hpp"
#include "dupli/gui/taskhistory_panel.hpp"
#include "dupli/gui/platform_paths.hpp"
#include "dupli/gui/duplicate_files_tab.hpp"

DupliMainFrame::DupliMainFrame(const wxString& title)
    : wxFrame(NULL, wxID_ANY, title, wxDefaultPosition, wxSize(1024, 768)) {
    
    dupli::gui::PlatformPaths::ensure_directories_exist();
    SetBackgroundColour(wxColour(45, 45, 48));
    CreateTabs();
    Centre();
}

void DupliMainFrame::CreateTabs() {
    m_notebook = new wxNotebook(this, wxID_ANY);
    
    DuplicateFilesTab* dupFilesPanel = new DuplicateFilesTab(m_notebook);
    dupFilesPanel->SetToolTip("Find and manage duplicate files");
    m_notebook->AddPage(dupFilesPanel, "Duplicate Files");
    
    wxScrolledWindow* uniqueSentencesPanel = new wxScrolledWindow(m_notebook);
    uniqueSentencesPanel->SetBackgroundColour(wxColour(30, 30, 30));
    uniqueSentencesPanel->SetScrollRate(5, 5);
    wxBoxSizer* uniqueSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* uniqueTitle = new wxStaticText(uniqueSentencesPanel, wxID_ANY, "Unique Sentences");
    wxFont ut = uniqueTitle->GetFont(); ut.MakeBold(); ut.SetPointSize(ut.GetPointSize()+1); uniqueTitle->SetFont(ut);
    uniqueTitle->SetForegroundColour(wxColour(220,220,220));
    uniqueSizer->Add(uniqueTitle, 0, wxALL, 10);
    wxStaticText* uniqueInfo = new wxStaticText(uniqueSentencesPanel, wxID_ANY, "Coming soon...");
    uniqueInfo->SetForegroundColour(wxColour(200,200,200));
    uniqueSizer->Add(uniqueInfo, 0, wxALL, 10);
    uniqueSentencesPanel->SetSizer(uniqueSizer);
    uniqueSentencesPanel->SetToolTip("Remove duplicate sentences from text files");
    m_notebook->AddPage(uniqueSentencesPanel, "Unique Sentences");
    
    wxScrolledWindow* uselessFilesPanel = new wxScrolledWindow(m_notebook);
    uselessFilesPanel->SetBackgroundColour(wxColour(30, 30, 30));
    uselessFilesPanel->SetScrollRate(5, 5);
    wxBoxSizer* uselessSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* uselessTitle = new wxStaticText(uselessFilesPanel, wxID_ANY, "Useless Files");
    wxFont ut2 = uselessTitle->GetFont(); ut2.MakeBold(); ut2.SetPointSize(ut2.GetPointSize()+1); uselessTitle->SetFont(ut2);
    uselessTitle->SetForegroundColour(wxColour(220,220,220));
    uselessSizer->Add(uselessTitle, 0, wxALL, 10);
    wxStaticText* uselessInfo = new wxStaticText(uselessFilesPanel, wxID_ANY, "Coming soon...");
    uselessInfo->SetForegroundColour(wxColour(200,200,200));
    uselessSizer->Add(uselessInfo, 0, wxALL, 10);
    uselessFilesPanel->SetSizer(uselessSizer);
    uselessFilesPanel->SetToolTip("Find and clean up useless files like thumbnails and caches");
    m_notebook->AddPage(uselessFilesPanel, "Useless Files");
    
    wxScrolledWindow* resizeImagesPanel = new wxScrolledWindow(m_notebook);
    resizeImagesPanel->SetBackgroundColour(wxColour(30, 30, 30));
    resizeImagesPanel->SetScrollRate(5, 5);
    wxBoxSizer* resizeSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* resizeTitle = new wxStaticText(resizeImagesPanel, wxID_ANY, "Resize Images");
    wxFont rt = resizeTitle->GetFont(); rt.MakeBold(); rt.SetPointSize(rt.GetPointSize()+1); resizeTitle->SetFont(rt);
    resizeTitle->SetForegroundColour(wxColour(220,220,220));
    resizeSizer->Add(resizeTitle, 0, wxALL, 10);
    wxStaticText* resizeInfo = new wxStaticText(resizeImagesPanel, wxID_ANY, "Coming soon...");
    resizeInfo->SetForegroundColour(wxColour(200,200,200));
    resizeSizer->Add(resizeInfo, 0, wxALL, 10);
    resizeImagesPanel->SetSizer(resizeSizer);
    resizeImagesPanel->SetToolTip("Resize images with preview and batch processing");
    m_notebook->AddPage(resizeImagesPanel, "Resize Images");
    
    wxScrolledWindow* settingsPanel = new wxScrolledWindow(m_notebook);
    settingsPanel->SetBackgroundColour(wxColour(30, 30, 30));
    settingsPanel->SetScrollRate(5, 5);
    wxBoxSizer* settingsSizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* settingsTitle = new wxStaticText(settingsPanel, wxID_ANY, "Settings");
    wxFont st = settingsTitle->GetFont(); st.MakeBold(); st.SetPointSize(st.GetPointSize()+1); settingsTitle->SetFont(st);
    settingsTitle->SetForegroundColour(wxColour(220,220,220));
    settingsSizer->Add(settingsTitle, 0, wxALL, 10);
    wxStaticText* settingsInfo = new wxStaticText(settingsPanel, wxID_ANY, "Coming soon...");
    settingsInfo->SetForegroundColour(wxColour(200,200,200));
    settingsSizer->Add(settingsInfo, 0, wxALL, 10);
    settingsPanel->SetSizer(settingsSizer);
    settingsPanel->SetToolTip("Configure application settings and preferences");
    m_notebook->AddPage(settingsPanel, "Settings");
    
    TaskHistoryPanel* historyPanel = new TaskHistoryPanel(m_notebook);
    historyPanel->SetBackgroundColour(wxColour(30, 30, 30));
    historyPanel->SetToolTip("View and manage task history, resume or undo operations");
    m_notebook->AddPage(historyPanel, "Task History");
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    mainSizer->Add(m_notebook, 1, wxEXPAND);
    SetSizer(mainSizer);
}
