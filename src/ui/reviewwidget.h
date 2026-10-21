#pragma once

#include "core/databasemanager.h"
#include "operations/segregationmanager.h"
#include "operations/safefileops.h"
#include "operations/undomanager.h"

#include <QWidget>

class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QComboBox;

namespace dedupvault::ui {

/**
 * @brief Review area split into two tabs:
 *
 *  - Folder Review: folder/subtree matches (exact duplicate subtrees,
 *    partial overlaps, structured projects) that require explicit approval
 *    before anything may move.
 *  - History & Undo: the action journal with one-click undo of completed
 *    moves (and of approved subtree moves).
 *
 * Folder overlap scores are evidence for review only: approving a subtree
 * move is always an explicit user action, never an automatic consequence.
 */
class ReviewWidget : public QWidget {
    Q_OBJECT

public:
    struct Context {
        QString protectedRoot;
        QString destinationRoot;
        quint64 jobId = 0;
        bool    dryRun = true;
        bool    verifyHash = true;
    };

    ReviewWidget(core::DatabaseManager& db,
                 operations::SafeFileOps& ops,
                 operations::SegregationManager& segregation,
                 operations::UndoManager& undo,
                 QWidget* parent = nullptr);

    void setContext(const Context& ctx);
    void refreshFolderMatches();
    void refreshHistory();

private slots:
    void approveSelectedSubtrees();
    void skipSelectedMatches();
    void undoSelected();
    void undoAllCompleted();
    void onHistoryFilterChanged(int index);

private:
    void buildFolderTab();
    void buildHistoryTab();
    QString subtreeDestination(const core::FolderMatchRow& match, bool* ok) const;

    core::DatabaseManager&         m_db;
    operations::SafeFileOps&       m_ops;
    operations::SegregationManager& m_segregation;
    operations::UndoManager&       m_undo;
    Context                        m_ctx;

    QTabWidget* m_tabs = nullptr;
    QTreeWidget* m_matchTree = nullptr;
    QTreeWidget* m_historyTree = nullptr;
    QComboBox*  m_historyFilter = nullptr;
    QLabel*     m_matchSummary = nullptr;
    QLabel*     m_historySummary = nullptr;
    QPushButton* m_approveBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
    QPushButton* m_undoBtn = nullptr;
    QPushButton* m_undoAllBtn = nullptr;
};

} // namespace dedupvault::ui