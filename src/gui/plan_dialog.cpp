#include "dupli/gui/plan_dialog.hpp"

#include "dupli/gui/wx_utils.hpp"

#include <algorithm>
#include <string>

namespace dupli {
namespace gui {

namespace {

wxString mode_label(ActionMode mode) {
    switch (mode) {
    case ActionMode::PreviewOnly: return "Preview only (no changes)";
    case ActionMode::QuarantineDuplicates: return "Move duplicates to a quarantine folder";
    case ActionMode::CreateUniqueCollection: return "Collect unique files into a folder";
    case ActionMode::InPlaceMerge: return "Merge duplicates in place (advanced)";
    }
    return "Unknown";
}

} // namespace

PlanDialog::PlanDialog(wxWindow* parent, std::shared_ptr<const ActionPlan> plan,
                       const wxString& rescueNote)
    : wxDialog(parent, wxID_ANY, "Preview plan - nothing has been changed yet",
               wxDefaultPosition, wxSize(860, 560),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_plan(std::move(plan)) {
    createControls();

    if (m_plan) {
        m_actionList->Freeze();
        for (const auto& action : m_plan->actions) {
            const long row = m_actionList->InsertItem(
                m_actionList->GetItemCount(), fromUtf8(action.id));
            m_actionList->SetItem(row, 1, fromUtf8(to_string(action.kind)));
            m_actionList->SetItem(row, 2, wxFromPath(action.source));
            m_actionList->SetItem(row, 3, wxFromPath(action.destination));
            wxString status = action.requires_approval ? "Approval required" : "Ready";
            if (!action.rationale.empty()) status += " - " + fromUtf8(action.rationale);
            m_actionList->SetItem(row, 4, status);
        }
        m_actionList->Thaw();
    }

    // Only actions the planner flagged need a per-action approval; a blanket
    // "approve everything" button is intentionally absent.
    if (m_plan) {
        for (const auto& action : m_plan->actions) {
            if (!action.requires_approval) continue;
            wxString label = wxFromPath(action.source) + " -> " + wxFromPath(action.destination);
            if (!action.approval_reason.empty()) label += "  (" + fromUtf8(action.approval_reason) + ")";
            m_approvalList->Append(label);
        }
    }
    m_approvalList->Show(m_approvalList->GetCount() > 0);
    updateApplyState();

    if (!rescueNote.empty()) {
        wxMessageBox(rescueNote, "How to reverse this operation", wxOK | wxICON_INFORMATION, this);
    }
}

void PlanDialog::createControls() {
    SetBackgroundColour(wxColour(45, 45, 48));
    auto* outer = new wxBoxSizer(wxVERTICAL);

    const size_t actionCount = m_plan ? m_plan->actions.size() : 0;
    wxString summary = "This is a preview. No file has been moved, copied or changed yet.\n";
    summary += "Operation: " + mode_label(m_plan ? m_plan->mode : ActionMode::PreviewOnly) + "\n";
    summary += "Actions: " + format_count(actionCount);
    if (m_plan && !m_plan->destination.empty()) {
        summary += "\nDestination: " + wxFromPath(m_plan->destination);
    }
    if (m_plan && !m_plan->plan_id.empty()) {
        summary += "\nPlan id: " + fromUtf8(m_plan->plan_id) +
                   "  (approvals are bound to this exact plan)";
    }
    auto* summaryText = new wxStaticText(this, wxID_ANY, summary);
    summaryText->SetForegroundColour(wxColour(230, 230, 235));
    outer->Add(summaryText, 0, wxALL | wxEXPAND, 10);

    if (m_plan && !m_plan->warnings.empty()) {
        wxString warnings;
        for (const auto& warning : m_plan->warnings) warnings += "• " + fromUtf8(warning) + "\n";
        auto* warningText = new wxStaticText(this, wxID_ANY, warnings);
        warningText->SetForegroundColour(wxColour(240, 200, 120));
        outer->Add(warningText, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
    }
    if (m_plan && !m_plan->unresolved_conflicts.empty()) {
        wxString conflicts = "Unresolved conflicts - this plan cannot be applied:\n";
        for (const auto& conflict : m_plan->unresolved_conflicts) {
            conflicts += "• " + fromUtf8(conflict) + "\n";
        }
        auto* conflictText = new wxStaticText(this, wxID_ANY, conflicts);
        conflictText->SetForegroundColour(wxColour(250, 150, 150));
        outer->Add(conflictText, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
    }

    m_actionList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 220),
                                  wxLC_REPORT | wxLC_SINGLE_SEL);
    m_actionList->InsertColumn(0, "Action", wxLIST_FORMAT_LEFT, 70);
    m_actionList->InsertColumn(1, "Kind", wxLIST_FORMAT_LEFT, 60);
    m_actionList->InsertColumn(2, "Source", wxLIST_FORMAT_LEFT, 250);
    m_actionList->InsertColumn(3, "Destination", wxLIST_FORMAT_LEFT, 250);
    m_actionList->InsertColumn(4, "Status", wxLIST_FORMAT_LEFT, 200);
    m_actionList->SetBackgroundColour(wxColour(28, 28, 30));
    m_actionList->SetForegroundColour(wxColour(225, 225, 228));
    outer->Add(m_actionList, 1, wxEXPAND | wxALL, 10);

    auto* approvalLabel = new wxStaticText(
        this, wxID_ANY,
        "These actions need an explicit approval because they touch a Git work tree or change "
        "files in place. Tick the ones you have reviewed:");
    approvalLabel->SetForegroundColour(wxColour(230, 230, 235));
    outer->Add(approvalLabel, 0, wxLEFT | wxRIGHT | wxEXPAND, 10);
    m_approvalList = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 90));
    m_approvalList->SetBackgroundColour(wxColour(28, 28, 30));
    m_approvalList->SetForegroundColour(wxColour(225, 225, 228));
    outer->Add(m_approvalList, 0, wxEXPAND | wxALL, 10);

