#pragma once

#include "core/settings.h"

#include <QWidget>

class QListWidget;
class QLineEdit;
class QLabel;
class QPushButton;

namespace dedupvault::ui {

/**
 * @brief Selection of the three folder roles: protected main folder,
 * one or more candidate folders and the segregation destination.
 *
 * Selections are remembered in Settings and restored on the next run, so
 * the usual journey is: open the app, press Start Scan.
 */
class FolderSelectionWidget : public QWidget {
    Q_OBJECT

public:
    explicit FolderSelectionWidget(QWidget* parent = nullptr);

    QString mainFolder() const;
    QStringList candidateFolders() const;
    QString destinationFolder() const;

    void setMainFolder(const QString& path);
    void setCandidateFolders(const QStringList& paths);
    void setDestinationFolder(const QString& path);

    /// Restore remembered selections (Settings) into the UI.
    void loadFromSettings(const core::AppSettings& settings);
    /// Persist the current UI selection into Settings.
    void saveToSettings(core::AppSettings& settings) const;

    /// User-facing validation (exists, overlaps, roles). Empty = valid.
    QString validationError() const;

signals:
    void foldersChanged();

private slots:
    void pickMainFolder();
    void addCandidateFolders();
    void removeSelectedCandidate();
    void pickDestinationFolder();
    void onChanged();

private:
    void refreshWarnings();

    QLineEdit*   m_mainEdit = nullptr;
    QListWidget* m_candidatesList = nullptr;
    QLineEdit*   m_destEdit = nullptr;
    QLabel*      m_warningLabel = nullptr;
    QPushButton* m_removeCandidateBtn = nullptr;
};

} // namespace dedupvault::ui