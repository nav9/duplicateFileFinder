#include "resultswidget.h"
#include "core/logger.h"

#include <QColor>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

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

constexpr int RoleKind = Qt::UserRole;         // group | keeper | protected | redundant
constexpr int RoleProposalIndex = Qt::UserRole + 1;

} // namespace

ResultsWidget::ResultsWidget(core::DatabaseManager& db,
                             operations::SafeFileOps& ops,
                             operations::SegregationManager& segregation,
                             QWidget* parent)
    : QWidget(parent)
    , m_db(db)
    , m_ops(ops)
    , m_segregation(segregation)
{
    auto* layout = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Duplicate Files"), this);
    title->setProperty("role", "title");
    layout->addWidget(title);

    auto* toolbar = new QHBoxLayout;
    m_summaryLabel = new QLabel(tr("No scan results yet — run a scan first."), this);
    m_summaryLabel->setProperty("role", "subtitle");
    toolbar->addWidget(m_summaryLabel, 1);
    layout->addLayout(toolbar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({tr("File"), tr("Size"), tr("Located in"),
                            tr("Root role"), tr("Action")});
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_tree->setColumnWidth(2, 320);
    layout->addWidget(m_tree, 1);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setProperty("role", "subtitle");
    m_hintLabel->setWordWrap(true);
    layout->addWidget(m_hintLabel);

    auto* buttons = new QHBoxLayout;
    m_checkAllBtn = new QPushButton(tr("Select All Proposed"), this);
    m_checkNoneBtn = new QPushButton(tr("Select None"), this);
    m_dryRunBtn = new QPushButton(tr("Dry Run"), this);
    m_dryRunBtn->setToolTip(
        tr("Validate the selected moves and report what would happen. "
           "Nothing is changed on disk."));
    m_applyBtn = new QPushButton(tr("Segregate Selected"), this);
    m_applyBtn->setProperty("role", "primary");
    m_applyBtn->setToolTip(
        tr("Move the selected copies into the destination's segregation "
           "folder. The main folder is never touched."));
    buttons->addWidget(m_checkAllBtn);
    buttons->addWidget(m_checkNoneBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_dryRunBtn);
    buttons->addWidget(m_applyBtn);
    layout->addLayout(buttons);

    connect(m_checkAllBtn, &QPushButton::clicked, this, [this]() {
        setAllChecked(true);
    });
    connect(m_checkNoneBtn, &QPushButton::clicked, this, [this]() {
        setAllChecked(false);
    });
    connect(m_dryRunBtn, &QPushButton::clicked, this, &ResultsWidget::runDryRun);
    connect(m_applyBtn, &QPushButton::clicked, this, &ResultsWidget::applySelected);
    connect(m_tree, &QTreeWidget::itemExpanded, this,
            &ResultsWidget::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem*, int) { refreshCounters(); });

    m_dryRunBtn->setEnabled(false);
    m_applyBtn->setEnabled(false);
}

void ResultsWidget::setContext(const Context& ctx) {
    m_ctx = ctx;
    m_hintLabel->setText(
        tr("Dry Run validates the selected moves and changes nothing on "
           "disk. Segregate Selected moves the copies into the segregation "
           "folder — nothing under the main folder is ever touched, and "
           "every move can be undone from Review → History & Undo."));
}

void ResultsWidget::reload(const QVector<engine::DuplicateProposal>& proposals) {
    m_proposals = proposals;

    m_actionState.clear();
    for (const core::ActionRow& action : m_db.actions()) {
        m_actionState.insert(action.sourcePath,
                             qMakePair(action.status, action.actionId));
    }

    m_tree->clear();

    m_totalGroups = 0;
    m_reviewOnlyGroups = 0;
    m_potentialSavings = 0;
    for (const auto& p : proposals) {
        m_potentialSavings += p.recoverableBytes;
        if (p.redundant.isEmpty()) ++m_reviewOnlyGroups;
    }

    // Top-level group headers only: children materialize on expansion.
    int created = 0;
    for (int i = 0; i < proposals.size() && created < 4000; ++i, ++created) {
        m_tree->addTopLevelItem(buildGroupItem(proposals[i], i));
    }
    m_totalGroups = created;

    m_tree->setEnabled(!proposals.isEmpty());
    refreshCounters();
}

