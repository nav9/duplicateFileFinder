#pragma once

#include "core/databasemanager.h"
#include "core/settings.h"
#include "engine/scanengine.h"
#include "operations/segregationmanager.h"
#include "operations/safefileops.h"
#include "operations/undomanager.h"

#include <QMainWindow>

class QListWidget;
class QStackedWidget;
class QTimer;

namespace dedupvault::ui {

class ThemeManager;
class DashboardWidget;
class FolderSelectionWidget;
class ScanConfigWidget;
class ProgressWidget;
class ResultsWidget;
class ReviewWidget;
class LogViewerWidget;
class SettingsWidget;

/**
 * @brief Application shell: sidebar navigation (with reserved entries for
 * future tools), page stack and orchestration of the engine / operations.
 *
 * Sidebar pages: Dashboard · New Scan · Progress · Duplicate Results ·
 * Review & History · Logs · Settings — plus disabled previews of future
 * entries (Duplicate Images, Duplicate Videos, Unique Sentences, Junk
 * Cleaner) so the menu can grow without reshuffling the layout.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(core::DatabaseManager& db,
               core::AppSettings& settings,
               ThemeManager& theme,
               QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void navigateTo(int page);
    void startScan();
    void resumeInterruptedScan();
    void pauseScan();
    void resumeScan();
    void cancelScan();
    void pollProgress();
    void onPhaseChanged(const QString& phase);
    void onJobFinished(qint64 jobId, bool cancelled, const QString& message);
    void onEngineError(const QString& message);
    void onApplyFinished(int moved, int failed, const QString& summary);
    void refreshDashboard();
    void refreshResults();

private:
    void buildSidebar();
    void buildPages();
    void buildMenuBar();
    void restoreSession();
    void clampToAvailableScreen();
    void updateContexts();
    engine::ScanJobConfig currentConfig();

    core::DatabaseManager&   m_db;
    core::AppSettings&       m_settings;
    ThemeManager&            m_theme;

    engine::ScanEngine       m_engine;
    operations::SafeFileOps  m_fileOps;
    operations::SegregationManager m_segregation;
    operations::UndoManager  m_undo;

    QListWidget*     m_sidebar = nullptr;
    QStackedWidget*  m_stack = nullptr;
    DashboardWidget* m_dashboard = nullptr;
    FolderSelectionWidget* m_folders = nullptr;
    ScanConfigWidget*      m_scanConfig = nullptr;
    ProgressWidget*  m_progress = nullptr;
    ResultsWidget*   m_results = nullptr;
    ReviewWidget*    m_review = nullptr;
    LogViewerWidget* m_logs = nullptr;
    SettingsWidget*  m_settingsPage = nullptr;

    QTimer* m_progressTimer = nullptr;

    enum Page {
        PageDashboard = 0,
        PageScan,
        PageProgress,
        PageResults,
        PageReview,
        PageLogs,
        PageSettings
    };
};

} // namespace dedupvault::ui