#include "dupli/gui/scan_thread.hpp"
#include <fstream>

wxDEFINE_EVENT(wxEVT_SCAN_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SCAN_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_SCAN_ERROR, wxCommandEvent);

ScanThread::ScanThread(wxEvtHandler* handler, const std::vector<std::string>& roots)
    : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_roots(roots) {
}

wxThread::ExitCode ScanThread::Entry() {
    if (TestDestroy()) return (wxThread::ExitCode)0;
    
    try {
        dupli::RealFileSystem fs;
        dupli::Blake3Hasher hasher;
        dupli::SimpleJournal journal;
        dupli::DuplicateManagerService service(fs, hasher, journal);
        
        dupli::ScanOptions options;
        for (const auto& r : m_roots) {
            options.roots.push_back(r);
        }
        options.follow_symlinks = false;
        
        auto scan_result = service.scan(options);
        if (!scan_result.ok()) {
            wxCommandEvent event(wxEVT_SCAN_ERROR);
            event.SetString(scan_result.error().message);
            wxQueueEvent(m_handler, event.Clone());
            return (wxThread::ExitCode)0;
        }
        
        auto class_result = service.classify(scan_result.value());
        if (!class_result.ok()) {
            wxCommandEvent event(wxEVT_SCAN_ERROR);
            event.SetString(class_result.error().message);
            wxQueueEvent(m_handler, event.Clone());
            return (wxThread::ExitCode)0;
        }
        
        wxCommandEvent event(wxEVT_SCAN_COMPLETE);
        event.SetInt((int)class_result.value().relationships.size());
        wxQueueEvent(m_handler, event.Clone());
        
    } catch (...) {
        wxCommandEvent event(wxEVT_SCAN_ERROR);
        event.SetString("Scan failed");
        wxQueueEvent(m_handler, event.Clone());
    }
    
    return (wxThread::ExitCode)0;
}
