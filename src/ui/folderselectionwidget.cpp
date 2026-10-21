#include "folderselectionwidget.h"
#include "core/filesystemutils.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStyle>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace dedupvault::ui {

FolderSelectionWidget::FolderSelectionWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    // --- Main folder --------------------------------------------------------
    auto* mainGroup = new QGroupBox(tr("1 · Main folder (protected reference)"), this);
    auto* mainLayout = new QHBoxLayout(mainGroup);
    m_mainEdit = new QLineEdit(mainGroup);
    m_mainEdit->setPlaceholderText(tr("The folder that is never modified"));
    m_mainEdit->setReadOnly(true);
    auto* mainBrowse = new QPushButton(tr("Browse…"), mainGroup);
    mainLayout->addWidget(m_mainEdit, 1);
    mainLayout->addWidget(mainBrowse);
    rootLayout->addWidget(mainGroup);

    // --- Candidates ---------------------------------------------------------
    auto* candGroup = new QGroupBox(tr("2 · Candidate folders (to consolidate)"), this);
    auto* candLayout = new QVBoxLayout(candGroup);
    m_candidatesList = new QListWidget(candGroup);
    m_candidatesList->setMinimumHeight(110);
    m_candidatesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    auto* candBtns = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add folders…"), candGroup);
    m_removeCandidateBtn = new QPushButton(tr("Remove selected"), candGroup);
    auto* hint = new QLabel(tr("Duplicates found here (and not in the main "
                               "folder) are moved to the destination."), candGroup);
    hint->setProperty("role", "subtitle");
    hint->setWordWrap(true);
    candBtns->addWidget(addBtn);
    candBtns->addWidget(m_removeCandidateBtn);
    candBtns->addStretch(1);
    candLayout->addWidget(m_candidatesList, 1);
    candLayout->addLayout(candBtns);
    candLayout->addWidget(hint);
    rootLayout->addWidget(candGroup, 1);

    // --- Destination --------------------------------------------------------
    auto* destGroup = new QGroupBox(tr("3 · Destination (segregation)"), this);
    auto* destLayout = new QHBoxLayout(destGroup);
    m_destEdit = new QLineEdit(destGroup);
    m_destEdit->setPlaceholderText(tr("Where consolidated duplicates are kept "
                                      "for review"));
    m_destEdit->setReadOnly(true);
    auto* destBrowse = new QPushButton(tr("Browse…"), destGroup);
    destLayout->addWidget(m_destEdit, 1);
    destLayout->addWidget(destBrowse);
    rootLayout->addWidget(destGroup);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    rootLayout->addWidget(m_warningLabel);

    connect(mainBrowse, &QPushButton::clicked, this, &FolderSelectionWidget::pickMainFolder);
    connect(addBtn, &QPushButton::clicked, this, &FolderSelectionWidget::addCandidateFolders);
    connect(m_removeCandidateBtn, &QPushButton::clicked,
            this, &FolderSelectionWidget::removeSelectedCandidate);
    connect(destBrowse, &QPushButton::clicked,
            this, &FolderSelectionWidget::pickDestinationFolder);
    connect(m_candidatesList, &QListWidget::itemSelectionChanged, this,
            [this]() {
                m_removeCandidateBtn->setEnabled(
                    !m_candidatesList->selectedItems().isEmpty());
                onChanged();
            });

    refreshWarnings();
}

QString FolderSelectionWidget::mainFolder() const {
    return m_mainEdit->text().trimmed();
}

QStringList FolderSelectionWidget::candidateFolders() const {
    QStringList out;
    for (int i = 0; i < m_candidatesList->count(); ++i) {
        out.append(m_candidatesList->item(i)->text());
    }
    return out;
}

QString FolderSelectionWidget::destinationFolder() const {
    return m_destEdit->text().trimmed();
}

void FolderSelectionWidget::setMainFolder(const QString& path) {
    m_mainEdit->setText(path);
    onChanged();
}

void FolderSelectionWidget::setCandidateFolders(const QStringList& paths) {
    m_candidatesList->clear();
    for (const QString& p : paths) m_candidatesList->addItem(p);
    onChanged();
}

void FolderSelectionWidget::setDestinationFolder(const QString& path) {
    m_destEdit->setText(path);
    onChanged();
}

void FolderSelectionWidget::loadFromSettings(const core::AppSettings& settings) {
    if (!settings.lastMainFolder().isEmpty()) {
        m_mainEdit->setText(settings.lastMainFolder());
    }
    m_candidatesList->clear();
    for (const QString& p : settings.lastCandidateFolders()) {
        m_candidatesList->addItem(p);
    }
    if (!settings.lastDestinationFolder().isEmpty()) {
        m_destEdit->setText(settings.lastDestinationFolder());
    }
    refreshWarnings();
}

