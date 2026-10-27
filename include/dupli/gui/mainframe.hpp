#pragma once

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>

class DupliMainFrame : public wxFrame {
public:
    DupliMainFrame(const wxString& title);
    
private:
    void CreateTabs();
    
    wxNotebook* m_notebook;
};
#include "dupli/gui/taskmanager.hpp"
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/gauge.h>

// Forward declaration
namespace dupli { namespace gui { class TaskManager; } }
void AddTaskHistoryToFrame(wxNotebook* notebook);
