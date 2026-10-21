#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMutex>
#include <QFile>
#include <QVector>
#include <QTextStream>
#include <functional>

namespace dedupvault::core {

/**
 * @brief Log entry structure for in-memory circular buffer and UI display.
 */
struct LogEntry {
    QDateTime   timestamp;
    int         level;       // Maps to Logger::Level
    QString     threadName;
    QString     message;
};

/**
 * @brief Rotating file logger with in-memory circular buffer for UI.
 *
 * Thread-safe. Supports runtime enable/disable, level filtering,
 * configurable rotation, and Qt message handler integration.
 */
class Logger : public QObject {
    Q_OBJECT

public:
    enum Level {
        Trace   = 0,
        Debug   = 1,
        Info    = 2,
        Warning = 3,
        Error   = 4,
        Fatal   = 5
    };
    Q_ENUM(Level)

    static Logger& instance();

    /// Initialize the logger with a log directory path
    void initialize(const QString& logDir, bool enabled = true,
                    Level minLevel = Info,
                    qint64 maxFileSize = 10 * 1024 * 1024,
                    int maxFiles = 5);

    /// Install as Qt global message handler
    void installMessageHandler();

    /// Log a message
    void log(Level level, const QString& message);

    // Convenience methods
    void trace(const QString& msg)   { log(Trace, msg); }
    void debug(const QString& msg)   { log(Debug, msg); }
    void info(const QString& msg)    { log(Info, msg); }
    void warning(const QString& msg) { log(Warning, msg); }
    void error(const QString& msg)   { log(Error, msg); }
    void fatal(const QString& msg)   { log(Fatal, msg); }

    // Configuration
    bool isEnabled() const;
    void setEnabled(bool enabled);
    Level minLevel() const;
    void setMinLevel(Level level);
    void setMaxFileSize(qint64 bytes);
    void setMaxFiles(int count);

    /// Flush output
    void flush();

    /// Get recent log entries (from circular buffer) for UI
    QVector<LogEntry> recentEntries(int maxCount = 1000) const;

    /// Search/filter entries in circular buffer
    QVector<LogEntry> searchEntries(const QString& searchText,
                                    Level minLevel = Trace,
                                    int maxCount = 1000) const;

    static QString levelToString(Level level);
    static Level levelFromQt(QtMsgType type);

signals:
    void newLogEntry(const LogEntry& entry);

private:
    Logger();
    ~Logger() override;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void writeToFile(const QString& formattedLine);
    void rotateIfNeeded();
    QString currentLogFilePath() const;
    void addToCircularBuffer(const LogEntry& entry);

    static void qtMessageHandler(QtMsgType type,
                                  const QMessageLogContext& context,
                                  const QString& msg);

    mutable QMutex m_mutex;
    bool           m_enabled = true;
    Level          m_minLevel = Info;
    QString        m_logDir;
    qint64         m_maxFileSize = 10 * 1024 * 1024;
    int            m_maxFiles = 5;
    QFile          m_currentFile;
    QTextStream    m_stream;
    bool           m_initialized = false;

    // Circular buffer for recent entries
    static constexpr int BUFFER_CAPACITY = 5000;
    QVector<LogEntry> m_buffer;
    int               m_bufferHead = 0;
    int               m_bufferCount = 0;
};

} // namespace dedupvault::core
