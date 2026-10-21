#pragma once

#include "core/databasemanager.h"
#include "engine/duplicatedetector.h"
#include "operations/segregationmanager.h"
#include "operations/safefileops.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;

namespace dedupvault::ui {

/**
 * @brief Exact-duplicate results: groups, keepers and proposed moves.
 *
 * The table scrolls lazily (only top-level groups are materialized up
 * front) so multi-million-file catalogs stay responsive. Redundant copies
 * arrive pre-checked; the user can skip individual rows and act on the rest.
 * Two explicit actions: Dry Run validates everything and reports what would
 * happen without touching the filesystem; Segregate Selected performs the
 * moves into the destination's segregation folder.
 */
class ResultsWidget : public QWidget {
    Q_OBJECT

public:
    struct Context {
        QString protectedRoot;
        QString destinationRoot;
        quint64 jobId = 0;
        bool    verifyHash = true;
    };

    ResultsWidget(core::DatabaseManager& db,
                  operations::SafeFileOps& ops,
                  operations::SegregationManager& segregation,
                  QWidget* parent = nullptr);

    void setContext(const Context& ctx);
    void reload(const QVector<engine::DuplicateProposal>& proposals);

    /// Ready-to-apply count (checked redundant rows).
    int selectedCount() const;

signals:
    void applyFinished(int moved, int failed, const QString& summary);

private slots:
    /// Run the selected plan in dry-run mode (validates, changes nothing).
    void runDryRun();
    /// Actually move the selected copies into the segregation folder.
    void applySelected();
    void setAllChecked(bool checked);
    void onItemExpanded(QTreeWidgetItem* groupItem);

private:
    void runSelected(bool dryRun);
    QTreeWidgetItem* buildGroupItem(const engine::DuplicateProposal& proposal,
                                    int index);
    void populateGroup(QTreeWidgetItem* groupItem,
                       const engine::DuplicateProposal& proposal);
    void refreshCounters();

    core::DatabaseManager&          m_db;
    operations::SafeFileOps&        m_ops;
    operations::SegregationManager&  m_segregation;
    Context                         m_ctx;
    QVector<engine::DuplicateProposal> m_proposals;

    QHash<QString, QPair<QString, qint64>> m_actionState; // src -> (status, id)

    QTreeWidget* m_tree = nullptr;
    QLabel*      m_summaryLabel = nullptr;
    QLabel*      m_hintLabel = nullptr;
    QPushButton* m_applyBtn = nullptr;
    QPushButton* m_dryRunBtn = nullptr;
    QPushButton* m_checkAllBtn = nullptr;
    QPushButton* m_checkNoneBtn = nullptr;
    int          m_totalGroups = 0;
    int          m_reviewOnlyGroups = 0;
    qint64       m_potentialSavings = 0;
};

} // namespace dedupvault::ui