    m_inPlaceAcknowledgement = new wxCheckBox(
        this, wxID_ANY,
        "I understand an in-place merge modifies the original folders and cannot be undone by "
        "simply re-running the program.");
    m_inPlaceAcknowledgement->SetForegroundColour(wxColour(250, 190, 190));
    outer->Add(m_inPlaceAcknowledgement, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer(1);
    m_applyButton = new wxButton(this, wxID_APPLY, "Apply this plan");
    auto* closeButton = new wxButton(this, wxID_CANCEL, "Close");
    buttons->Add(m_applyButton, 0, wxRIGHT, 8);
    buttons->Add(closeButton, 0);
    outer->Add(buttons, 0, wxEXPAND | wxALL, 10);

    SetSizer(outer);
    Layout();

    m_applyButton->Bind(wxEVT_BUTTON, &PlanDialog::onApply, this);
    closeButton->Bind(wxEVT_BUTTON, &PlanDialog::onClose, this);
    m_inPlaceAcknowledgement->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { updateApplyState(); });
    m_approvalList->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent&) { updateApplyState(); });
}

void PlanDialog::updateApplyState() {
    bool ready = m_plan != nullptr;
    if (m_plan) {
        ready = ready && m_plan->executable && m_plan->actions.empty() == false;
        if (m_plan->mode == ActionMode::InPlaceMerge) {
            ready = ready && m_inPlaceAcknowledgement->IsChecked();
        }
        // Every approval-gated action must be ticked; missing approvals would
        // otherwise be rejected later by the validator with a less obvious error.
        ready = ready && (static_cast<size_t>(m_approvalList->GetCount()) == 0 ||
                          [&] {
                              for (unsigned int i = 0; i < m_approvalList->GetCount(); ++i) {
                                  if (!m_approvalList->IsChecked(i)) return false;
                              }
                              return true;
                          }());
    }
    m_applyButton->Enable(ready);
}

void PlanDialog::onApply(wxCommandEvent&) {
    if (!m_plan) return;

    m_approvals.clear();
    size_t approvalIndex = 0;
    for (const auto& action : m_plan->actions) {
        if (!action.requires_approval) continue;
        if (approvalIndex < static_cast<size_t>(m_approvalList->GetCount()) &&
            m_approvalList->IsChecked(static_cast<unsigned int>(approvalIndex))) {
            Approval approval;
            approval.action_id = action.id;
            approval.source = path_to_utf8(action.source);
            approval.expected_digest = action.expected_digest;
            m_approvals.push_back(std::move(approval));
        }
        ++approvalIndex;
    }

    wxString question =
        "Apply " + format_count(m_plan->actions.size()) + " action(s) now?\n\n";
    if (!m_plan->destination.empty()) question += "Destination: " + wxFromPath(m_plan->destination) + "\n";
    question +=
        "Every source is re-verified immediately before it is touched. If anything changed since "
        "the scan, that action is refused instead of being applied to different content.\n\n"
        "Completed actions are journaled and can be undone from the Task History tab.";
    if (wxMessageBox(question, "Confirm plan", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) !=
        wxYES) {
        return;
    }

    m_applyRequested = true;
    EndModal(wxID_OK);
}

void PlanDialog::onClose(wxCommandEvent&) {
    m_applyRequested = false;
    EndModal(wxID_CANCEL);
}

} // namespace gui
} // namespace dupli
