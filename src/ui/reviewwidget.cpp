#include "reviewwidget.h"
#include "core/logger.h"
#include "core/filesystemutils.h"

#include <QColor>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace dedupvault::ui {

namespace {

QString matchTypeLabel(const QString& type) {
    if (type == QLatin1String("exact_subtree")) return QObject::tr("Exact duplicate subtree");
    if (type == QLatin1String("partial_overlap")) return QObject::tr("Partial overlap");
    return type;
}

constexpr int RoleMatchId = Qt::UserRole;
constexpr int RoleMoveSide = Qt::UserRole + 1; // folder id to move (candidate side)

} // namespace

ReviewWidget::ReviewWidget(core::DatabaseManager& db,
                           operations::SafeFileOps& ops,
                           operations::SegregationManager& segregation,
                           operations::UndoManager& undo,
                           QWidget* parent)
    : QWidget(parent)
    , m_db(db)
    , m_ops(ops)
    , m_segregation(segregation)
    , m_undo(undo)
{
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(tr("Review && History"), this);
    title->setProperty("role", "title");
    layout->addWidget(title);

    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);
    buildFolderTab();
    buildHistoryTab();

    connect(&m_undo, &operations::UndoManager::historyChanged, this,
            [this]() { refreshHistory(); });
    connect(&m_segregation, &operations::SegregationManager::actionsPlanned, this,
            [this](int) { refreshHistory(); });
}

void ReviewWidget::setContext(const Context& ctx) {
    m_ctx = ctx;
    refreshFolderMatches();
    refreshHistory();
}

// ---------------------------------------------------------------------------
// Folder review tab

void ReviewWidget::buildFolderTab() {
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);

    m_matchSummary = new QLabel(tr("No folder analysis yet."), page);
    m_matchSummary->setProperty("role", "subtitle");
    m_matchSummary->setWordWrap(true);
    layout->addWidget(m_matchSummary);

    m_matchTree = new QTreeWidget(page);
    m_matchTree->setColumnCount(5);
    m_matchTree->setHeaderLabels({tr("Match"), tr("Overlap"), tr("Folder A"),
                                  tr("Folder B"), tr("Review status")});
    m_matchTree->setAlternatingRowColors(true);
    m_matchTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_matchTree->setRootIsDecorated(true);
    m_matchTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_matchTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_matchTree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_matchTree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_matchTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_matchTree->setColumnWidth(2, 260);
    m_matchTree->setColumnWidth(3, 260);
    layout->addWidget(m_matchTree, 1);

    auto* buttons = new QHBoxLayout;
    m_approveBtn = new QPushButton(tr("Approve Subtree Move (selected)"), page);
    m_approveBtn->setProperty("role", "primary");
    m_skipBtn = new QPushButton(tr("Skip / Keep As-Is"), page);
    buttons->addWidget(m_approveBtn);
    buttons->addWidget(m_skipBtn);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(m_approveBtn, &QPushButton::clicked,
            this, &ReviewWidget::approveSelectedSubtrees);
    connect(m_skipBtn, &QPushButton::clicked,
            this, &ReviewWidget::skipSelectedMatches);
    m_approveBtn->setEnabled(false);
    m_skipBtn->setEnabled(false);
    connect(m_matchTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const bool any = !m_matchTree->selectedItems().isEmpty();
        m_approveBtn->setEnabled(any);
        m_skipBtn->setEnabled(any);
    });

    m_tabs->addTab(page, tr("Folder Review"));
}

QString ReviewWidget::subtreeDestination(const core::FolderMatchRow& match,
                                         bool* ok) const {
    if (m_ctx.destinationRoot.isEmpty()) {
        if (ok) *ok = false;
        return QString();
    }

    // Move the candidate side, never the protected/main side.
    const auto folderA = m_db.folderById(match.folderAId);
    const auto folderB = m_db.folderById(match.folderBId);
    if (!folderA || !folderB) {
        if (ok) *ok = false;
        return QString();
    }

    const auto rootRoleA = m_db.rootById(folderA->rootId);
    const auto rootRoleB = m_db.rootById(folderB->rootId);
    const bool aMain = rootRoleA && rootRoleA->role == core::RootRole::Main;
    const bool bMain = rootRoleB && rootRoleB->role == core::RootRole::Main;

    const core::FolderRow* mover = nullptr;
    if (!aMain && bMain) {
        mover = &*folderA;
    } else if (aMain && !bMain) {
        mover = &*folderB;
    } else {
        // Both candidates: deterministic choice — lower folder id (the one
        // registered first) stays, the other moves.
        mover = (folderA->folderId <= folderB->folderId) ? &*folderB : &*folderA;
    }

    const QString dest = m_segregation.plannedDestination(
        m_ctx.destinationRoot, mover->rootPath, mover->relPath);
    if (ok) *ok = true;
    return dest;
}

