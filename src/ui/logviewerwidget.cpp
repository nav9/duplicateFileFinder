#include "logviewerwidget.h"

#include <QColor>
#include <QComboBox>
#include <QShowEvent>
#include <QHideEvent>
#include <QDesktopServices>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUrl>

namespace dedupvault::ui {

namespace {

QString levelName(int level) {
    return core::Logger::levelToString(static_cast<core::Logger::Level>(level));
}

QColor levelColor(int level) {
    switch (level) {
        case core::Logger::Trace:   return QColor(0x8a, 0x8b, 0x93);
        case core::Logger::Debug:   return QColor(0x9a, 0x9b, 0xa4);
        case core::Logger::Info:    return QColor(0xe6, 0xe6, 0xea);
        case core::Logger::Warning: return QColor(0xf5, 0xb9, 0x42);
        case core::Logger::Error:   return QColor(0xff, 0x6b, 0x6b);
        case core::Logger::Fatal:   return QColor(0xff, 0x44, 0x44);
        default:                    return QColor(0xe6, 0xe6, 0xea);
    }
}

constexpr int MAX_ROWS = 2000;

} // namespace

LogViewerWidget::LogViewerWidget(core::AppSettings& settings, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
{
    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Logs"), this);
    title->setProperty("role", "title");
    layout->addWidget(title);

    auto* controls = new QHBoxLayout;

    m_levelCombo = new QComboBox(this);
    m_levelCombo->addItem(tr("All levels"),
                          static_cast<int>(core::Logger::Trace));
    m_levelCombo->addItem(tr("Debug and above"),
                          static_cast<int>(core::Logger::Debug));
    m_levelCombo->addItem(tr("Info and above"),
                          static_cast<int>(core::Logger::Info));
    m_levelCombo->addItem(tr("Warnings and above"),
                          static_cast<int>(core::Logger::Warning));
    m_levelCombo->addItem(tr("Errors only"),
                          static_cast<int>(core::Logger::Error));
    controls->addWidget(new QLabel(tr("Level:"), this));
    controls->addWidget(m_levelCombo);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search log messages…"));
    m_searchEdit->setClearButtonEnabled(true);
    controls->addWidget(m_searchEdit, 1);

    m_autoRefreshCheck = new QCheckBox(tr("Live"), this);
    m_autoRefreshCheck->setChecked(true);
    controls->addWidget(m_autoRefreshCheck);

    m_enableCheck = new QCheckBox(tr("Logging enabled"), this);
    m_enableCheck->setChecked(m_settings.loggingEnabled());
    controls->addWidget(m_enableCheck);

    m_openFolderBtn = new QPushButton(tr("Open log folder"), this);
    controls->addWidget(m_openFolderBtn);
    layout->addLayout(controls);

    m_table = new QTreeWidget(this);
    m_table->setColumnCount(4);
    m_table->setHeaderLabels({tr("Time"), tr("Level"), tr("Thread"), tr("Message")});
    m_table->setRootIsDecorated(false);
    m_table->setAlternatingRowColors(true);
    m_table->setUniformRowHeights(true);
    m_table->setHeaderHidden(false);
    m_table->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setProperty("role", "subtitle");
    layout->addWidget(m_statusLabel);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);

    connect(m_timer, &QTimer::timeout, this, &LogViewerWidget::refresh);
    connect(m_levelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &LogViewerWidget::onLevelFilterChanged);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &LogViewerWidget::onSearchTextChanged);
    connect(m_enableCheck, &QCheckBox::toggled, this, &LogViewerWidget::toggleLogging);
    connect(m_autoRefreshCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_active) {
            if (on) m_timer->start(); else m_timer->stop();
        }
        setStatus(on ? tr("Live updates on") : tr("Live updates off — "
                                                 "refresh manually by switching pages"),
                  "subtitle");
    });
    connect(m_openFolderBtn, &QPushButton::clicked, this,
            &LogViewerWidget::openLogFolder);

    // Live-appended entries show up immediately.
    connect(&core::Logger::instance(), &core::Logger::newLogEntry, this,
            [this](const core::LogEntry& entry) {
                if (!m_active || !m_autoRefreshCheck->isChecked()) return;
                const int minLevel = m_levelCombo->currentData().toInt();
                if (entry.level < minLevel) return;
                const QString search = m_searchEdit->text();
                if (!search.isEmpty() &&
                    !entry.message.contains(search, Qt::CaseInsensitive)) {
                    return;
                }
                appendRow(entry);
            });

    applyFilters();
}

void LogViewerWidget::appendRow(const core::LogEntry& entry) {
    auto* item = new QTreeWidgetItem(QStringList{
        entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
        levelName(entry.level),
        entry.threadName,
        entry.message});
    item->setForeground(1, levelColor(entry.level));
    item->setToolTip(3, entry.message);
    m_table->addTopLevelItem(item);
    while (m_table->topLevelItemCount() > MAX_ROWS) {
        delete m_table->takeTopLevelItem(0);
    }
}

void LogViewerWidget::setPageActive(bool active) {
    m_active = active;
    if (active) {
        refresh();
        if (m_timer && m_autoRefreshCheck->isChecked()) m_timer->start();
    } else if (m_timer) {
        m_timer->stop();
    }
}

void LogViewerWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    setPageActive(true);
}

void LogViewerWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    setPageActive(false);
}

void LogViewerWidget::refresh() {
    applyFilters();
}

void LogViewerWidget::onLevelFilterChanged(int) {
    applyFilters();
}

void LogViewerWidget::onSearchTextChanged(const QString&) {
    applyFilters();
}

void LogViewerWidget::applyFilters() {
    const auto minLevel =
        static_cast<core::Logger::Level>(m_levelCombo->currentData().toInt());
    const QString search = m_searchEdit->text();

    m_entries = core::Logger::instance().searchEntries(
        search, minLevel, MAX_ROWS);

    m_table->setUpdatesEnabled(false);
    m_table->clear();
    m_table->setColumnCount(4);
    for (const core::LogEntry& entry : std::as_const(m_entries)) {
        auto* item = new QTreeWidgetItem(QStringList{
            entry.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
            levelName(entry.level),
            entry.threadName,
            entry.message});
        item->setForeground(1, levelColor(entry.level));
        item->setToolTip(3, entry.message);
        m_table->addTopLevelItem(item);
    }
    m_table->setUpdatesEnabled(true);

    const bool loggingOn = core::Logger::instance().isEnabled();
    setStatus(loggingOn
        ? tr("%1 entr(y/ies) shown · log file size %2 MB · %3 file(s) kept · "
             "working folder: %4")
              .arg(m_entries.size())
              .arg(m_settings.maxLogFileSize() / (1024 * 1024))
              .arg(m_settings.maxLogFiles())
              .arg(m_settings.workingFolder())
        : tr("Logging is switched OFF — nothing is written to disk. "
             "In-memory entries from this session remain visible until exit."),
        loggingOn ? "subtitle" : "warn");
}

void LogViewerWidget::setStatus(const QString& text, const char* role) {
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("role", role);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
}

void LogViewerWidget::toggleLogging(bool enabled) {
    m_settings.setLoggingEnabled(enabled);
    core::Logger::instance().setEnabled(enabled);
    setStatus(enabled ? tr("Logging enabled")
                      : tr("Logging disabled — no new log files will be written"),
              enabled ? "subtitle" : "warn");
}

void LogViewerWidget::openLogFolder() {
    const QString logDir =
        QDir(m_settings.workingFolder()).filePath(QStringLiteral("logs"));
    QDir().mkpath(logDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
}

} // namespace dedupvault::ui