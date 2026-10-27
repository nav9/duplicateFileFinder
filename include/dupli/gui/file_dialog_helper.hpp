#pragma once

#include <wx/wx.h>
#include <wx/dirdlg.h>
#include <string>
#include <filesystem>

class FileDialogHelper {
public:
    static std::string ShowBrowseFolderDialog(wxWindow* parent, const std::string& title = "Choose folder");
    static std::string GetLastBrowsedPath();
    static void SetLastBrowsedPath(const std::string& path);
    
private:
    static std::string m_lastBrowsedPath;
};
