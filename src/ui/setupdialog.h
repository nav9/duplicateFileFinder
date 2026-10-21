#pragma once

#include "core/settings.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace dedupvault::ui {

/**
 * @brief First-run working-folder selection.
 *
 * Verifies write privileges on the configured working folder. If the folder
 * is missing or read-only the user can browse for another folder or create
 * one; every choice is persisted in Settings so it only ever has to be
 * made once.
 */
class SetupDialog : public QDialog {
    Q_OBJECT

public:
    /// \param suggested initial folder (settings value / default)
    /// \param parent parent widget
    explicit SetupDialog(const QString& suggested, QWidget* parent = nullptr);

    /// Folder that passed the write-privilege test (already persisted).
    QString chosenFolder() const { return m_chosen; }

    /// Run the full check/browse/create interaction until a writable folder
    /// is confirmed or the user aborts. \return true when a folder was chosen.
    bool run();

private slots:
    void browse();
    void createAndUse();
    void testCurrent();

private:
    void setStatus(const QString& text, const char* role);
    void refreshActionState();

    QLineEdit* m_pathEdit = nullptr;
    QLabel*    m_statusLabel = nullptr;
    QString    m_chosen;
    QPushButton* m_okButton = nullptr;
    QPushButton* m_createButton = nullptr;
};

} // namespace dedupvault::ui