void FolderSelectionWidget::saveToSettings(core::AppSettings& settings) const {
    settings.setLastMainFolder(mainFolder());
    settings.setLastCandidateFolders(candidateFolders());
    settings.setLastDestinationFolder(destinationFolder());
}

QString FolderSelectionWidget::validationError() const {
    using core::FileSystemUtils;

    if (mainFolder().isEmpty()) {
        return tr("Choose the main (reference) folder first.");
    }
    if (!QFileInfo::exists(mainFolder())) {
        return tr("Main folder does not exist: %1").arg(mainFolder());
    }

    QStringList all{mainFolder()};
    all.append(candidateFolders());
    const auto overlaps = FileSystemUtils::findOverlappingRoots(all);
    if (!overlaps.isEmpty()) {
        const auto& o = overlaps.first();
        return tr("These selections overlap: %1 and %2. Remove duplicates and "
                  "nested folders — every folder may be scanned only once.")
            .arg(all[o.first], all[o.second]);
    }

    if (!candidateFolders().isEmpty() && destinationFolder().isEmpty()) {
        return tr("Choose a destination folder for the consolidated duplicates.");
    }

    if (!destinationFolder().isEmpty()) {
        if (FileSystemUtils::isContainedIn(destinationFolder(), mainFolder())) {
            return tr("The destination must not live inside the protected "
                      "main folder.");
        }
        if (QFileInfo::exists(destinationFolder()) &&
            !core::AppSettings::isFolderWritable(destinationFolder())) {
            return tr("The destination folder is not writable: %1")
                .arg(destinationFolder());
        }
    }
    return QString();
}

void FolderSelectionWidget::refreshWarnings() {
    const QString err = validationError();
    if (err.isEmpty()) {
        m_warningLabel->setText(tr("✓ Selection looks good."));
        m_warningLabel->setProperty("role", "ok");
    } else {
        m_warningLabel->setText(err);
        m_warningLabel->setProperty("role",
            mainFolder().isEmpty() ? "subtitle" : "warn");
    }
    m_warningLabel->style()->unpolish(m_warningLabel);
    m_warningLabel->style()->polish(m_warningLabel);
    m_removeCandidateBtn->setEnabled(!m_candidatesList->selectedItems().isEmpty());
}

void FolderSelectionWidget::onChanged() {
    refreshWarnings();
    emit foldersChanged();
}

void FolderSelectionWidget::pickMainFolder() {
    const QString start = mainFolder().isEmpty()
        ? QDir::homePath() : mainFolder();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select the main (protected) folder"), start);
    if (dir.isEmpty()) return;
    setMainFolder(core::FileSystemUtils::normalizePath(QFileInfo(dir).absoluteFilePath()));
}

void FolderSelectionWidget::addCandidateFolders() {
    const QString start = candidateFolders().isEmpty()
        ? (mainFolder().isEmpty() ? QDir::homePath() : mainFolder())
        : candidateFolders().last();

    // Qt's directory dialog is single-select; add repeatedly — simple and
    // predictable, and existing entries are kept in order.
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Add a candidate folder"), start);
    if (dir.isEmpty()) return;

    const QString normalized =
        core::FileSystemUtils::normalizePath(QFileInfo(dir).absoluteFilePath());
    if (normalized == mainFolder()) {
        QMessageBox::warning(this, tr("Already selected"),
            tr("That folder is the main folder — it is protected and cannot "
               "also be a candidate."));
        return;
    }
    if (candidateFolders().contains(normalized)) {
        return; // silently ignore exact duplicates
    }
    for (const QString& existing : candidateFolders()) {
        if (core::FileSystemUtils::isContainedIn(normalized, existing) ||
            core::FileSystemUtils::isContainedIn(existing, normalized)) {
            QMessageBox::warning(this, tr("Overlapping folders"),
                tr("%1 and %2 overlap. Each folder may appear only once in "
                   "the selection.").arg(existing, normalized));
            return;
        }
    }
    m_candidatesList->addItem(normalized);
    onChanged();
}

void FolderSelectionWidget::removeSelectedCandidate() {
    const auto items = m_candidatesList->selectedItems();
    for (QListWidgetItem* item : items) delete item;
    onChanged();
}

void FolderSelectionWidget::pickDestinationFolder() {
    const QString start = destinationFolder().isEmpty()
        ? QDir::homePath() : destinationFolder();
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select (or create) the destination folder"), start);
    if (dir.isEmpty()) return;
    setDestinationFolder(core::FileSystemUtils::normalizePath(
        QFileInfo(dir).absoluteFilePath()));
    refreshWarnings();
}

} // namespace dedupvault::ui