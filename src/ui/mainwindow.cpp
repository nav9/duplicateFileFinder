#include "mainwindow.h"

#include "core/logger.h"
#include "ui/dashboardwidget.h"
#include "ui/folderselectionwidget.h"
#include "ui/logviewerwidget.h"
#include "ui/progresswidget.h"
#include "ui/resultswidget.h"
#include "ui/reviewwidget.h"
#include "ui/scanconfigwidget.h"
#include "ui/settingswidget.h"
#include "ui/thememanager.h"

#include <QAction>
#include <QColor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QDir>

namespace dedupvault::ui {

namespace {

QString humanBytes(qint64 bytes) {
    if (bytes < 0) bytes = 0;
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1)
               .arg(QString::fromLatin1(units[unit]));
}

QListWidgetItem* addNavItem(QListWidget* list, const QString& text, bool enabled) {
    auto* item = new QListWidgetItem(text);
    if (enabled) {
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    } else {
        item->setFlags(Qt::ItemIsEnabled); // present but not selectable
        item->setForeground(QColor(0x6a, 0x6b, 0x73));
    }
    list->addItem(item);
    return item;
}

} // namespace

MainWindow::MainWindow(core::DatabaseManager& db,
                       core::AppSettings& settings,
                       ThemeManager& theme,
                       QWidget* parent)
    : QMainWindow(parent)
    , m_db(db)
    , m_settings(settings)
    , m_theme(theme)
    , m_engine(db, settings, this)
    , m_fileOps(db, this)
    , m_segregation(db, this)
    , m_undo(db, m_fileOps, this)
{
    setWindowTitle(tr("DedupVault — Duplicate Finder & Backup Consolidation"));
    setWindowIcon(QIcon(QStringLiteral(":/gallery/app_icon.svg")));

    // Fit the window to the display that is actually available instead of
    // assuming a large desktop monitor: a 1366x768 laptop must still show
    // every control, and the New Scan page scrolls when it cannot.
    const QRect available = screen() ? screen()->availableGeometry()
                                     : QRect(0, 0, 1240, 820);
    const int targetWidth =
        qBound(640, static_cast<int>(available.width() * 0.92), 1240);
    const int targetHeight =
        qBound(480, static_cast<int>(available.height() * 0.92), 820);
    resize(targetWidth, targetHeight);
    setMinimumSize(qMin(880, targetWidth), qMin(560, targetHeight));

    auto* central = new QWidget(this);
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(8, 8, 8, 8);

    auto* body = new QHBoxLayout;
    m_sidebar = new QListWidget(central);
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->setFixedWidth(targetWidth < 1000 ? 190 : 230);
    m_sidebar->setSpacing(4);

    m_stack = new QStackedWidget(central);

    body->addWidget(m_sidebar);
    body->addWidget(m_stack, 1);
    rootLayout->addLayout(body);
    setCentralWidget(central);

    buildSidebar();
    buildPages();
    buildMenuBar();

    statusBar()->showMessage(tr("Ready"));

    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(400);
    connect(m_progressTimer, &QTimer::timeout, this, &MainWindow::pollProgress);

    connect(m_sidebar, &QListWidget::currentRowChanged, this,
            &MainWindow::navigateTo);

    connect(&m_engine, &engine::ScanEngine::phaseChanged,
            this, &MainWindow::onPhaseChanged);
    connect(&m_engine, &engine::ScanEngine::jobFinished,
            this, &MainWindow::onJobFinished);
    connect(&m_engine, &engine::ScanEngine::errorOccurred,
            this, &MainWindow::onEngineError);
    connect(&m_engine, &engine::ScanEngine::proposalsReady, this, [this]() {
        statusBar()->showMessage(tr("Duplicate groups computed."), 4000);
    });

    // Transport controls.
    connect(m_progress, &ProgressWidget::pauseRequested, this, &MainWindow::pauseScan);
    connect(m_progress, &ProgressWidget::resumeRequested, this, &MainWindow::resumeScan);
    connect(m_progress, &ProgressWidget::cancelRequested, this, &MainWindow::cancelScan);
    connect(m_progress, &ProgressWidget::resumeExistingRequested,
            this, &MainWindow::resumeInterruptedScan);



    // Dashboard journey.
    connect(m_dashboard, &DashboardWidget::startScanRequested, this, [this]() {
        navigateTo(PageScan);
    });
    connect(m_dashboard, &DashboardWidget::resumeJobRequested,
            this, &MainWindow::resumeInterruptedScan);
    connect(m_dashboard, &DashboardWidget::viewResultsRequested,
            this, [this]() { navigateTo(PageResults); });
    connect(m_dashboard, &DashboardWidget::openLogsRequested,
            this, [this]() { navigateTo(PageLogs); });
    connect(m_dashboard, &DashboardWidget::openSettingsRequested,
            this, [this]() { navigateTo(PageSettings); });

    // Apply -> refresh everything downstream.
    connect(m_results, &ResultsWidget::applyFinished,
            this, &MainWindow::onApplyFinished);
    connect(&m_fileOps, &operations::SafeFileOps::actionCompleted, this,
            [this](qint64, const QString&) { m_review->refreshHistory(); });

    connect(&m_engine, &engine::ScanEngine::jobFinished, this,
            [this](qint64, bool, const QString&) { refreshDashboard(); });
    connect(m_settingsPage, &SettingsWidget::settingsSaved, this,
            [this]() {
                refreshDashboard();
                statusBar()->showMessage(tr("Settings saved."), 4000);
            });

    // Startup reconciliation: journal vs. actual filesystem state.
    const int corrected = m_fileOps.reconcile();
    if (corrected > 0) {
        core::Logger::instance().info(
            tr("Startup reconciliation corrected %1 journal entr(y/ies).")
                .arg(corrected));
    }
    m_db.recoverInterruptedTasks();

    restoreSession();
    refreshDashboard();
    refreshResults();

    if (corrected > 0) {
        statusBar()->showMessage(
            tr("Recovered from an interrupted session (%1 journal entr(y/ies) "
               "reconciled).").arg(corrected), 8000);
    }
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Construction

void MainWindow::buildSidebar() {
    addNavItem(m_sidebar, tr("⌂  Dashboard"), true);
    addNavItem(m_sidebar, tr("＋  New Scan"), true);
    addNavItem(m_sidebar, tr("⧗  Scan Progress"), true);
    addNavItem(m_sidebar, tr("≡  Duplicate Results"), true);
    addNavItem(m_sidebar, tr("✓  Review && History"), true);
    addNavItem(m_sidebar, tr("☰  Logs"), true);
    addNavItem(m_sidebar, tr("⚙  Settings"), true);

    // Reserved: future tools announced up front, enabled when implemented.
    auto* section = new QListWidgetItem(tr("MORE TOOLS — COMING SOON"));
    section->setFlags(Qt::ItemIsEnabled);
    section->setForeground(QColor(0x5a, 0x5b, 0x63));
    m_sidebar->addItem(section);
    addNavItem(m_sidebar, tr("🖼  Duplicate images"), false);
    addNavItem(m_sidebar, tr("🎬  Duplicate videos"), false);
    addNavItem(m_sidebar, tr("¶  Unique sentences"), false);
    addNavItem(m_sidebar, tr("🗑  Junk cleaner (Thumbs.db…)"), false);

    m_sidebar->setCurrentRow(PageDashboard);
}

void MainWindow::buildPages() {
    m_dashboard = new DashboardWidget(m_stack);
    m_folders = new FolderSelectionWidget(m_stack);
    m_scanConfig = new ScanConfigWidget(m_stack);
    m_results = new ResultsWidget(m_db, m_fileOps, m_segregation, m_stack);
    m_review = new ReviewWidget(m_db, m_fileOps, m_segregation, m_undo, m_stack);
    m_logs = new LogViewerWidget(m_settings, m_stack);
    m_settingsPage = new SettingsWidget(m_settings, m_theme, m_stack);
    m_progress = new ProgressWidget(m_stack);

    // Dashboard page.
    auto* dashPage = new QWidget(m_stack);
    auto* dashLayout = new QVBoxLayout(dashPage);
    dashLayout->addWidget(m_dashboard);
    m_stack->addWidget(dashPage);

    // New scan page: the folder and option blocks scroll, while the Start
    // button lives in a fixed footer so it is reachable at any window size.
    auto* scanPage = new QWidget(m_stack);
    auto* scanLayout = new QVBoxLayout(scanPage);

    auto* scroll = new QScrollArea(scanPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Keep both bars as-needed: a narrow window may not fit the longest
    // labels, and clipped controls would be unreachable again.
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* scrollContent = new QWidget(scroll);
    auto* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setContentsMargins(0, 0, 6, 0);
    auto* scanTitle = new QLabel(tr("New Scan"), scrollContent);
    scanTitle->setProperty("role", "title");
    contentLayout->addWidget(scanTitle);
    contentLayout->addWidget(m_folders, 1);
    contentLayout->addWidget(m_scanConfig);
    scroll->setWidget(scrollContent);
    scanLayout->addWidget(scroll, 1);

    auto* footer = new QHBoxLayout;
    auto* footerHint = new QLabel(
        tr("The main folder is only ever read — nothing inside it is moved."),
        scanPage);
    footerHint->setProperty("role", "subtitle");
    footerHint->setWordWrap(true);
    footer->addWidget(footerHint, 1);

    auto* startBtn = new QPushButton(tr("Start Scan"), scanPage);
    startBtn->setProperty("role", "primary");
    startBtn->setMinimumHeight(40);
    startBtn->setMinimumWidth(160);
    footer->addWidget(startBtn, 0);
    scanLayout->addLayout(footer);

    connect(startBtn, &QPushButton::clicked, this, &MainWindow::startScan);
    m_stack->addWidget(scanPage);

    m_stack->addWidget(m_progress);
    m_stack->addWidget(m_results);
    m_stack->addWidget(m_review);
    m_stack->addWidget(m_logs);
    m_stack->addWidget(m_settingsPage);
}

void MainWindow::buildMenuBar() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));

    auto* newScanAct = fileMenu->addAction(tr("&New Scan"));
    newScanAct->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    connect(newScanAct, &QAction::triggered, this, [this]() {
        navigateTo(PageScan);
    });

    auto* refreshAct = fileMenu->addAction(tr("&Refresh Results"));
    refreshAct->setShortcut(QKeySequence::Refresh);
    connect(refreshAct, &QAction::triggered, this, &MainWindow::refreshResults);

    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    const struct { const char* label; Page page; } entries[] = {
        {"&Dashboard", PageDashboard}, {"&Progress", PageProgress},
        {"&Duplicate Results", PageResults}, {"&Review && History", PageReview},
        {"&Logs", PageLogs}, {"Se&ttings", PageSettings}
    };
    for (const auto& e : entries) {
        auto* act = viewMenu->addAction(tr(e.label));
        connect(act, &QAction::triggered, this,
                [this, e]() { navigateTo(e.page); });
    }

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAct = helpMenu->addAction(tr("&About DedupVault"));
    connect(aboutAct, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this, tr("About DedupVault"),
            tr("<b>DedupVault 1.0</b><br>"
               "Cross-platform duplicate finder and backup consolidation "
               "tool.<br><br>"
               "Your main folder is only ever read — consolidated duplicates "
               "are parked in a segregation folder and every move can be "
               "undone from Review → History &amp; Undo.<br><br>"
               "Working folder: %1").arg(m_settings.workingFolder()));
    });
}

