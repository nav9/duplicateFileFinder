#pragma once

#include "core/settings.h"
#include "engine/scanengine.h"

#include <QWidget>

class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;

namespace dedupvault::ui {

/**
 * @brief Scan configuration controls (the Start Scan button lives in the
 * page footer so it stays reachable on small displays).
 *
 * Every knob that has a sensible Settings default is exposed here and in
 * Settings; values chosen here are remembered for the next run.
 */
class ScanConfigWidget : public QWidget {
    Q_OBJECT

public:
    explicit ScanConfigWidget(QWidget* parent = nullptr);

    void loadFromSettings(const core::AppSettings& settings);
    void saveToSettings(core::AppSettings& settings) const;

    /// Copy the current control values into a job config.
    void applyTo(engine::ScanJobConfig& config) const;

    bool dryRun() const;

signals:
    void configChanged();

private:
    QCheckBox*     m_dryRunCheck = nullptr;
    QCheckBox*     m_mainFolderDupCheck = nullptr;
    QCheckBox*     m_completeHashCheck = nullptr;
    QCheckBox*     m_byteVerifyCheck = nullptr;
    QCheckBox*     m_followSymlinkCheck = nullptr;
    QCheckBox*     m_analyzeFoldersCheck = nullptr;
    QSpinBox*      m_hashThreadsSpin = nullptr;
    QSpinBox*      m_minSizeSpin = nullptr;
    QSpinBox*      m_maxSizeSpin = nullptr;
    QDoubleSpinBox* m_overlapFloorSpin = nullptr;
    QLabel*        m_dryRunHint = nullptr;
};

} // namespace dedupvault::ui