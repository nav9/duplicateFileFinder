#include "logger.h"
#include <QDir>
#include <QThread>
#include <QFileInfo>
#include <QCoreApplication>

namespace dedupvault::core {

Logger::Logger()
    : QObject(nullptr)
{
    m_buffer.resize(BUFFER_CAPACITY);
}

Logger::~Logger() {
    flush();
    if (m_currentFile.isOpen()) {
        m_currentFile.close();
    }
}

Logger& Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

void Logger::initialize(const QString& logDir, bool enabled, Level minLevel,
                         qint64 maxFileSize, int maxFiles) {
    QMutexLocker lock(&m_mutex);

    m_logDir = logDir;
    m_enabled = enabled;
    m_minLevel = minLevel;
    m_maxFileSize = maxFileSize;
    m_maxFiles = maxFiles;

    QDir dir(logDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString filePath = currentLogFilePath();
    m_currentFile.setFileName(filePath);
    if (m_currentFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(&m_currentFile);
    }

    m_initialized = true;
}

void Logger::installMessageHandler() {
    qInstallMessageHandler(Logger::qtMessageHandler);
}

void Logger::log(Level level, const QString& message) {
    QMutexLocker lock(&m_mutex);

    if (!m_enabled || level < m_minLevel) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QString threadName = QThread::currentThread()->objectName().isEmpty()
        ? QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()))
        : QThread::currentThread()->objectName();

    LogEntry entry;
    entry.timestamp = now;
    entry.level = level;
    entry.threadName = threadName;
    entry.message = message;

    addToCircularBuffer(entry);

    const QString formatted = QStringLiteral("[%1] [%2] [%3] %4")
        .arg(now.toString(Qt::ISODateWithMs),
             levelToString(level),
             threadName,
             message);

    if (m_initialized) {
        writeToFile(formatted);

        // Flush on warning and above
        if (level >= Warning) {
            m_stream.flush();
            m_currentFile.flush();
        }
    }

    // Emit signal (unlock first to avoid deadlock with connected slots)
    lock.unlock();
    emit newLogEntry(entry);
}

bool Logger::isEnabled() const {
    QMutexLocker lock(&m_mutex);
    return m_enabled;
}

void Logger::setEnabled(bool enabled) {
    QMutexLocker lock(&m_mutex);
    m_enabled = enabled;
}

Logger::Level Logger::minLevel() const {
    QMutexLocker lock(&m_mutex);
    return m_minLevel;
}

void Logger::setMinLevel(Level level) {
    QMutexLocker lock(&m_mutex);
    m_minLevel = level;
}

void Logger::setMaxFileSize(qint64 bytes) {
    QMutexLocker lock(&m_mutex);
    m_maxFileSize = bytes;
}

void Logger::setMaxFiles(int count) {
    QMutexLocker lock(&m_mutex);
    m_maxFiles = count;
}

void Logger::flush() {
    QMutexLocker lock(&m_mutex);
    if (m_currentFile.isOpen()) {
        m_stream.flush();
        m_currentFile.flush();
    }
}

QVector<LogEntry> Logger::recentEntries(int maxCount) const {
    QMutexLocker lock(&m_mutex);

    QVector<LogEntry> result;
    const int count = qMin(maxCount, m_bufferCount);
    result.reserve(count);

    // Read from oldest to newest
    int start = (m_bufferHead - m_bufferCount + BUFFER_CAPACITY) % BUFFER_CAPACITY;
    int skip = m_bufferCount - count;
    start = (start + skip) % BUFFER_CAPACITY;

    for (int i = 0; i < count; ++i) {
        int idx = (start + i) % BUFFER_CAPACITY;
        result.append(m_buffer[idx]);
    }

    return result;
}

QVector<LogEntry> Logger::searchEntries(const QString& searchText,
                                         Level minLevel, int maxCount) const {
    QMutexLocker lock(&m_mutex);

    QVector<LogEntry> result;
    int start = (m_bufferHead - m_bufferCount + BUFFER_CAPACITY) % BUFFER_CAPACITY;

    for (int i = 0; i < m_bufferCount && result.size() < maxCount; ++i) {
        int idx = (start + i) % BUFFER_CAPACITY;
        const auto& entry = m_buffer[idx];

        if (entry.level < minLevel) continue;
        if (!searchText.isEmpty() &&
            !entry.message.contains(searchText, Qt::CaseInsensitive)) continue;

        result.append(entry);
    }

    return result;
}

QString Logger::levelToString(Level level) {
    switch (level) {
        case Trace:   return QStringLiteral("TRACE");
        case Debug:   return QStringLiteral("DEBUG");
        case Info:    return QStringLiteral("INFO ");
        case Warning: return QStringLiteral("WARN ");
        case Error:   return QStringLiteral("ERROR");
        case Fatal:   return QStringLiteral("FATAL");
    }
    return QStringLiteral("?????");
}

Logger::Level Logger::levelFromQt(QtMsgType type) {
    switch (type) {
        case QtDebugMsg:    return Debug;
        case QtInfoMsg:     return Info;
        case QtWarningMsg:  return Warning;
        case QtCriticalMsg: return Error;
        case QtFatalMsg:    return Fatal;
    }
    return Debug;
}

void Logger::writeToFile(const QString& formattedLine) {
    if (!m_currentFile.isOpen()) return;

    rotateIfNeeded();

    m_stream << formattedLine << '\n';
}

void Logger::rotateIfNeeded() {
    if (m_currentFile.size() < m_maxFileSize) return;

    m_stream.flush();
    m_currentFile.close();

    // Rotate files: log.4 -> deleted, log.3 -> log.4, ... log -> log.1
    const QDir dir(m_logDir);
    const QString baseName = QStringLiteral("dedupvault.log");

    // Remove oldest
    const QString oldest = dir.filePath(
        QStringLiteral("%1.%2").arg(baseName).arg(m_maxFiles));
    QFile::remove(oldest);

    // Shift existing files
    for (int i = m_maxFiles - 1; i >= 1; --i) {
        const QString from = dir.filePath(
            QStringLiteral("%1.%2").arg(baseName).arg(i));
        const QString to = dir.filePath(
            QStringLiteral("%1.%2").arg(baseName).arg(i + 1));
        QFile::rename(from, to);
    }

    // Rename current to .1
    const QString currentPath = dir.filePath(baseName);
    const QString firstBackup = dir.filePath(
        QStringLiteral("%1.1").arg(baseName));
    QFile::rename(currentPath, firstBackup);

    // Open new current
    m_currentFile.setFileName(currentPath);
    if (m_currentFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(&m_currentFile);
    }
}

QString Logger::currentLogFilePath() const {
    return QDir(m_logDir).filePath(QStringLiteral("dedupvault.log"));
}

void Logger::addToCircularBuffer(const LogEntry& entry) {
    m_buffer[m_bufferHead] = entry;
    m_bufferHead = (m_bufferHead + 1) % BUFFER_CAPACITY;
    if (m_bufferCount < BUFFER_CAPACITY) {
        ++m_bufferCount;
    }
}

void Logger::qtMessageHandler(QtMsgType type,
                                const QMessageLogContext& context,
                                const QString& msg) {
    Q_UNUSED(context);
    Logger::instance().log(levelFromQt(type), msg);
}

} // namespace dedupvault::core
