#include "scanconfigwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QThread>

namespace dedupvault::ui {

ScanConfigWidget::ScanConfigWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox(tr("Scan options"), this);
    auto* grid = new QVBoxLayout(group);

    m_dryRunCheck = new QCheckBox(
        tr("Dry run for folder review — plan subtree moves, move nothing "
           "(recommended)"), group);
    m_dryRunCheck->setChecked(true);
    grid->addWidget(m_dryRunCheck);

    m_dryRunHint = new QLabel(
        tr("Applies to folder approvals in Review: validated and journaled "
           "only, executable later. File moves on the Results page are "
           "chosen there with the Dry Run and Segregate buttons."), group);
    m_dryRunHint->setProperty("role", "subtitle");
    m_dryRunHint->setWordWrap(true);
    grid->addWidget(m_dryRunHint);

    m_completeHashCheck = new QCheckBox(
        tr("Complete hashing — hash every file, not just size matches "
           "(slower)"), group);
    grid->addWidget(m_completeHashCheck);

    m_byteVerifyCheck = new QCheckBox(
        tr("Byte-for-byte verification before each move (SHA-256 re-check)"),
        group);
    m_byteVerifyCheck->setChecked(true);
    grid->addWidget(m_byteVerifyCheck);

    m_analyzeFoldersCheck = new QCheckBox(
        tr("Analyze folder structure (subtree matches, projects)"), group);
    m_analyzeFoldersCheck->setChecked(true);
    grid->addWidget(m_analyzeFoldersCheck);

    m_mainFolderDupCheck = new QCheckBox(
        tr("Also report duplicates that exist inside the main folder"), group);
    m_mainFolderDupCheck->setToolTip(
        tr("Finds copies that duplicate each other within the main folder. "
           "They are listed for review only: the main folder is protected and "
           "nothing in it is ever moved."));
    grid->addWidget(m_mainFolderDupCheck);
    auto* mainDupHint = new QLabel(
        tr("Review only — files in the main folder are never moved."), group);
    mainDupHint->setProperty("role", "subtitle");
    mainDupHint->setWordWrap(true);
    mainDupHint->setIndent(22);
    grid->addWidget(mainDupHint);

    m_followSymlinkCheck = new QCheckBox(
        tr("Follow directory symlinks (off: never bypass the protected "
           "folder)"), group);
    m_followSymlinkCheck->setChecked(false);
    grid->addWidget(m_followSymlinkCheck);

    auto* threadRow = new QHBoxLayout;
    auto* threadLabel = new QLabel(tr("Hashing threads:"), group);
    m_hashThreadsSpin = new QSpinBox(group);
    m_hashThreadsSpin->setRange(1, 16);
    m_hashThreadsSpin->setValue(
        qBound(1, QThread::idealThreadCount(), 8));
    threadRow->addWidget(threadLabel);
    threadRow->addWidget(m_hashThreadsSpin);
    threadRow->addStretch(1);
    grid->addLayout(threadRow);

    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("Min size (KB):"), group));
    m_minSizeSpin = new QSpinBox(group);
    m_minSizeSpin->setRange(0, 1024 * 1024);
    m_minSizeSpin->setValue(1);
    sizeRow->addWidget(m_minSizeSpin);
    sizeRow->addWidget(new QLabel(tr("Max size (MB, 0 = unlimited):"), group));
    m_maxSizeSpin = new QSpinBox(group);
    m_maxSizeSpin->setRange(0, 1024 * 1024);
    sizeRow->addWidget(m_maxSizeSpin);
    sizeRow->addStretch(1);
    grid->addLayout(sizeRow);

    auto* floorRow = new QHBoxLayout;
    floorRow->addWidget(new QLabel(tr("Folder overlap floor (%):"), group));
    m_overlapFloorSpin = new QDoubleSpinBox(group);
    m_overlapFloorSpin->setRange(0.0, 1.0);
    m_overlapFloorSpin->setSingleStep(0.05);
    m_overlapFloorSpin->setDecimals(2);
    m_overlapFloorSpin->setValue(0.34);
    floorRow->addWidget(m_overlapFloorSpin);
    floorRow->addStretch(1);
    grid->addLayout(floorRow);

    rootLayout->addWidget(group);

    auto notify = [this]() { emit configChanged(); };
    connect(m_mainFolderDupCheck, &QCheckBox::toggled, this, notify);
    connect(m_dryRunCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_dryRunHint->setVisible(on);
        emit configChanged();
    });
    connect(m_completeHashCheck, &QCheckBox::toggled, this, notify);
    connect(m_byteVerifyCheck, &QCheckBox::toggled, this, notify);
    connect(m_followSymlinkCheck, &QCheckBox::toggled, this, notify);
    connect(m_analyzeFoldersCheck, &QCheckBox::toggled, this, notify);
    connect(m_hashThreadsSpin, qOverload<int>(&QSpinBox::valueChanged), this, notify);
    connect(m_minSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this, notify);
    connect(m_maxSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this, notify);
    connect(m_overlapFloorSpin, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, notify);
}

