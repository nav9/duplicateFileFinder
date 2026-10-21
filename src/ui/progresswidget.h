#pragma once

#include "engine/scanengine.h"

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class QGroupBox;

namespace dedupvault::ui {

/**
 * @brief Live progress of the running job: phases, progress bars,
 * statistics and the pause / resume / cancel controls.
 */
class ProgressWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProgressWidget(QWidget* parent = nullptr);

    void updateProgress(const engine::EngineProgress& progress);

    /// Enable/disable controls depending on engine state.
    void setRunning(bool running);
    /// Show the "Resume Interrupted Job" control when one exists.
    void setResumeAvailable(bool available);

signals:
    void pauseRequested();
    void resumeRequested();
    void cancelRequested();
    void resumeExistingRequested();

private:
    QLabel*       m_phaseLabel = nullptr;
    QLabel*       m_messageLabel = nullptr;
    QProgressBar* m_traversalBar = nullptr;
    QProgressBar* m_hashBar = nullptr;
    QLabel*       m_traversalValue = nullptr;
    QLabel*       m_hashValue = nullptr;

    QLabel* m_statFiles = nullptr;
    QLabel* m_statBytes = nullptr;
    QLabel* m_statHashed = nullptr;
    QLabel* m_statGroups = nullptr;
    QLabel* m_statSavings = nullptr;
    QLabel* m_statErrors = nullptr;
    QLabel* m_statReviews = nullptr;

    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_resumeBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_resumeJobBtn = nullptr;
    QGroupBox*   m_statsGroup = nullptr;
    QGroupBox*   m_controlsGroup = nullptr;
};

} // namespace dedupvault::ui