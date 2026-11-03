#include "dupli/gui/result_view.hpp"

#include "dupli/gui/wx_utils.hpp"

#include <wx/sizer.h>
#include <wx/splitter.h>

#include <algorithm>
#include <string>
#include <utility>

namespace dupli {
namespace gui {

namespace {

// A tree with hundreds of thousands of nodes would freeze the UI. Each section
// renders a bounded number of nodes and says so honestly rather than silently
// dropping results; the full list is always available in the saved report.
constexpr size_t kMaxGroupsPerSection = 500;
constexpr size_t kMaxItemsPerGroup = 500;

// Tree nodes carry their own detail text, which keeps the selection handler free
// of any state and guarantees the details always match the visible node.
class ResultItemData : public wxTreeItemData {
public:
    explicit ResultItemData(wxString details) : m_details(std::move(details)) {}
    const wxString& details() const { return m_details; }

private:
    wxString m_details;
};

const ReportCategory* find_category(const Report& report, const std::string& id) {
    for (const auto& category : report.categories) {
        if (category.id == id) return &category;
    }
    return nullptr;
}

wxString review_suffix(bool requires_review) {
    return requires_review ? "  [review required]" : wxString();
}


} // namespace

ResultView::ResultView(wxWindow* parent) : wxPanel(parent, wxID_ANY) {
    create_controls();
    clear(m_empty_message);
}

void ResultView::create_controls() {
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(wxColour(30, 30, 30));

    m_headline = new wxStaticText(this, wxID_ANY, m_empty_message);
    m_headline->SetForegroundColour(wxColour(205, 205, 210));
    m_headline->Wrap(860);
    sizer->Add(m_headline, 0, wxALL | wxEXPAND, 6);

    auto* filterBar = new wxBoxSizer(wxHORIZONTAL);
    filterBar->Add(new wxStaticText(this, wxID_ANY, "Show:"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_section_filter = new wxChoice(this, wxID_ANY);
    m_section_filter->Append("Everything");
    m_section_filter->Append("Duplicate files");
    m_section_filter->Append("Duplicate trees");
    m_section_filter->Append("Unique files");
    m_section_filter->Append("Unreadable entries");
    m_section_filter->Append("Other findings");
    m_section_filter->SetSelection(0);
    m_section_filter->SetToolTip("Choose which result category to display");
    filterBar->Add(m_section_filter, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    filterBar->Add(new wxStaticText(this, wxID_ANY, "Filter:"), 0,
                   wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_search = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                              wxSize(220, -1));
    m_search->SetHint("Path contains...");
    m_search->SetToolTip("Only show entries whose path contains this text");
    filterBar->Add(m_search, 1, wxALIGN_CENTER_VERTICAL);
    sizer->Add(filterBar, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    // wxSplitterWindow requires both panes to be created with the splitter as
    // their parent; creating them on `this` triggers a wxWidgets assertion and
    // leaves the splitter uninitialised.
    auto* splitter = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                          wxSP_LIVE_UPDATE | wxSP_3DSASH);
    splitter->SetMinimumPaneSize(80);

    m_tree = new wxTreeCtrl(splitter, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                            wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_SINGLE |
                                wxTR_LINES_AT_ROOT);
    m_tree->SetBackgroundColour(wxColour(24, 24, 26));
    m_tree->SetForegroundColour(wxColour(225, 225, 228));

    m_details = new wxTextCtrl(splitter, wxID_ANY, wxEmptyString, wxDefaultPosition,
                               wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    m_details->SetBackgroundColour(wxColour(24, 24, 26));
    m_details->SetForegroundColour(wxColour(210, 210, 215));

    splitter->SplitVertically(m_tree, m_details, 560);
    splitter->SetSashGravity(0.65);
    sizer->Add(splitter, 1, wxEXPAND | wxALL, 6);

    SetSizer(sizer);

    m_tree->Bind(wxEVT_TREE_SEL_CHANGED, &ResultView::on_selection_changed, this);
    m_section_filter->Bind(wxEVT_CHOICE, &ResultView::on_filter_changed, this);
    m_search->Bind(wxEVT_TEXT, &ResultView::on_filter_text, this);
}

void ResultView::clear(const wxString& message) {
    m_scan.reset();
    m_classification.reset();
    m_report.reset();
    m_sections.clear();
    if (m_tree) {
        m_tree->DeleteAllItems();
        m_tree->AddRoot("results");
    }
    if (m_details) m_details->SetValue(message);
    if (m_headline) m_headline->SetLabel(message);
}

size_t ResultView::duplicate_group_count() const {
    if (m_sections.empty()) return 0;
    return m_sections.front().groups.size();
}

void ResultView::show_scan(std::shared_ptr<const ScanResult> scan,
                           std::shared_ptr<const ClassificationResult> classification,
                           std::shared_ptr<const Report> report) {
    m_scan = std::move(scan);
    m_classification = std::move(classification);
    m_report = std::move(report);
    build_sections();

    if (!m_report) {
        clear("The scan did not produce a report.");
        return;
    }
    const ReportSummary& summary = m_report->summary;
    m_headline->SetLabel(
        format_count(summary.duplicate_groups) + " duplicate set(s) · " +
        format_count(summary.duplicate_files) + " duplicate file(s) · " +
        format_count(summary.unique_files) + " unique · " +
        format_bytes(summary.reclaimable_bytes) + " reclaimable");
    m_headline->Wrap(860);

    rebuild();
    m_details->SetValue(
        "Select a category or entry to see why it is listed, its full path and its size."
        "\n\nA file that could not be read is never shown as unique: it is listed under "
        "\"Unreadable entries\" so an I/O problem cannot look like a clean result.");
}

void ResultView::build_sections() {
    m_sections.clear();
    if (!m_scan || !m_classification || !m_report) return;

    // --- Duplicate files, taken straight from the grouping step -------------
    SectionData duplicates;
    duplicates.title = "Duplicate files";
    duplicates.description =
        "Files whose complete BLAKE3 digests are identical. Names, extensions and "
        "locations are irrelevant; the content is byte-for-byte the same.";
    if (!m_classification->grouping.groups.empty()) {
        duplicates.title += " (" + format_count(m_classification->grouping.groups.size()) + ")";
        for (const auto& group : m_classification->grouping.groups) {
            GroupData data;
            data.title = format_count(group.files.size()) + " identical file(s), " +
                         format_bytes(group.size) + " each";
            data.evidence = "Identical complete content, verified by BLAKE3.";
            data.details = "Identical complete content (BLAKE3 verified).\n"
                           "Digest: " + fromUtf8(group.digest_hex) +
                           "\nFile size: " + format_bytes(group.size) +
                           "\nCopies: " + format_count(group.files.size()) +
                           "\nReclaimable by moving all but one: " +
                           format_bytes(group.reclaimable_bytes()) +
                           "\n\nIdentical bytes can still serve different purposes in different "
                           "project paths. The suggestion below is only a suggestion.";
            for (size_t index = 0; index < group.files.size(); ++index) {
                const ScannedEntry& file = group.files[index];
                ItemData item;
                item.path = wxFromPath(file.path);
                item.note = index == 0 ? "Suggested copy to keep" : "Duplicate";
                item.details = "Path: " + wxFromPath(file.path) +
                               "\nRelative path: " + wxFromPath(file.relative_path) +
                               "\nSize: " + format_bytes(file.size) +
                               "\nSuggestion: " + item.note +
                               "\nDigest: " + fromUtf8(group.digest_hex);
                data.items.push_back(std::move(item));
            }
            duplicates.groups.push_back(std::move(data));
            if (duplicates.groups.size() >= kMaxGroupsPerSection) break;
        }
    }
    m_sections.push_back(std::move(duplicates));

    // --- Unique files -------------------------------------------------------
    SectionData unique;
    unique.title = "Unique files (" +
                   format_count(m_classification->grouping.unique_files.size()) + ")";
    unique.description =
        "Files whose complete content occurs exactly once. Files that could not be read "
        "are listed separately and are not proven unique.";
    if (!m_classification->grouping.unique_files.empty()) {
        GroupData data;
        data.title = format_count(m_classification->grouping.unique_files.size()) +
                     " file(s) with unique content";
        data.evidence = "Each file's BLAKE3 digest occurs exactly once in this scan.";
        data.details = data.evidence;
        size_t shown = 0;
        for (const auto& file : m_classification->grouping.unique_files) {
            if (shown++ >= kMaxItemsPerGroup) break;
            ItemData item;
            item.path = wxFromPath(file.path);
            item.details = "Path: " + wxFromPath(file.path) +
                           "\nRelative path: " + wxFromPath(file.relative_path) +
                           "\nSize: " + format_bytes(file.size) +
                           "\nContent digest occurs exactly once in this scan.";
            data.items.push_back(std::move(item));
        }
        if (m_classification->grouping.unique_files.size() > kMaxItemsPerGroup) {
            data.warnings.push_back(
                "Only the first " + format_count(kMaxItemsPerGroup) +
                " unique files are listed here; the saved report contains all of them.");
        }
        unique.groups.push_back(std::move(data));
    }
    m_sections.push_back(std::move(unique));

    // --- Report-backed categories ------------------------------------------
    struct CategorySpec {
        const char* id;
        const char* title;
        const char* description;
    };
    const CategorySpec specs[] = {
        {"unreadable_entries", "Unreadable entries",
         "Entries that could not be scanned or hashed. They are not proven unique or duplicate."},
        {"tree_duplicates", "Duplicate directory trees",
         "Whole trees with identical relative paths and file contents."},
        {"content_sets", "Content-set relationships",
         "Roots holding the same files in a different layout. Not byte-identical trees."},
        {"relative_path_conflicts", "Same relative path, different content",
         "Entries that share a relative path but differ in content; merging would conflict."},
        {"containers", "Special containers",
         "Git repositories and saved web pages that must be reviewed as a unit."},
        {"skipped_links", "Symbolic links (never followed)",
         "Links are reported for transparency but are not scanned, copied or moved."},
        {"excluded_entries", "Excluded entries",
         "Entries deliberately left out by your exclusion rules or settings."},
        {"empty_directories", "Empty directories",
         "Directories with no entries beneath them."},
    };

    for (const auto& spec : specs) {
        const ReportCategory* category = find_category(*m_report, spec.id);
        if (!category || category->groups.empty()) continue;

        SectionData section;
        section.title = fromUtf8(spec.title) + " (" + format_count(category->groups.size()) + ")";
        section.description = fromUtf8(spec.description);
        for (const auto& group : category->groups) {
            if (section.groups.size() >= kMaxGroupsPerSection) break;
            GroupData data;
            data.title = fromUtf8(group.title);
            data.evidence = fromUtf8(group.evidence);
            data.requires_review = group.requires_review;
            data.details = data.evidence;
            if (!group.digest.empty()) data.details += "\nDigest: " + fromUtf8(group.digest);
            if (group.requires_review) data.details += "\nThis item needs review before it is changed.";
            for (const auto& warning : group.warnings) {
                data.warnings.push_back(fromUtf8(warning));
                data.details += "\nWarning: " + fromUtf8(warning);
            }
            for (const auto& entry : group.items) {
                ItemData item;
                item.path = fromUtf8(entry.path);
                item.note = fromUtf8(entry.note);
                item.details = "Path: " + fromUtf8(entry.path);
                if (!entry.relative_path.empty()) {
                    item.details += "\nRelative path: " + fromUtf8(entry.relative_path);
                }
                item.details += "\nSize: " + format_bytes(entry.size);
                if (!entry.note.empty()) item.details += "\nNote: " + fromUtf8(entry.note);
                data.items.push_back(std::move(item));
            }
            section.groups.push_back(std::move(data));
        }
        m_sections.push_back(std::move(section));
    }
}

bool ResultView::group_matches(const GroupData& group, const wxString& needle) const {
    if (needle.empty()) return true;
    if (group.title.Lower().Contains(needle)) return true;
    for (const auto& item : group.items) {
        if (item.path.Lower().Contains(needle)) return true;
        if (item.note.Lower().Contains(needle)) return true;
    }
    return false;
}

void ResultView::rebuild() {
    m_tree->Freeze();
    m_tree->DeleteAllItems();
    const wxTreeItemId root = m_tree->AddRoot("results");

    if (m_sections.empty() || !m_scan) {
        m_tree->Thaw();
        return;
    }

    const int selected = m_section_filter ? m_section_filter->GetSelection() : 0;
    const wxString needle =
        m_search && !m_search->GetValue().empty() ? m_search->GetValue().Lower() : wxString();

    // Keep the filter list in the same order as the sections built above, so a
    // specific filter never hides "other" findings silently.
    auto section_visible = [selected](size_t index, const wxString& title) {
        switch (selected) {
        case 0: return true;                    // Everything
        case 1: return index == 0;              // Duplicate files
        case 2: return title.StartsWith("Duplicate directory trees");
        case 3: return title.StartsWith("Unique files");
        case 4: return title.StartsWith("Unreadable entries");
        default: return index > 2;              // Other findings
        }
    };

    size_t visible_sections = 0;
    for (size_t index = 0; index < m_sections.size(); ++index) {
        const SectionData& section = m_sections[index];
        if (!section_visible(index, section.title)) continue;

        std::vector<const GroupData*> matching;
        for (const auto& group : section.groups) {
            if (group_matches(group, needle)) matching.push_back(&group);
        }
        if (matching.empty()) continue;

        auto* section_details = new ResultItemData(section.description);
        const wxTreeItemId section_item =
            m_tree->AppendItem(root, section.title, -1, -1, section_details);
        m_tree->SetItemBold(section_item, true);
        m_tree->SetItemTextColour(section_item, wxColour(235, 235, 240));
        ++visible_sections;

        size_t rendered = 0;
        for (const GroupData* group : matching) {
            if (rendered >= kMaxGroupsPerSection) {
                m_tree->AppendItem(section_item,
                                   "... and " + format_count(matching.size() - rendered) +
                                       " more group(s); save the report to see all of them");
                break;
            }
            ++rendered;
            wxString group_label = group->title;
            if (group->requires_review) group_label += review_suffix(true);

            wxString group_details = group->details;
            if (group_details.empty()) group_details = group->evidence;
            const wxTreeItemId group_item =
                m_tree->AppendItem(section_item, group_label, -1, -1,
                                   new ResultItemData(group_details));
            if (group->requires_review) m_tree->SetItemTextColour(group_item, wxColour(240, 200, 120));

            size_t item_count = 0;
            for (const auto& item : group->items) {
                if (item_count++ >= kMaxItemsPerGroup) {
                    m_tree->AppendItem(group_item,
                                       "... and " + format_count(group->items.size() - item_count + 1) +
                                           " more entry/entries");
                    break;
                }
                wxString label = item.path;
                if (!item.note.empty()) label += "  (" + item.note + ")";
                m_tree->AppendItem(group_item, label, -1, -1, new ResultItemData(item.details));
            }
            m_tree->Expand(group_item);
        }
        m_tree->Expand(section_item);
    }

    if (visible_sections == 0 && !needle.empty()) {
        m_tree->AppendItem(root, "No entries match \"" + m_search->GetValue() + "\"");
    }
    m_tree->Thaw();
}

void ResultView::show_details_for(const wxTreeItemId& item) {
    if (!item.IsOk()) return;
    auto* data = dynamic_cast<ResultItemData*>(m_tree->GetItemData(item));
    if (data) {
        m_details->SetValue(data->details());
    } else {
        m_details->SetValue(m_tree->GetItemText(item));
    }
}

void ResultView::on_selection_changed(wxTreeEvent& event) {
    show_details_for(event.GetItem());
}

void ResultView::on_filter_changed(wxCommandEvent&) { rebuild(); }

void ResultView::on_filter_text(wxCommandEvent&) { rebuild(); }

} // namespace gui
} // namespace dupli