void ReviewWidget::refreshFolderMatches() {
    m_matchTree->clear();
    const auto matches = m_db.folderMatches();

    int pending = 0;
    for (const core::FolderMatchRow& match : matches) {
        if (match.reviewStatus == QLatin1String("pending")) ++pending;

        auto* item = new QTreeWidgetItem(QStringList{
            matchTypeLabel(match.matchType),
            QStringLiteral("%1%").arg(match.overlapScore * 100.0, 0, 'f', 0),
            match.pathA,
            match.pathB,
            match.reviewStatus});
        item->setData(0, RoleMatchId, match.matchId);

        const bool exact = match.matchType == QLatin1String("exact_subtree");
        item->setToolTip(0, match.notes);
        item->setToolTip(2, match.pathA);
        item->setToolTip(3, match.pathB);
        if (match.reviewStatus != QLatin1String("pending")) {
            item->setForeground(4, QColor(0x8a, 0x8b, 0x93));
        } else if (!exact) {
            item->setForeground(0, QColor(0xf5, 0xb9, 0x42));
        } else {
            item->setForeground(0, QColor(0x4a, 0x9e, 0xff));
        }

        auto* notes = new QTreeWidgetItem(item, QStringList{
            match.notes.isEmpty() ? tr("(no notes)") : match.notes});
        notes->setFirstColumnSpanned(true);
        notes->setFlags(Qt::ItemIsEnabled);
        m_matchTree->addTopLevelItem(item);
    }
    m_matchTree->setEnabled(!matches.isEmpty());

    m_matchSummary->setText(matches.isEmpty()
        ? tr("No folder matches. Run a scan with folder analysis enabled to "
             "compare folder structures.")
        : tr("%1 match(es) recorded, %2 awaiting your review. Structured "
             "content (repos, websites, projects) always needs explicit "
             "approval.")
              .arg(matches.size()).arg(pending));
}

void ReviewWidget::approveSelectedSubtrees() {
    const auto selected = m_matchTree->selectedItems();
    if (selected.isEmpty()) return;

    if (m_ctx.destinationRoot.isEmpty()) {
        QMessageBox::warning(this, tr("No destination"),
                             tr("Choose a destination folder first."));
        return;
    }

    QStringList report;
    int approved = 0;
    int failed = 0;

    for (QTreeWidgetItem* entry : selected) {
        if (entry->parent() != nullptr) continue; // notes rows are not selectable
        const qint64 matchId = entry->data(0, RoleMatchId).toLongLong();

        core::FolderMatchRow match;
        bool found = false;
        for (const auto& m : m_db.folderMatches()) {
            if (m.matchId == matchId) { match = m; found = true; break; }
        }
        if (!found) continue;

        if (match.matchType == QLatin1String("partial_overlap")) {
            report.append(tr("Partial overlap (%1) — review file-level "
                             "duplicates in the Duplicate Files instead of "
                             "moving a whole subtree: %2")
                              .arg(match.overlapScore)
                              .arg(match.pathB));
            continue;
        }

        bool ok = false;
        const QString dest = subtreeDestination(match, &ok);
        if (!ok) {
            ++failed;
            report.append(tr("Could not resolve a mover/destination for %1")
                              .arg(match.pathA));
            continue;
        }

        const auto folderA = m_db.folderById(match.folderAId);
        const auto folderB = m_db.folderById(match.folderBId);
        if (!folderA || !folderB) continue;
        const auto rootRoleA = m_db.rootById(folderA->rootId);
        const bool aMain = rootRoleA && rootRoleA->role == core::RootRole::Main;
        const core::FolderRow& mover = aMain ? *folderB : *folderA;
        const QString source = mover.absPath;

        if (core::FileSystemUtils::isProtected(source, m_ctx.protectedRoot)) {
            ++failed;
            report.append(tr("Refused: %1 is inside the protected main folder")
                              .arg(source));
            continue;
        }

        if (m_ctx.dryRun) {
            // Validate only — journal it so the plan can be executed later.
            const qint64 actionId = m_segregation.planSubtreeAction(
                static_cast<qint64>(m_ctx.jobId), source, dest);
            if (actionId > 0) {
                core::ActionRow row;
                row.actionId = actionId;
                row.sourcePath = source;
                row.destPath = dest;
                const auto validation = operations::SafeFileOps::validate(
                    row, m_ctx.protectedRoot);
                if (validation.valid) {
                    ++approved;
                    report.append(tr("[dry-run] subtree move validated: %1 -> %2")
                                      .arg(source, dest));
                } else {
                    ++failed;
                    report.append(validation.error);
                }
            } else {
                ++failed;
            }
            m_db.setFolderMatchReview(matchId, QStringLiteral("approved"));
            continue;
        }

        const qint64 actionId = m_segregation.planSubtreeAction(
            static_cast<qint64>(m_ctx.jobId), source, dest);
        if (actionId <= 0) {
            ++failed;
            continue;
        }
        const auto action = m_db.actionById(actionId);
        if (!action) { ++failed; continue; }

        operations::SafeFileOps::Options options;
        options.protectedRoot = m_ctx.protectedRoot;
        options.verifyHash = m_ctx.verifyHash;
        options.dryRun = false;

        const auto outcome = m_ops.execute(*action, options);
        if (outcome.ok) {
            ++approved;
            report.append(tr("Moved subtree %1 -> %2").arg(source, dest));
            m_db.setFolderMatchReview(matchId, QStringLiteral("approved"));
        } else {
            ++failed;
            report.append(tr("%1: %2").arg(source, outcome.error));
        }
    }

    refreshFolderMatches();
    refreshHistory();

    QString summary = tr("%1 approved, %2 failed/skipped.").arg(approved).arg(failed);
    if (!report.isEmpty()) {
        summary += QLatin1Char('\n') + report.mid(0, 8).join(QLatin1Char('\n'));
        if (report.size() > 8) {
            summary += tr("\n… and %1 more (see Logs)").arg(report.size() - 8);
        }
    }
    QMessageBox::information(this, tr("Folder review"), summary);
}

