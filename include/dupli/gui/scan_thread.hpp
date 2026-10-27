#pragma once

#include <wx/wx.h>
#include <wx/thread.h>
#include "dupli/service.hpp"
#include "dupli/real_filesystem.hpp"
#include "dupli/hasher.hpp"
#include "dupli/journal.hpp"

class ScanThread : public wxThread {
public:
    ScanThread(wxEvtHandler* handler, const std::vector<std::string>& roots);
    virtual ExitCode Entry() override;
    
private:
    wxEvtHandler* m_handler;
    std::vector<std::string> m_roots;
};

wxDECLARE_EVENT(wxEVT_SCAN_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SCAN_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_SCAN_ERROR, wxCommandEvent);
