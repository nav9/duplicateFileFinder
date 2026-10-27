#include "dupli/gui/file_dialog_helper.hpp"

std::string FileDialogHelper::m_lastBrowsedPath = "";

std::string FileDialogHelper::ShowBrowseFolderDialog(wxWindow* parent, const std::string& title) {
    wxString defaultPath = wxGetHomeDir();
    if (!m_lastBrowsedPath.empty()) {
        std::filesystem::path lastPath(m_lastBrowsedPath);
        if (std::filesystem::exists(lastPath) && std::filesystem::is_directory(lastPath)) {
            defaultPath = wxString(m_lastBrowsedPath.c_str(), wxConvUTF8);
        } else if (lastPath.has_parent_path()) {
            defaultPath = wxString(lastPath.parent_path().string().c_str(), wxConvUTF8);
        }
    }
    
    wxDirDialog dialog(parent, wxString(title.c_str(), wxConvUTF8), defaultPath, 
                       wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dialog.ShowModal() == wxID_OK) {
        std::string chosenPath = dialog.GetPath().ToStdString();
        SetLastBrowsedPath(chosenPath);
        return chosenPath;
    }
    return "";
}

std::string FileDialogHelper::GetLastBrowsedPath() {
    return m_lastBrowsedPath;
}

void FileDialogHelper::SetLastBrowsedPath(const std::string& path) {
    m_lastBrowsedPath = path;
}
