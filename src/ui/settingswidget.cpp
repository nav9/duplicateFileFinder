#include "settingswidget.h"
#include "core/filesystemutils.h"
#include "core/logger.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>
#include <QThread>

namespace dedupvault::ui {

SettingsWidget::SettingsWidget(core::AppSettings& settings, ThemeManager& theme,
                               QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_theme(theme)
{
    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Settings"), this);
    title->setProperty("role", "title");
    layout->addWidget(title);

    // --- Appearance ---------------------------------------------------------
    auto* themeGroup = new QGroupBox(tr("Appearance"), this);
    auto* themeLayout = new QHBoxLayout(themeGroup);
    themeLayout->addWidget(new QLabel(tr("Theme:"), themeGroup));
    m_themeCombo = new QComboBox(themeGroup);
    m_themeCombo->addItem(tr("Dark (default)"),
                          static_cast<int>(core::AppSettings::Theme::Dark));
    m_themeCombo->addItem(tr("Light"),
                          static_cast<int>(core::AppSettings::Theme::Light));
    themeLayout->addWidget(m_themeCombo);
    themeLayout->addStretch(1);
    layout->addWidget(themeGroup);

    // --- Working folder -----------------------------------------------------
    auto* workGroup = new QGroupBox(tr("Working folder"), this);
    auto* workLayout = new QVBoxLayout(workGroup);
    auto* workRow = new QHBoxLayout;
    m_workingFolderEdit = new QLineEdit(workGroup);
    m_workingFolderEdit->setReadOnly(true);
    auto* browseBtn = new QPushButton(tr("Browse…"), workGroup);
    auto* testBtn = new QPushButton(tr("Test write"), workGroup);
    m_createFolderBtn = new QPushButton(tr("Create folder"), workGroup);
    workRow->addWidget(m_workingFolderEdit, 1);
    workRow->addWidget(browseBtn);
    workRow->addWidget(testBtn);
    workRow->addWidget(m_createFolderBtn);
    workLayout->addLayout(workRow);
    m_workingFolderStatus = new QLabel(workGroup);
    m_workingFolderStatus->setWordWrap(true);
    workLayout->addWidget(m_workingFolderStatus);
    auto* workHint = new QLabel(
        tr("Catalog database, rotating log files and temporary working "
           "memory live here. Duplicates themselves are never copied into "
           "the working folder — only metadata."), workGroup);
    workHint->setProperty("role", "subtitle");
    workHint->setWordWrap(true);
    workLayout->addWidget(workHint);
    layout->addWidget(workGroup);

    // --- Logging ------------------------------------------------------------
    auto* logGroup = new QGroupBox(tr("Logging"), this);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_loggingEnabled = new QCheckBox(tr("Write logs to disk"), logGroup);
    logLayout->addWidget(m_loggingEnabled);

    auto* levelRow = new QHBoxLayout;
    levelRow->addWidget(new QLabel(tr("Minimum level:"), logGroup));
    m_logLevelCombo = new QComboBox(logGroup);
    m_logLevelCombo->addItem(tr("Trace"), static_cast<int>(core::Logger::Trace));
    m_logLevelCombo->addItem(tr("Debug"), static_cast<int>(core::Logger::Debug));
    m_logLevelCombo->addItem(tr("Info"), static_cast<int>(core::Logger::Info));
    m_logLevelCombo->addItem(tr("Warning"), static_cast<int>(core::Logger::Warning));
    m_logLevelCombo->addItem(tr("Error"), static_cast<int>(core::Logger::Error));
    levelRow->addWidget(m_logLevelCombo);
    levelRow->addStretch(1);
    logLayout->addLayout(levelRow);

    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("Log file size limit (MB):"), logGroup));
    m_logSizeSpin = new QSpinBox(logGroup);
    m_logSizeSpin->setRange(1, 1024);
    sizeRow->addWidget(m_logSizeSpin);
    sizeRow->addWidget(new QLabel(tr("Rotated files kept:"), logGroup));
    m_logFileCountSpin = new QSpinBox(logGroup);
    m_logFileCountSpin->setRange(1, 50);
    sizeRow->addWidget(m_logFileCountSpin);
    sizeRow->addStretch(1);
    logLayout->addLayout(sizeRow);
    layout->addWidget(logGroup);

    // --- Performance --------------------------------------------------------
    auto* perfGroup = new QGroupBox(tr("Performance && scanning"), this);
    auto* perfLayout = new QVBoxLayout(perfGroup);

    auto* threadsRow = new QHBoxLayout;
    threadsRow->addWidget(new QLabel(
        tr("Hashing threads (1 = single disk-friendly thread):"), perfGroup));
    m_hashThreadsSpin = new QSpinBox(perfGroup);
    m_hashThreadsSpin->setRange(1, 16);
    threadsRow->addWidget(m_hashThreadsSpin);
    threadsRow->addStretch(1);
    perfLayout->addLayout(threadsRow);

