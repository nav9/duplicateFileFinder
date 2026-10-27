#include "dupli/gui/mainframe.hpp"
#include <wx/wx.h>

class DupliApp : public wxApp {
public:
    virtual bool OnInit() override {
        DupliMainFrame* frame = new DupliMainFrame("Duplicate Manager");
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(DupliApp);

// Ensure program can run properly