void ReviewWidget::skipSelectedMatches() {
    const auto selected = m_matchTree->selectedItems();
    int skipped = 0;
    for (QTreeWidgetItem* entry : selected) {
        if (entry->parent() != nullptr) continue;
        const qint64 matchId = entry->data(0, RoleMatchId).toLongLong();
        if (m_db.setFolderMatchReview(matchId, QStringLiteral("skipped"))) {
            ++skipped;
        }
    }
    refreshFolderMatches();
    core::Logger::instance().info(tr("%1 folder match(es) skipped by the user.")
                                .arg(skipped));
}

// ---------------------------------------------------------------------------
// History & undo tab

void ReviewWidget::buildHistoryTab() {
    auto* page = new QWidget(m_tabs);
    auto* layout = new QVBoxLayout(page);

    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Show:"), page));
    m_historyFilter = new QComboBox(page);
    m_historyFilter->addItem(tr("All actions"), QString());
    m_historyFilter->addItem(tr("Completed (done)"), QStringLiteral("done"));
    m_historyFilter->addItem(tr("Planned (not yet applied)"), QStringLiteral("planned"));
    m_historyFilter->addItem(tr("Undone"), QStringLiteral("undone"));
    m_historyFilter->addItem(tr("Failed"), QStringLiteral("failed"));
    filterRow->addWidget(m_historyFilter);
    m_historySummary = new QLabel(page);
    m_historySummary->setProperty("role", "subtitle");
    filterRow->addWidget(m_historySummary, 1);
    layout->addLayout(filterRow);

    m_historyTree = new QTreeWidget(page);
    m_historyTree->setColumnCount(6);
    m_historyTree->setHeaderLabels({tr("ID"), tr("Operation"), tr("Source"),
                                    tr("Destination"), tr("Status"),
                                    tr("Completed")});
    m_historyTree->setAlternatingRowColors(true);
    m_historyTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_historyTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_historyTree->header()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_historyTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_historyTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_historyTree->setColumnWidth(2, 300);
    m_historyTree->setColumnWidth(3, 300);
    layout->addWidget(m_historyTree, 1);

    auto* buttons = new QHBoxLayout;
    m_undoBtn = new QPushButton(tr("Undo Selected"), page);
    m_undoBtn->setProperty("role", "primary");
    m_undoAllBtn = new QPushButton(tr("Undo All Completed"), page);
    buttons->addWidget(m_undoBtn);
    buttons->addWidget(m_undoAllBtn);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    connect(m_historyFilter, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ReviewWidget::onHistoryFilterChanged);
    connect(m_undoBtn, &QPushButton::clicked, this, &ReviewWidget::undoSelected);
    connect(m_undoAllBtn, &QPushButton::clicked, this, &ReviewWidget::undoAllCompleted);
    connect(m_historyTree, &QTreeWidget::itemSelectionChanged, this, [this]() {
        m_undoBtn->setEnabled(!m_historyTree->selectedItems().isEmpty());
    });
    m_undoBtn->setEnabled(false);
    m_undoAllBtn->setEnabled(false);

    m_tabs->addTab(page, tr("History && Undo"));
}