void MainWindow::restoreSession() {
    m_folders->loadFromSettings(m_settings);
    m_scanConfig->loadFromSettings(m_settings);

    if (!m_settings.windowGeometry().isEmpty()) {
        restoreGeometry(m_settings.windowGeometry());
        // A geometry remembered from a bigger monitor must not push controls
        // (or the window itself) off the current screen.
        clampToAvailableScreen();
    }
    if (!m_settings.windowState().isEmpty()) {
        restoreState(m_settings.windowState());
    }

    updateContexts();
    if (const auto job = m_engine.resumableJob()) {
        m_progress->setResumeAvailable(true);
        m_dashboard->setResumableJob(true, *job);
    }
}

void MainWindow::clampToAvailableScreen() {
    QScreen* scr = screen() ? screen() : QGuiApplication::primaryScreen();
    if (!scr) return;

    const QRect available = scr->availableGeometry();
    const QSize fitted = size().boundedTo(available.size());
    if (fitted != size()) resize(fitted);

    const QRect frame = frameGeometry();
    int x = frame.x();
    int y = frame.y();
    if (x + frame.width() > available.right() + 1) {
        x = available.right() + 1 - frame.width();
    }
    if (y + frame.height() > available.bottom() + 1) {
        y = available.bottom() + 1 - frame.height();
    }
    x = qMax(x, available.left());
    y = qMax(y, available.top());
    if (x != frame.x() || y != frame.y()) move(x, y);
}

