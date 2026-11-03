#pragma once

#include <wx/wx.h>
#include <wx/checklst.h>
#include <wx/listctrl.h>

#include <memory>
#include <vector>

#include "dupli/action_planner.hpp"

namespace dupli {
namespace gui {

// Read-only preview of an action plan with an explicit approval step.
//
// Nothing is executed from here: the dialog only collects the approvals the
// planner asked for (Git work trees, in-place merges) and returns them to the
// scan tab, which runs the executor on a worker thread. Showing the full list of
// sources and destinations before any confirmation is deliberate - a plan is a
// bulk mutation and must never be applied from a summary line alone.
class PlanDialog : public wxDialog {
public:
    PlanDialog(wxWindow* parent, std::shared_ptr<const ActionPlan> plan,
               const wxString& rescueNote);

    // True only when the user pressed "Apply plan" and every safety condition
    // still held at that moment.
    bool applyRequested() const { return m_applyRequested; }
    std::vector<Approval> approvals() const { return m_approvals; }

private:
    void createControls();
    void updateApplyState();
    void onApply(wxCommandEvent& event);
    void onClose(wxCommandEvent& event);

    std::shared_ptr<const ActionPlan> m_plan;
    wxListCtrl* m_actionList = nullptr;
    wxCheckListBox* m_approvalList = nullptr;
    wxCheckBox* m_inPlaceAcknowledgement = nullptr;
    wxButton* m_applyButton = nullptr;
    bool m_applyRequested = false;
    std::vector<Approval> m_approvals;
};

} // namespace gui
} // namespace dupli
