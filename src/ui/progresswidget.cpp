#include "progresswidget.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <climits>

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

QString phaseDisplayName(const QString& phase) {
    if (phase == QLatin1String("traversal")) return QObject::tr("Traversing folders");
    if (phase == QLatin1String("hashing"))    return QObject::tr("Hashing candidate files");
    if (phase == QLatin1String("grouping"))   return QObject::tr("Grouping duplicates");
    if (phase == QLatin1String("analysis"))   return QObject::tr("Analyzing folder structure");
    if (phase == QLatin1String("planning"))   return QObject::tr("Planning moves");
    if (phase == QLatin1String("done"))       return QObject::tr("Finished");
    if (phase == QLatin1String("paused"))     return QObject::tr("Paused");
    return QObject::tr("Idle");
}

QLabel* makeStatValue(QWidget* parent) {
    auto* label = new QLabel(QStringLiteral("—"), parent);
    label->setProperty("role", "stat-value");
    label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    return label;
}

QLabel* makeStatCaption(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("role", "subtitle");
    label->setAlignment(Qt::AlignHCenter);
    return label;
}

} // namespace

ProgressWidget::ProgressWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Scan Progress"), this);
    title->setProperty("role", "title");
    layout->addWidget(title);

    auto* headRow = new QHBoxLayout;
    m_phaseLabel = new QLabel(tr("Idle"), this);
    m_phaseLabel->setProperty("role", "subtitle");
    headRow->addWidget(m_phaseLabel);
    headRow->addStretch(1);
    layout->addLayout(headRow);

    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    layout->addWidget(m_messageLabel);

    // --- Bars ---------------------------------------------------------------
    auto* barsBox = new QGroupBox(tr("Stages"), this);
    auto* barsLayout = new QVBoxLayout(barsBox);

    auto* travRow = new QGridLayout;
    travRow->addWidget(new QLabel(tr("Traversal"), barsBox), 0, 0);
    m_traversalValue = new QLabel(QStringLiteral("—"), barsBox);
    travRow->addWidget(m_traversalValue, 0, 1, Qt::AlignRight);
    m_traversalBar = new QProgressBar(barsBox);
    m_traversalBar->setRange(0, 1); m_traversalBar->setValue(0);
    travRow->addWidget(m_traversalBar, 1, 0, 1, 2);
    barsLayout->addLayout(travRow);

    auto* hashRow = new QGridLayout;
    hashRow->addWidget(new QLabel(tr("Hashing"), barsBox), 0, 0);
    m_hashValue = new QLabel(QStringLiteral("—"), barsBox);
    hashRow->addWidget(m_hashValue, 0, 1, Qt::AlignRight);
    m_hashBar = new QProgressBar(barsBox);
    m_hashBar->setRange(0, 1); m_hashBar->setValue(0);
    hashRow->addWidget(m_hashBar, 1, 0, 1, 2);
    barsLayout->addLayout(hashRow);

    layout->addWidget(barsBox);

    // --- Statistics ---------------------------------------------------------
    m_statsGroup = new QGroupBox(tr("Statistics"), this);
    auto* grid = new QGridLayout(m_statsGroup);
    auto addStat = [this, grid](int row, int col, const QString& caption,
                                QLabel** valueOut) {
        auto* value = makeStatValue(this);
        auto* cap = makeStatCaption(caption, this);
        grid->addWidget(value, row * 2, col, 1, 1, Qt::AlignHCenter);
        grid->addWidget(cap, row * 2 + 1, col, 1, 1, Qt::AlignHCenter);
        *valueOut = value;
    };
    addStat(0, 0, tr("Files discovered"), &m_statFiles);
    addStat(0, 1, tr("Bytes scanned"), &m_statBytes);
    addStat(0, 2, tr("Files hashed"), &m_statHashed);
    addStat(1, 0, tr("Duplicate groups"), &m_statGroups);
    addStat(1, 1, tr("Recoverable space"), &m_statSavings);
    addStat(1, 2, tr("Errors"), &m_statErrors);
    addStat(2, 0, tr("Pending reviews"), &m_statReviews);
    layout->addWidget(m_statsGroup);

    // --- Controls -----------------------------------------------------------
    m_controlsGroup = new QGroupBox(tr("Controls"), this);
    auto* controls = new QHBoxLayout(m_controlsGroup);
    m_pauseBtn = new QPushButton(tr("Pause"), m_controlsGroup);
    m_resumeBtn = new QPushButton(tr("Resume"), m_controlsGroup);
    m_resumeBtn->setProperty("role", "primary");
    m_cancelBtn = new QPushButton(tr("Cancel"), m_controlsGroup);
    m_cancelBtn->setProperty("role", "danger");
    m_resumeJobBtn = new QPushButton(tr("Resume Interrupted Job"), m_controlsGroup);
    m_resumeJobBtn->setProperty("role", "primary");
    controls->addWidget(m_pauseBtn);
    controls->addWidget(m_resumeBtn);
    controls->addWidget(m_cancelBtn);
    controls->addWidget(m_resumeJobBtn);
    controls->addStretch(1);
    layout->addWidget(m_controlsGroup);
    layout->addStretch(1);

    connect(m_pauseBtn, &QPushButton::clicked, this, &ProgressWidget::pauseRequested);
    connect(m_resumeBtn, &QPushButton::clicked, this, &ProgressWidget::resumeRequested);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ProgressWidget::cancelRequested);
    connect(m_resumeJobBtn, &QPushButton::clicked, this,
            &ProgressWidget::resumeExistingRequested);

    setRunning(false);
    m_resumeJobBtn->setEnabled(false);
}