QTreeWidgetItem* ResultsWidget::buildGroupItem(const engine::DuplicateProposal& proposal,
                                               int index) {
    // Groups with no movable copy consist only of main-folder files: they are
    // reported for information and can never be acted on.
    const bool reviewOnly = proposal.redundant.isEmpty();
    auto* group = new QTreeWidgetItem(QStringList{
        reviewOnly
            ? tr("Group %1 — duplicates inside the main folder (%2)")
                  .arg(index + 1).arg(proposal.hash.left(12))
            : tr("Group %1 — identical content (%2)")
                  .arg(index + 1).arg(proposal.hash.left(12)),
        humanBytes(proposal.size),
        tr("%1 copies")
            .arg(proposal.redundant.size() + proposal.protectedOnes.size() + 1),
        QString(),
        reviewOnly ? tr("review only — main folder is never modified")
                   : tr("save %1").arg(humanBytes(proposal.recoverableBytes))});
    if (reviewOnly) {
        group->setToolTip(0, tr("Every copy of this content lives in the main "
                                "folder, which is protected. Nothing is moved."));
    }
    group->setData(0, RoleKind, QStringLiteral("group"));
    group->setData(0, RoleProposalIndex, index);
    group->setFirstColumnSpanned(true);
    group->setFlags(Qt::ItemIsEnabled);

    // Children are created on first expansion (lazy) for large result sets.
    group->setData(0, Qt::UserRole + 2, false); // populated flag
    return group;
}

void ResultsWidget::onItemExpanded(QTreeWidgetItem* groupItem) {
    if (!groupItem) return;
    if (groupItem->data(0, Qt::UserRole + 2).toBool()) return;

    const int index = groupItem->data(0, RoleProposalIndex).toInt();
    if (index < 0 || index >= m_proposals.size()) return;
    populateGroup(groupItem, m_proposals[index]);
    groupItem->setData(0, Qt::UserRole + 2, true);
}

void ResultsWidget::populateGroup(QTreeWidgetItem* groupItem,
                                  const engine::DuplicateProposal& proposal) {
    const auto addRow = [this, groupItem](const QString& relPath, qint64 size,
                                          const QString& rootPath,
                                          const QString& rootRole,
                                          const QString& absPath,
                                          const QString& kind,
                                          const QString& actionText,
                                          bool checkable, bool checked) {
        auto* item = new QTreeWidgetItem(groupItem, QStringList{
            relPath, humanBytes(size), rootPath, rootRole, actionText});
        item->setData(0, RoleKind, kind);
        item->setToolTip(0, absPath);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable |
                       Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        if (checkable) {
            item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        } else {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        }
        const auto state = m_actionState.constFind(absPath);
        if (state != m_actionState.cend()) {
            if (state->first == QLatin1String("done")) {
                item->setText(4, tr("Moved to segregation"));
                item->setForeground(4, QColor(0x4a, 0xde, 0x80));
                item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            } else if (state->first == QLatin1String("failed")) {
                item->setText(4, tr("Failed (see Logs / History)"));
                item->setForeground(4, QColor(0xff, 0x6b, 0x6b));
            }
        }
    };

    // Keeper (always kept, never movable).
    const QFileInfo keeperInfo(proposal.keeperPath);
    addRow(proposal.keeperPath, proposal.size, keeperInfo.path(),
           proposal.keeperRole, proposal.keeperPath,
           QStringLiteral("keeper"), tr("Keep"), false, false);

    for (const core::FileRow& m : proposal.protectedOnes) {
        addRow(m.relPath, m.size, m.rootPath, m.rootRole, m.absPath,
               QStringLiteral("protected"), tr("Protected (main folder)"),
               false, false);
    }

    for (const core::FileRow& r : proposal.redundant) {
        const auto state = m_actionState.constFind(r.absPath);
        const bool alreadyMoved =
            state != m_actionState.cend() &&
            state->first == QLatin1String("done");
        addRow(r.relPath, r.size, r.rootPath, r.rootRole, r.absPath,
               QStringLiteral("redundant"),
               alreadyMoved ? tr("Moved to segregation")
                            : tr("Move to segregation"),
               true, !alreadyMoved);
    }
}

int ResultsWidget::selectedCount() const {
    int count = 0;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        auto* group = m_tree->topLevelItem(g);
        if (!group->isExpanded()) continue;
        for (int c = 0; c < group->childCount(); ++c) {
            auto* child = group->child(c);
            if (child->data(0, RoleKind).toString() == QLatin1String("redundant") &&
                child->checkState(0) == Qt::Checked) {
                ++count;
            }
        }
    }
    return count;
}

void ResultsWidget::refreshCounters() {
    const int selected = selectedCount();
    QString summary = tr("%1 group(s) · %2 recoverable · %3 selected to move")
                          .arg(m_totalGroups)
                          .arg(humanBytes(m_potentialSavings))
                          .arg(selected);
    if (m_reviewOnlyGroups > 0) {
        summary += tr(" · %1 main-folder review-only").arg(m_reviewOnlyGroups);
    }
    m_summaryLabel->setText(summary);
    m_dryRunBtn->setEnabled(selected > 0);
    m_applyBtn->setEnabled(selected > 0);
}

