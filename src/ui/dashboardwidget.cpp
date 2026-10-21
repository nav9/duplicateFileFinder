#include "dashboardwidget.h"

#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace dedupvault::ui {

namespace {

QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Raised);
    return card;
}

} // namespace

DashboardWidget::DashboardWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("DedupVault"), this);
    title->setProperty("role", "title");
    layout->addWidget(title);

    auto* subtitle = new QLabel(
        tr("Find and consolidate duplicate files across your backups — "
           "nothing in your main folder is ever touched."), this);
    subtitle->setProperty("role", "subtitle");
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    // --- Primary action -----------------------------------------------------
    auto* actions = new QHBoxLayout;
    m_startBtn = new QPushButton(tr("Start a New Scan"), this);
    m_startBtn->setProperty("role", "primary");
    m_startBtn->setMinimumHeight(46);
    m_resumeBtn = new QPushButton(tr("Resume Interrupted Scan"), this);
    m_resumeBtn->setProperty("role", "primary");
    m_resumeBtn->setMinimumHeight(46);
    m_resumeBtn->setEnabled(false);
    auto* resultsBtn = new QPushButton(tr("Duplicate Results"), this);
    auto* logsBtn = new QPushButton(tr("Logs"), this);
    auto* settingsBtn = new QPushButton(tr("Settings"), this);
    actions->addWidget(m_startBtn, 2);
    actions->addWidget(m_resumeBtn, 2);
    actions->addWidget(resultsBtn);
    actions->addWidget(logsBtn);
    actions->addWidget(settingsBtn);
    layout->addLayout(actions);

    connect(m_startBtn, &QPushButton::clicked, this,
            &DashboardWidget::startScanRequested);
    connect(m_resumeBtn, &QPushButton::clicked, this,
            &DashboardWidget::resumeJobRequested);
    connect(resultsBtn, &QPushButton::clicked, this,
            &DashboardWidget::viewResultsRequested);
    connect(logsBtn, &QPushButton::clicked, this,
            &DashboardWidget::openLogsRequested);
    connect(settingsBtn, &QPushButton::clicked, this,
            &DashboardWidget::openSettingsRequested);

    // --- Status cards -------------------------------------------------------
    auto* cards = new QGridLayout;
    cards->setSpacing(12);

    auto* folderCard = makeCard(this);
    auto* folderLayout = new QVBoxLayout(folderCard);
    auto* folderCaption = new QLabel(tr("Working folder"), folderCard);
    folderCaption->setProperty("role", "subtitle");
    m_folderValue = new QLabel(QStringLiteral("—"), folderCard);
    m_folderValue->setWordWrap(true);
    m_folderStatus = new QLabel(folderCard);
    folderLayout->addWidget(folderCaption);
    folderLayout->addWidget(m_folderValue);
    folderLayout->addWidget(m_folderStatus);
    cards->addWidget(folderCard, 0, 0);

    auto* resumeCard = makeCard(this);
    auto* resumeLayout = new QVBoxLayout(resumeCard);
    auto* resumeCaption = new QLabel(tr("Resumable scan"), resumeCard);
    resumeCaption->setProperty("role", "subtitle");
    m_resumeValue = new QLabel(tr("None"), resumeCard);
    m_resumeValue->setWordWrap(true);
    resumeLayout->addWidget(resumeCaption);
    resumeLayout->addWidget(m_resumeValue);
    cards->addWidget(resumeCard, 0, 1);

    auto* summaryCard = makeCard(this);
    auto* summaryLayout = new QVBoxLayout(summaryCard);
    auto* summaryCaption = new QLabel(tr("Last results"), summaryCard);
    summaryCaption->setProperty("role", "subtitle");
    m_summaryValue = new QLabel(tr("No scan yet"), summaryCard);
    m_summaryValue->setWordWrap(true);
    summaryLayout->addWidget(summaryCaption);
    summaryLayout->addWidget(m_summaryValue);
    cards->addWidget(summaryCard, 0, 2);

    layout->addLayout(cards);

    // --- Fast journey -------------------------------------------------------
    auto* journey = new QGroupBox(tr("How it works"), this);
    auto* journeyLayout = new QVBoxLayout(journey);
    journeyLayout->addWidget(new QLabel(
        tr("1 · Pick the main folder (never modified), the candidate folders "
           "to consolidate and a segregation destination — your choice is "
           "remembered."), journey));
    journeyLayout->addWidget(new QLabel(
        tr("2 · Press Start Scan. Duplicate detection, hash generation and "
           "folder analysis run in the background; you can pause, resume or "
           "close the app and come back."), journey));
    journeyLayout->addWidget(new QLabel(
        tr("3 · Review the proposed moves, apply the ones you like into the "
           "segregation folder, and undo anything later from "
           "Review → History && Undo."), journey));
    layout->addWidget(journey);

    // --- Reserved for future menu options -----------------------------------
    auto* future = new QGroupBox(tr("More tools (coming soon)"), this);
    auto* futureGrid = new QGridLayout(future);
    const QStringList futureTools = {
        tr("Duplicate images — perceptual (pHash) matching"),
        tr("Duplicate videos — container-aware comparison"),
        tr("Unique sentences across text files"),
        tr("Junk cleaner — Thumbs.db, .DS_Store, editor droppings")
    };
    for (int i = 0; i < futureTools.size(); ++i) {
        auto* btn = new QPushButton(futureTools[i], future);
        btn->setEnabled(false);
        futureGrid->addWidget(btn, i / 2, i % 2);
    }
    layout->addWidget(future);
    layout->addStretch(1);
}

void DashboardWidget::setWorkingFolder(const QString& path, bool writable) {
    m_folderValue->setText(path.isEmpty() ? tr("Not configured") : path);
    m_folderStatus->setText(writable ? tr("✓ Writable — ready")
                                     : tr("⚠ Needs attention (read-only or missing)"));
    m_folderStatus->setProperty("role", writable ? "ok" : "warn");
    m_folderStatus->style()->unpolish(m_folderStatus);
    m_folderStatus->style()->polish(m_folderStatus);
}

void DashboardWidget::setResumableJob(bool available, qint64 jobId) {
    m_resumeBtn->setEnabled(available);
    m_resumeValue->setText(available
        ? tr("Job #%1 can be resumed where it stopped.").arg(jobId)
        : tr("None"));
}

void DashboardWidget::setLastSummary(const QString& summary) {
    m_summaryValue->setText(summary);
}

} // namespace dedupvault::ui