void MainWindow::updateContexts() {
    ResultsWidget::Context rctx;
    rctx.protectedRoot = m_folders->mainFolder();
    rctx.destinationRoot = m_folders->destinationFolder();
    rctx.jobId = static_cast<quint64>(m_engine.currentJobId());
    rctx.verifyHash =
        m_settings.value(QStringLiteral("Scan/ByteVerify"), true).toBool();
    m_results->setContext(rctx);

    ReviewWidget::Context vctx;
    vctx.protectedRoot = rctx.protectedRoot;
    vctx.destinationRoot = rctx.destinationRoot;
    vctx.jobId = rctx.jobId;
    vctx.dryRun = m_scanConfig->dryRun();
    vctx.verifyHash = rctx.verifyHash;
    m_review->setContext(vctx);
}

engine::ScanJobConfig MainWindow::currentConfig() {
    engine::ScanJobConfig config;
    config.mainFolder = m_folders->mainFolder();
    config.candidateFolders = m_folders->candidateFolders();
    config.destinationFolder = m_folders->destinationFolder();
    m_scanConfig->applyTo(config);
    return config;
}

// ---------------------------------------------------------------------------
// Navigation

void MainWindow::navigateTo(int page) {
    if (page < 0 || page >= m_stack->count()) return;
    m_stack->setCurrentIndex(page);

    QSignalBlocker blocker(m_sidebar);
    m_sidebar->setCurrentRow(page);

    if (page == PageLogs) {
        m_logs->setPageActive(true);
    } else {
        m_logs->setPageActive(false);
    }
    if (page == PageResults) refreshResults();
    if (page == PageReview) {
        m_review->refreshFolderMatches();
        m_review->refreshHistory();
    }
    if (page == PageDashboard) refreshDashboard();
}

