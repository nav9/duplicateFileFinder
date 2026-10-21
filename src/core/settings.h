#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QDir>
#include <QVariant>

namespace dedupvault::core {

/**
 * @brief Persistent application settings using QSettings.
 *
 * Manages all configurable parameters including working folder, theme,
 * logging preferences, hashing concurrency, and remembered paths.
 */
class AppSettings : public QObject {
    Q_OBJECT

public:
    enum class Theme { Dark, Light };
    Q_ENUM(Theme)

    explicit AppSettings(QObject* parent = nullptr);
    ~AppSettings() override = default;

    // Working folder
    QString workingFolder() const;
    void setWorkingFolder(const QString& path);
    bool isWorkingFolderValid() const;
    static bool isFolderWritable(const QString& path);
    static bool createFolder(const QString& path);

    // Theme
    Theme theme() const;
    void setTheme(Theme theme);

    // Logging
    bool loggingEnabled() const;
    void setLoggingEnabled(bool enabled);
    int logLevel() const;
    void setLogLevel(int level);
    qint64 maxLogFileSize() const;
    void setMaxLogFileSize(qint64 bytes);
    int maxLogFiles() const;
    void setMaxLogFiles(int count);

    // Hashing
    int hashingThreadCount() const;
    void setHashingThreadCount(int count);

    // Remembered paths
    QString lastMainFolder() const;
    void setLastMainFolder(const QString& path);
    QStringList lastCandidateFolders() const;
    void setLastCandidateFolders(const QStringList& paths);
    QString lastDestinationFolder() const;
    void setLastDestinationFolder(const QString& path);

    // Window state
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
    QByteArray windowState() const;
    void setWindowState(const QByteArray& state);

    // Generic
    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setValue(const QString& key, const QVariant& value);

    /// Flush pending writes to disk.
    void sync();

signals:
    void settingsChanged(const QString& key);
    void themeChanged(Theme theme);
    void workingFolderChanged(const QString& path);

private:
    QSettings m_settings;

    static constexpr const char* KEY_WORKING_FOLDER      = "General/WorkingFolder";
    static constexpr const char* KEY_THEME                = "General/Theme";
    static constexpr const char* KEY_LOGGING_ENABLED      = "Logging/Enabled";
    static constexpr const char* KEY_LOG_LEVEL            = "Logging/Level";
    static constexpr const char* KEY_MAX_LOG_FILE_SIZE    = "Logging/MaxFileSize";
    static constexpr const char* KEY_MAX_LOG_FILES        = "Logging/MaxFiles";
    static constexpr const char* KEY_HASHING_THREADS      = "Performance/HashingThreads";
    static constexpr const char* KEY_LAST_MAIN_FOLDER     = "Paths/LastMainFolder";
    static constexpr const char* KEY_LAST_CANDIDATES      = "Paths/LastCandidateFolders";
    static constexpr const char* KEY_LAST_DESTINATION     = "Paths/LastDestination";
    static constexpr const char* KEY_WINDOW_GEOMETRY      = "Window/Geometry";
    static constexpr const char* KEY_WINDOW_STATE         = "Window/State";

    static constexpr qint64 DEFAULT_MAX_LOG_FILE_SIZE = 10 * 1024 * 1024; // 10 MB
    static constexpr int DEFAULT_MAX_LOG_FILES = 5;
    static constexpr int DEFAULT_HASHING_THREADS = 4;
    static constexpr int DEFAULT_LOG_LEVEL = 2; // Info
};

} // namespace dedupvault::core