    auto* minRow = new QHBoxLayout;
    minRow->addWidget(new QLabel(tr("Ignore files smaller than (KB):"), perfGroup));
    m_minSizeSpin = new QSpinBox(perfGroup);
    m_minSizeSpin->setRange(0, 1024 * 1024);
    minRow->addWidget(m_minSizeSpin);
    minRow->addStretch(1);
    perfLayout->addLayout(minRow);

    auto* floorRow = new QHBoxLayout;
    floorRow->addWidget(new QLabel(tr("Folder overlap floor (0–1):"), perfGroup));
    m_overlapFloorSpin = new QDoubleSpinBox(perfGroup);
    m_overlapFloorSpin->setRange(0.0, 1.0);
    m_overlapFloorSpin->setSingleStep(0.05);
    m_overlapFloorSpin->setDecimals(2);
    floorRow->addWidget(m_overlapFloorSpin);
    floorRow->addStretch(1);
    perfLayout->addLayout(floorRow);

    auto* limitRow = new QHBoxLayout;
    limitRow->addWidget(new QLabel(tr("Preview limit (groups shown):"), perfGroup));
    m_previewLimitSpin = new QSpinBox(perfGroup);
    m_previewLimitSpin->setRange(100, 100000);
    limitRow->addWidget(m_previewLimitSpin);
    limitRow->addStretch(1);
    perfLayout->addLayout(limitRow);

    m_defaultDryRun = new QCheckBox(
        tr("Default folder review to dry-run (recommended)"), perfGroup);
    perfLayout->addWidget(m_defaultDryRun);
    m_defaultByteVerify = new QCheckBox(
        tr("Byte-for-byte verification before moves"), perfGroup);
    perfLayout->addWidget(m_defaultByteVerify);
    layout->addWidget(perfGroup);

    // --- Save ---------------------------------------------------------------
    auto* saveRow = new QHBoxLayout;
    saveRow->addStretch(1);
    m_saveBtn = new QPushButton(tr("Save Settings"), this);
    m_saveBtn->setProperty("role", "primary");
    saveRow->addWidget(m_saveBtn);
    layout->addLayout(saveRow);
    layout->addStretch(1);

    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsWidget::save);
    connect(browseBtn, &QPushButton::clicked, this, &SettingsWidget::browseWorkingFolder);
    connect(testBtn, &QPushButton::clicked, this, &SettingsWidget::testWorkingFolder);
    connect(m_createFolderBtn, &QPushButton::clicked,
            this, &SettingsWidget::createWorkingFolder);

    reload();
}