// ---------------------------------------------------------------------------
// Scan lifecycle

void MainWindow::startScan() {
    const QString validation = m_folders->validationError();
    if (!validation.isEmpty()) {
        QMessageBox::information(this, tr("Check your folders"), validation);
        navigateTo(PageScan);
        return;
    }
    m_folders->saveToSettings(m_settings);
    m_scanConfig->saveToSettings(m_settings);

    updateContexts();
    if (!m_engine.start(currentConfig(), /*resumeExisting=*/false)) {
        return; // error already surfaced through onEngineError
    }
    updateContexts();
    m_progress->setResumeAvailable(false);
    m_progressTimer->start();
    navigateTo(PageProgress);
    statusBar()->showMessage(tr("Scan started."), 4000);
}

void MainWindow::resumeInterruptedScan() {
    if (!m_engine.resumableJob().has_value()) {
        QMessageBox::information(this, tr("Nothing to resume"),
                                 tr("There is no interrupted scan."));
        return;
    }
    if (!m_engine.start(currentConfig(), /*resumeExisting=*/true)) return;
    updateContexts();
    m_progressTimer->start();
    navigateTo(PageProgress);
    statusBar()->showMessage(tr("Resuming interrupted scan…"), 4000);
}

void MainWindow::pauseScan() {
    m_engine.pause();
    statusBar()->showMessage(tr("Paused."), 4000);
}

void MainWindow::resumeScan() {
    m_engine.resume();
    statusBar()->showMessage(tr("Resumed."), 4000);
}