void ResultsWidget::setAllChecked(bool checked) {
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        auto* group = m_tree->topLevelItem(g);
        if (!group->isExpanded()) {
            // Materialize children so selection applies to hidden groups too.
            onItemExpanded(group);
            group->setExpanded(true);
        }
        for (int c = 0; c < group->childCount(); ++c) {
            auto* child = group->child(c);
            if (child->data(0, RoleKind).toString() == QLatin1String("redundant") &&
                (child->flags() & Qt::ItemIsUserCheckable)) {
                child->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            }
        }
    }
    refreshCounters();
}

void ResultsWidget::runDryRun() {
    runSelected(true);
}

void ResultsWidget::applySelected() {
    runSelected(false);
}

void ResultsWidget::runSelected(bool dryRun) {
    using engine::DuplicateProposal;

    if (m_ctx.jobId == 0) {
        QMessageBox::information(this, tr("No scan yet"),
                                 tr("Run a scan before applying moves."));
        return;
    }
    if (m_ctx.destinationRoot.isEmpty()) {
        QMessageBox::warning(this, tr("No destination"),
                             tr("Select a destination folder first."));
        return;
    }

    // Materialize every group so user choices on lazy rows are honoured.
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        auto* group = m_tree->topLevelItem(g);
        if (!group->isExpanded()) {
            onItemExpanded(group);
            group->setExpanded(true);
        }
    }

    // Collect chosen absolute paths.
    QSet<QString> chosen;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        auto* group = m_tree->topLevelItem(g);
        for (int c = 0; c < group->childCount(); ++c) {
            auto* child = group->child(c);
            if (child->data(0, RoleKind).toString() == QLatin1String("redundant") &&
                child->checkState(0) == Qt::Checked) {
                chosen.insert(child->toolTip(0));
            }
        }
    }
    if (chosen.isEmpty()) return;

    if (!dryRun) {
        const int answer = QMessageBox::question(
            this, tr("Confirm segregation"),
            tr("Move %1 file(s) into the segregation folder?\n\n%2\n\n"
               "Every move is journaled and can be undone from the "
               "History screen.")
                .arg(chosen.size()).arg(m_ctx.destinationRoot),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }

    // Journal every planned move (idempotent: only unplanned sources added).
    m_segregation.planFileActions(static_cast<qint64>(m_ctx.jobId),
                                 m_ctx.destinationRoot, m_proposals);

    // Index planned actions by source path.
    QHash<QString, core::ActionRow> planBySource;
    for (const core::ActionRow& action : m_db.actions()) {
        planBySource.insert(action.sourcePath, action);
    }

    operations::SafeFileOps::Options options;
    options.protectedRoot = m_ctx.protectedRoot;
    options.verifyHash = m_ctx.verifyHash;
    options.dryRun = dryRun;

    int moved = 0;
    int failed = 0;
    int skipped = 0;
    QStringList errors;
    QStringList movedPaths;

    for (const QString& source : chosen) {
        const auto plan = planBySource.constFind(source);
        if (plan == planBySource.cend()) {
            ++skipped;
            errors.append(tr("No journal entry for %1").arg(source));
            continue;
        }
        if (plan->status == QLatin1String("undone")) {
            // Allowed: re-execute by returning it to planned state.
            m_db.finalizeAction(plan->actionId, QStringLiteral("planned"));
            planBySource[source].status = QStringLiteral("planned");
        } else if (plan->status == QLatin1String("done")) {
            ++skipped;
            continue; // already moved earlier
        }

        const core::ActionRow action = planBySource.value(source);
        const auto outcome = m_ops.execute(action, options);
        if (outcome.ok) {
            ++moved;
            if (!dryRun) movedPaths.append(source);
            core::Logger::instance().info(
                dryRun
                    ? tr("[dry-run] would move %1 -> %2").arg(source, outcome.actualDest)
                    : tr("moved %1 -> %2").arg(source, outcome.actualDest));
        } else {
            ++failed;
            errors.append(tr("%1: %2").arg(source, outcome.error));
        }
    }

    if (!movedPaths.isEmpty()) {
        m_db.removeFilesByAbsPath(movedPaths);
    }

    QString summary;
    if (dryRun) {
        summary = tr("Dry run: %1 move(s) validated, %2 would move, %3 failed. "
                     "Nothing changed on disk.")
                      .arg(moved + failed).arg(moved).arg(failed);
    } else {
        summary = tr("%1 move(s) completed, %2 failed, %3 skipped.")
                      .arg(moved).arg(failed).arg(skipped);
    }
    if (!errors.isEmpty()) {
        summary += QLatin1Char('\n') + errors.mid(0, 5).join(QLatin1Char('\n'));
        if (errors.size() > 5) summary += tr("\n… and %1 more (see Logs)")
                                              .arg(errors.size() - 5);
    }

    QMessageBox::information(this,
                             dryRun ? tr("Dry run results")
                                    : tr("Segregation results"),
                             summary);
    emit applyFinished(moved, failed, summary);
}

} // namespace dedupvault::ui