bool ScanConfigWidget::dryRun() const {
    return m_dryRunCheck->isChecked();
}

void ScanConfigWidget::loadFromSettings(const core::AppSettings& settings) {
    m_dryRunCheck->setChecked(settings.value(QStringLiteral("Scan/DryRun"), true).toBool());
    m_completeHashCheck->setChecked(
        settings.value(QStringLiteral("Scan/CompleteHashing"), false).toBool());
    m_byteVerifyCheck->setChecked(
        settings.value(QStringLiteral("Scan/ByteVerify"), true).toBool());
    m_analyzeFoldersCheck->setChecked(
        settings.value(QStringLiteral("Scan/AnalyzeFolders"), true).toBool());
    m_mainFolderDupCheck->setChecked(
        settings.value(QStringLiteral("Scan/IncludeMainFolderDuplicates"),
                       false).toBool());
    m_followSymlinkCheck->setChecked(
        settings.value(QStringLiteral("Scan/FollowDirSymlinks"), false).toBool());
    m_hashThreadsSpin->setValue(
        settings.value(QStringLiteral("Scan/HashingThreads"),
                       settings.hashingThreadCount()).toInt());
    m_minSizeSpin->setValue(
        settings.value(QStringLiteral("Scan/MinSizeKB"), 1).toInt());
    m_maxSizeSpin->setValue(
        settings.value(QStringLiteral("Scan/MaxSizeMB"), 0).toInt());
    m_overlapFloorSpin->setValue(
        settings.value(QStringLiteral("Scan/OverlapFloor"), 0.34).toDouble());
    m_dryRunHint->setVisible(m_dryRunCheck->isChecked());
}

void ScanConfigWidget::saveToSettings(core::AppSettings& settings) const {
    settings.setValue(QStringLiteral("Scan/DryRun"), m_dryRunCheck->isChecked());
    settings.setValue(QStringLiteral("Scan/CompleteHashing"),
                      m_completeHashCheck->isChecked());
    settings.setValue(QStringLiteral("Scan/ByteVerify"),
                      m_byteVerifyCheck->isChecked());
    settings.setValue(QStringLiteral("Scan/AnalyzeFolders"),
                      m_analyzeFoldersCheck->isChecked());
    settings.setValue(QStringLiteral("Scan/IncludeMainFolderDuplicates"),
                      m_mainFolderDupCheck->isChecked());
    settings.setValue(QStringLiteral("Scan/FollowDirSymlinks"),
                      m_followSymlinkCheck->isChecked());
    settings.setValue(QStringLiteral("Scan/HashingThreads"),
                      m_hashThreadsSpin->value());
    settings.setValue(QStringLiteral("Scan/MinSizeKB"), m_minSizeSpin->value());
    settings.setValue(QStringLiteral("Scan/MaxSizeMB"), m_maxSizeSpin->value());
    settings.setValue(QStringLiteral("Scan/OverlapFloor"),
                      m_overlapFloorSpin->value());
    settings.setHashingThreadCount(m_hashThreadsSpin->value());
}

void ScanConfigWidget::applyTo(engine::ScanJobConfig& config) const {
    config.dryRun = m_dryRunCheck->isChecked();
    config.completeHashing = m_completeHashCheck->isChecked();
    config.byteCompareBeforeMove = m_byteVerifyCheck->isChecked();
    config.analyzeFolders = m_analyzeFoldersCheck->isChecked();
    config.includeMainFolderDuplicates = m_mainFolderDupCheck->isChecked();
    config.followDirSymlinks = m_followSymlinkCheck->isChecked();
    config.hashingThreads = m_hashThreadsSpin->value();
    config.minFileSize = static_cast<qint64>(m_minSizeSpin->value()) * 1024;
    config.maxFileSize = m_maxSizeSpin->value() > 0
        ? static_cast<qint64>(m_maxSizeSpin->value()) * 1024 * 1024 : -1;
    config.folderOverlapFloor = m_overlapFloorSpin->value();
}

} // namespace dedupvault::ui