void ReviewWidget::onHistoryFilterChanged(int) {
    refreshHistory();
}

void ReviewWidget::refreshHistory() {
    const QString filter =
        m_historyFilter ? m_historyFilter->currentData().toString() : QString();

    m_historyTree->clear();
    const auto actions = m_db.actions(filter);

    int undoable = 0;
    for (const core::ActionRow& action : actions) {
        auto* item = new QTreeWidgetItem(QStringList{
            QString::number(action.actionId),
            action.opType == QLatin1String("move_subtree")
                ? tr("Move folder") : tr("Move file"),
            action.sourcePath,
            action.destPath,
            action.status,
            action.completedAt});
        item->setToolTip(2, action.sourcePath);
        item->setToolTip(3, action.destPath);
        if (!action.recoveryInfo.isEmpty()) {
            item->setToolTip(4, action.recoveryInfo);
        }
        if (action.status == QLatin1String("done")) {
            item->setForeground(4, QColor(0x4a, 0xde, 0x80));
            ++undoable;
        } else if (action.status == QLatin1String("failed")) {
            item->setForeground(4, QColor(0xff, 0x6b, 0x6b));
        } else if (action.status == QLatin1String("undone")) {
            item->setForeground(4, QColor(0xf5, 0xb9, 0x42));
        }
        m_historyTree->addTopLevelItem(item);
    }

    m_historyTree->setEnabled(!actions.isEmpty());
    if (m_historySummary) {
        m_historySummary->setText(tr("%1 action(s), %2 undoable")
                                      .arg(actions.size()).arg(undoable));
    }
    if (m_undoAllBtn) m_undoAllBtn->setEnabled(undoable > 0);
    if (m_undoBtn) {
        m_undoBtn->setEnabled(!m_historyTree->selectedItems().isEmpty());
    }
}

void ReviewWidget::undoSelected() {
    QVector<qint64> ids;
    const auto selected = m_historyTree->selectedItems();
    for (QTreeWidgetItem* item : selected) {
        const qint64 id = item->text(0).toLongLong();
        const auto record = m_db.actionById(id);
        if (record && record->status == QLatin1String("done")) ids.append(id);
    }
    if (ids.isEmpty()) {
        QMessageBox::information(this, tr("Nothing to undo"),
            tr("Select completed actions first."));
        return;
    }

    const int answer = QMessageBox::question(
        this, tr("Undo moves"),
        tr("Restore %1 moved item(s) back to their original locations?").arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    const auto report = m_undo.undoMany(ids);
    QStringList lines = {tr("%1 restored, %2 failed.")
                             .arg(report.succeeded).arg(report.failed)};
    lines.append(report.errors.mid(0, 6));
    QMessageBox::information(this, tr("Undo complete"), lines.join(QLatin1Char('\n')));
    refreshHistory();
}

void ReviewWidget::undoAllCompleted() {
    QVector<qint64> ids;
    for (const core::ActionRow& action : m_db.actions(QStringLiteral("done"))) {
        ids.append(action.actionId);
    }
    if (ids.isEmpty()) return;

    const int answer = QMessageBox::question(
        this, tr("Undo all"),
        tr("Restore all %1 completed moves to their original locations? "
           "Items whose original location is now occupied will be reported.")
            .arg(ids.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    const auto report = m_undo.undoMany(ids);
    QStringList lines = {tr("%1 restored, %2 failed.")
                             .arg(report.succeeded).arg(report.failed)};
    lines.append(report.errors.mid(0, 6));
    QMessageBox::information(this, tr("Undo complete"), lines.join(QLatin1Char('\n')));
    refreshHistory();
}

} // namespace dedupvault::ui