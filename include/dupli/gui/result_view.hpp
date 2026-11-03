#pragma once

#include <wx/wx.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>

#include <memory>
#include <string>
#include <vector>

#include "dupli/report.hpp"
#include "dupli/service.hpp"

namespace dupli {
namespace gui {

// Categorized, filterable view of one scan. The view renders exactly what the
// backend reported; it never re-derives duplicate/unique decisions, so the GUI
// cannot disagree with the CLI or with the report file.
//
// Results are built into plain UI data first (sections -> groups -> items). That
// keeps searching/filtering cheap and stops widget code from depending on the
// shape of the backend structs.
class ResultView : public wxPanel {
public:
    explicit ResultView(wxWindow* parent);

    void show_scan(std::shared_ptr<const ScanResult> scan,
                   std::shared_ptr<const ClassificationResult> classification,
                   std::shared_ptr<const Report> report);
    void clear(const wxString& message);

    bool has_data() const { return m_scan != nullptr; }
    size_t duplicate_group_count() const;

private:
    struct ItemData {
        wxString path;
        wxString note;
        wxString details;
    };
    struct GroupData {
        wxString title;
        wxString evidence;
        wxString details;
        std::vector<ItemData> items;
        std::vector<wxString> warnings;
        bool requires_review = false;
    };
    struct SectionData {
        wxString title;
        wxString description;
        std::vector<GroupData> groups;
    };

    void create_controls();
    void build_sections();
    void rebuild();
    void on_selection_changed(wxTreeEvent& event);
    void on_filter_changed(wxCommandEvent& event);
    void on_filter_text(wxCommandEvent& event);
    bool group_matches(const GroupData& group, const wxString& needle) const;
    void show_details_for(const wxTreeItemId& item);

    wxStaticText* m_headline = nullptr;
    wxChoice* m_section_filter = nullptr;
    wxTextCtrl* m_search = nullptr;
    wxTreeCtrl* m_tree = nullptr;
    wxTextCtrl* m_details = nullptr;

    std::shared_ptr<const ScanResult> m_scan;
    std::shared_ptr<const ClassificationResult> m_classification;
    std::shared_ptr<const Report> m_report;
    std::vector<SectionData> m_sections;
    wxString m_empty_message = "Run a scan to see results here.";
};

} // namespace gui
} // namespace dupli
