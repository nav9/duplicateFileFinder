#pragma once

#include "core/logger.h"
#include "core/settings.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QLineEdit;
class QComboBox;
class QCheckBox;
class QPushButton;
class QTimer;

namespace dedupvault::ui {

/**
 * @brief Log visualization page.
 *
 * Reads the logger's in-memory circular buffer: filter by level, free-text
 * search, auto-refresh, a switch that turns logging off entirely, and a
 * button that opens the log folder. All preferences are Settings keys.
 */
class LogViewerWidget : public QWidget {
    Q_OBJECT

public:
    explicit LogViewerWidget(core::AppSettings& settings, QWidget* parent = nullptr);

    /// Called when the page becomes visible (fresh data + timer control).
    void setPageActive(bool active);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void refresh();
    void onLevelFilterChanged(int index);
    void onSearchTextChanged(const QString& text);
    void toggleLogging(bool enabled);
    void openLogFolder();

private:
    void applyFilters();
    void appendRow(const core::LogEntry& entry);
    void setStatus(const QString& text, const char* role);

    core::AppSettings& m_settings;

    QTreeWidget*  m_table = nullptr;
    QLineEdit*    m_searchEdit = nullptr;
    QComboBox*    m_levelCombo = nullptr;
    QCheckBox*    m_enableCheck = nullptr;
    QCheckBox*    m_autoRefreshCheck = nullptr;
    QLabel*       m_statusLabel = nullptr;
    QPushButton*  m_openFolderBtn = nullptr;
    QTimer*       m_timer = nullptr;

    QVector<core::LogEntry> m_entries;   // last fetched (pre-search) entries
    bool m_active = false;
};

} // namespace dedupvault::ui