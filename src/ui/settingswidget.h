#pragma once

#include "core/settings.h"
#include "ui/thememanager.h"

#include <QWidget>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

namespace dedupvault::ui {

/**
 * @brief Settings page: theme, working folder, logging preferences and
 * every other configurable parameter, persisted for the next launch.
 */
class SettingsWidget : public QWidget {
    Q_OBJECT

public:
    SettingsWidget(core::AppSettings& settings, ThemeManager& theme,
                   QWidget* parent = nullptr);

    /// Re-read persisted values into the controls (e.g. after startup).
    void reload();

signals:
    void settingsSaved();

private slots:
    void save();
    void browseWorkingFolder();
    void createWorkingFolder();
    void testWorkingFolder();

private:
    void setStatus(const QLabel* label, const QString& text,
                   const char* role) const;
    void refreshWorkingFolderState();

    core::AppSettings& m_settings;
    ThemeManager&      m_theme;

    QLineEdit*     m_workingFolderEdit = nullptr;
    QLabel*        m_workingFolderStatus = nullptr;
    QPushButton*   m_createFolderBtn = nullptr;
    QComboBox*     m_themeCombo = nullptr;
    QCheckBox*     m_loggingEnabled = nullptr;
    QComboBox*     m_logLevelCombo = nullptr;
    QSpinBox*      m_logSizeSpin = nullptr;      // MB per file
    QSpinBox*      m_logFileCountSpin = nullptr;
    QSpinBox*      m_hashThreadsSpin = nullptr;
    QCheckBox*     m_defaultDryRun = nullptr;
    QCheckBox*     m_defaultByteVerify = nullptr;
    QSpinBox*      m_minSizeSpin = nullptr;      // KB
    QDoubleSpinBox* m_overlapFloorSpin = nullptr;
    QSpinBox*      m_previewLimitSpin = nullptr;
    QPushButton*   m_saveBtn = nullptr;
};

} // namespace dedupvault::ui