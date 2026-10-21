#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace dedupvault::ui {

/**
 * @brief Landing page: minimal-decision journey.
 *
 * The dashboard answers the three questions a returning user has — is the
 * working folder fine, is there a scan to resume, and where do I start —
 * and reserves room for future menu entries (duplicate images/videos,
 * unique sentences, junk cleaning), which appear as clearly disabled
 * previews so the layout never has to be redesigned later.
 */
class DashboardWidget : public QWidget {
    Q_OBJECT

public:
    explicit DashboardWidget(QWidget* parent = nullptr);

    void setWorkingFolder(const QString& path, bool writable);
    void setResumableJob(bool available, qint64 jobId);
    void setLastSummary(const QString& summary);

signals:
    void startScanRequested();
    void resumeJobRequested();
    void viewResultsRequested();
    void openLogsRequested();
    void openSettingsRequested();

private:
    QLabel*    m_folderValue = nullptr;
    QLabel*    m_folderStatus = nullptr;
    QLabel*    m_resumeValue = nullptr;
    QLabel*    m_summaryValue = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_resumeBtn = nullptr;
};

} // namespace dedupvault::ui