void SettingsWidget::setStatus(const QLabel* label, const QString& text,
                               const char* role) const {
    QLabel* w = const_cast<QLabel*>(label);
    w->setText(text);
    w->setProperty("role", role);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

void SettingsWidget::refreshWorkingFolderState() {
    const QString path = m_workingFolderEdit->text().trimmed();
    const bool exists = QFileInfo::exists(path);
    const bool writable = core::AppSettings::isFolderWritable(path);

    m_createFolderBtn->setEnabled(!exists);
    if (path.isEmpty()) {
        setStatus(m_workingFolderStatus, tr("No working folder selected."), "warn");
    } else if (writable) {
        setStatus(m_workingFolderStatus,
                  tr("✓ Writable working folder: %1").arg(path), "ok");
    } else if (!exists) {
        setStatus(m_workingFolderStatus,
                  tr("Missing: %1 — create it or browse for another folder.")
                      .arg(path), "warn");
    } else {
        setStatus(m_workingFolderStatus,
                  tr("No write privileges: %1 — browse for a writable folder.")
                      .arg(path), "error");
    }
}

void SettingsWidget::reload() {
    m_themeCombo->setCurrentIndex(
        m_settings.theme() == core::AppSettings::Theme::Dark ? 0 : 1);
    m_workingFolderEdit->setText(m_settings.workingFolder());
    refreshWorkingFolderState();

    m_loggingEnabled->setChecked(m_settings.loggingEnabled());
    m_logLevelCombo->setCurrentIndex(
        qBound(0, m_settings.logLevel(), m_logLevelCombo->count() - 1));
    m_logSizeSpin->setValue(
        static_cast<int>(m_settings.maxLogFileSize() / (1024 * 1024)));
    m_logFileCountSpin->setValue(m_settings.maxLogFiles());
    m_hashThreadsSpin->setValue(m_settings.hashingThreadCount());

    m_defaultDryRun->setChecked(
        m_settings.value(QStringLiteral("Scan/DryRun"), true).toBool());
    m_defaultByteVerify->setChecked(
        m_settings.value(QStringLiteral("Scan/ByteVerify"), true).toBool());
    m_minSizeSpin->setValue(
        m_settings.value(QStringLiteral("Scan/MinSizeKB"), 1).toInt());
    m_overlapFloorSpin->setValue(
        m_settings.value(QStringLiteral("Scan/OverlapFloor"), 0.34).toDouble());
    m_previewLimitSpin->setValue(
        m_settings.value(QStringLiteral("UI/PreviewLimit"), 4000).toInt());
}

void SettingsWidget::save() {
    // Working folder: only accept writable folders.
    const QString newPath = m_workingFolderEdit->text().trimmed();
    const QString oldPath = m_settings.workingFolder();
    if (newPath != oldPath) {
        if (!core::AppSettings::isFolderWritable(newPath)) {
            QMessageBox::warning(this, tr("Working folder not writable"),
                tr("DedupVault cannot use a folder it may not write to:\n%1")
                    .arg(newPath));
            return;
        }
        if (QMessageBox::question(this, tr("Change working folder"),
            tr("Move the working folder from\n%1\nto\n%2?\n\nThe catalog "
               "database and logs stay in the old location (they are not "
               "copied automatically); the new folder will start fresh "
               "unless you copy the files over yourself.")
                .arg(oldPath, newPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            reload();
            return;
        }
        m_settings.setWorkingFolder(newPath);
    }

    const auto newTheme = static_cast<core::AppSettings::Theme>(
        m_themeCombo->currentData().toInt());
    m_settings.setTheme(newTheme);
    m_theme.apply(newTheme);

    m_settings.setLoggingEnabled(m_loggingEnabled->isChecked());
    m_settings.setLogLevel(m_logLevelCombo->currentData().toInt());
    m_settings.setMaxLogFileSize(
        static_cast<qint64>(m_logSizeSpin->value()) * 1024 * 1024);
    m_settings.setMaxLogFiles(m_logFileCountSpin->value());
    m_settings.setHashingThreadCount(m_hashThreadsSpin->value());

    m_settings.setValue(QStringLiteral("Scan/DryRun"), m_defaultDryRun->isChecked());
    m_settings.setValue(QStringLiteral("Scan/ByteVerify"),
                        m_defaultByteVerify->isChecked());
    m_settings.setValue(QStringLiteral("Scan/MinSizeKB"), m_minSizeSpin->value());
    m_settings.setValue(QStringLiteral("Scan/OverlapFloor"),
                        m_overlapFloorSpin->value());
    m_settings.setValue(QStringLiteral("UI/PreviewLimit"), m_previewLimitSpin->value());

    // Apply logger configuration live.
    auto& logger = core::Logger::instance();
    logger.setEnabled(m_loggingEnabled->isChecked());
    logger.setMinLevel(static_cast<core::Logger::Level>(
        m_logLevelCombo->currentData().toInt()));
    logger.setMaxFileSize(m_settings.maxLogFileSize());
    logger.setMaxFiles(m_settings.maxLogFiles());

    refreshWorkingFolderState();
    emit settingsSaved();
    QMessageBox::information(this, tr("Settings saved"),
                             tr("Your preferences were saved and applied."));
}

void SettingsWidget::browseWorkingFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select working folder"), m_workingFolderEdit->text());
    if (dir.isEmpty()) return;
    m_workingFolderEdit->setText(
        core::FileSystemUtils::normalizePath(QFileInfo(dir).absoluteFilePath()));
    refreshWorkingFolderState();
}

void SettingsWidget::createWorkingFolder() {
    const QString path = m_workingFolderEdit->text().trimmed();
    if (path.isEmpty()) return;
    if (!QDir().mkpath(path)) {
        QMessageBox::warning(this, tr("Could not create folder"),
                             tr("Failed to create:\n%1").arg(path));
    }
    refreshWorkingFolderState();
}

void SettingsWidget::testWorkingFolder() {
    const QString path = m_workingFolderEdit->text().trimmed();
    if (!core::AppSettings::isFolderWritable(path)) {
        QMessageBox::warning(this, tr("Write test failed"),
            tr("DedupVault cannot write to:\n%1").arg(path));
        refreshWorkingFolderState();
        return;
    }
    const QString probe = QDir(path).filePath(QStringLiteral(".write_probe"));
    QFile f(probe);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Write test failed"), f.errorString());
        return;
    }
    f.write("ok");
    f.close();
    f.remove();
    QMessageBox::information(this, tr("Write test passed"),
        tr("DedupVault can write to:\n%1").arg(path));
    refreshWorkingFolderState();
}

} // namespace dedupvault::ui