void ProgressWidget::setRunning(bool running) {
    m_pauseBtn->setEnabled(running);
    m_cancelBtn->setEnabled(running);
    m_resumeBtn->setEnabled(running);
}

void ProgressWidget::setResumeAvailable(bool available) {
    m_resumeJobBtn->setEnabled(available && !m_pauseBtn->isEnabled());
}

void ProgressWidget::updateProgress(const engine::EngineProgress& p) {
    const QString phase = p.paused ? QStringLiteral("paused") : p.phase;
    m_phaseLabel->setText(phaseDisplayName(phase));
    m_messageLabel->setText(p.lastMessage);

    const qint64 total = p.dbStats.fileCount;
    const qint64 hashed = p.dbStats.hashedCount;

    // Traversal: unknown total up front; show absolute counts.
    m_traversalValue->setText(
        tr("%1 files · %2").arg(p.filesScanned).arg(humanBytes(p.bytesScanned)));
    m_traversalBar->setRange(0, 1);
    m_traversalBar->setValue(p.phase == QLatin1String("done") ? 1 : 0);

    // Hashing: hashed vs known candidates (bounded query in the engine).
    const qint64 queue = p.hashQueueDepth;
    m_hashValue->setText(queue > 0
        ? tr("%1 hashed · %2 queued").arg(hashed).arg(queue)
        : tr("%1 hashed").arg(hashed));
    const qint64 denom = hashed + queue;
    m_hashBar->setRange(0, denom > 0 && denom <= INT_MAX ? static_cast<int>(denom) : 1);
    m_hashBar->setValue(denom > 0 && denom <= INT_MAX ? static_cast<int>(hashed) : 0);

    m_statFiles->setText(QString::number(p.dbStats.fileCount));
    m_statBytes->setText(humanBytes(p.dbStats.totalBytes));
    m_statHashed->setText(humanBytes(p.bytesHashed));
    m_statGroups->setText(QString::number(p.dbStats.duplicateGroupCount));
    m_statSavings->setText(humanBytes(p.dbStats.wastedBytes));
    m_statErrors->setText(QString::number(p.errors + p.dbStats.errorCount));
    m_statReviews->setText(QString::number(p.dbStats.pendingReviewCount));

    m_pauseBtn->setEnabled(p.running && !p.paused);
    m_resumeBtn->setEnabled(p.running && p.paused);
    m_cancelBtn->setEnabled(p.running);
}

} // namespace dedupvault::ui