void MainWindow::cancelScan() {
    const int answer = QMessageBox::question(
        this, tr("Cancel scan"),
        tr("Stop the running scan? Progress already stored in the catalog "
           "is kept, so a later scan starts faster."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) m_engine.cancel();
}

void MainWindow::pollProgress() {
    const auto p = m_engine.progress();
    m_progress->updateProgress(p);

    if (p.running) {
        statusBar()->showMessage(
            tr("%1 · %2 files · %3 hashed · %4 error(s)")
                .arg(p.phase)
                .arg(p.dbStats.fileCount)
                .arg(p.dbStats.hashedCount)
                .arg(p.errors + p.dbStats.errorCount));
    }
    if (!p.running) {
        m_progressTimer->stop();
    }
}

void MainWindow::onPhaseChanged(const QString& phase) {
    statusBar()->showMessage(tr("Phase: %1").arg(phase), 3000);
    pollProgress();
}

void MainWindow::onJobFinished(qint64 jobId, bool cancelled,
                               const QString& message) {
    m_progressTimer->stop();
    pollProgress();

    statusBar()->showMessage(message, 10000);
    core::Logger::instance().info(message);

    updateContexts();

    if (!cancelled) {
        // Journal proposals right away so the Review/History and Results
        // screens reflect the full plan (execution still per user).
        const engine::ScanJobConfig config = currentConfig();
        if (!config.destinationFolder.isEmpty()) {
            m_segregation.planFileActions(jobId, config.destinationFolder,
                                         m_engine.takeProposals());
        }
        refreshResults();
        m_review->refreshFolderMatches();
        m_review->refreshHistory();
        refreshDashboard();
        navigateTo(PageResults);
    } else {
        refreshDashboard();
    }
}

void MainWindow::onEngineError(const QString& message) {
    statusBar()->showMessage(tr("Error: %1").arg(message), 8000);
    core::Logger::instance().error(message);
    if (m_stack->currentWidget() != static_cast<QWidget*>(m_progress)) {
        QMessageBox::warning(this, tr("Scan problem"), message);
    }
    m_progressTimer->stop();
    refreshDashboard();
    pollProgress();
}

void MainWindow::onApplyFinished(int moved, int failed, const QString& summary) {
    Q_UNUSED(summary);
    // Moved files were removed from the catalog by the results page.
    refreshResults();
    refreshDashboard();
    m_review->refreshHistory();
    updateContexts();
    if (failed > 0) {
        statusBar()->showMessage(
            tr("Applied with %1 failure(s) — see Logs.").arg(failed), 8000);
    } else if (moved > 0) {
        statusBar()->showMessage(
            tr("%1 operation(s) applied. Undo is available in "
               "Review → History && Undo.").arg(moved), 8000);
    }
}

void MainWindow::refreshDashboard() {
    const QString folder = m_settings.workingFolder();
    m_dashboard->setWorkingFolder(folder,
                                  core::AppSettings::isFolderWritable(folder));

    const auto resumable = m_engine.resumableJob();
    m_dashboard->setResumableJob(resumable.has_value(),
                                 resumable.value_or(0));

    const auto stats = m_db.statistics(m_engine.currentJobId());
    if (stats.fileCount > 0) {
        QString summary = tr("%1 files catalogued · %2 in duplicate groups · "
                             "%3 recoverable · %4 error(s)")
                              .arg(stats.fileCount)
                              .arg(stats.duplicateGroupCount)
                              .arg(humanBytes(stats.wastedBytes))
                              .arg(stats.errorCount);
        if (stats.pendingReviewCount > 0) {
            summary += tr(" · %1 folder match(es) awaiting review")
                           .arg(stats.pendingReviewCount);
        }
        m_dashboard->setLastSummary(summary);
    } else {
        m_dashboard->setLastSummary(tr("No scan yet — press “Start a New Scan”."));
    }
    m_progress->setResumeAvailable(resumable.has_value());
}

void MainWindow::refreshResults() {
    // Rebuilding groups mid-scan would fight the engine: show the proposals
    // already computed, and rebuild from the catalog once the job is done.
    // The current UI selection is passed explicitly so the protected root and
    // the "main folder duplicates" option apply before a scan has started.
    const auto proposals = m_engine.isRunning()
        ? m_engine.takeProposals()
        : m_engine.reloadProposals(currentConfig());
    updateContexts();
    m_results->reload(proposals);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_engine.isRunning()) {
        const int answer = QMessageBox::question(
            this, tr("Scan in progress"),
            tr("A scan is running. Cancel it and keep the progress made so "
               "far, then quit? You can resume next time."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_engine.stopForShutdown();
    } else if (m_engine.isPaused()) {
        m_engine.stopForShutdown();
    }

    m_settings.setWindowGeometry(saveGeometry());
    m_settings.setWindowState(saveState());
    m_folders->saveToSettings(m_settings);
    m_scanConfig->saveToSettings(m_settings);
    m_settings.sync();

    core::Logger::instance().flush();
    event->accept();
}

} // namespace dedupvault::ui