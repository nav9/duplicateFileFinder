#include "dupli/gui/mainframe.hpp"

#include <wx/wx.h>
#include <wx/image.h>

// Application entry point.
//
// wxInitAllImageHandlers() registers the decoders/encoders the Resize Images tab
// queries through its codec adapter; without it every image format would look
// unsupported. It touches no user files.
class DupliApp : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        auto* frame = new dupli::gui::DupliMainFrame("Duplicate Manager");
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(DupliApp);
