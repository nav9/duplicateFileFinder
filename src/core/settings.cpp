#include "settings.h"
#include <QStandardPaths>
#include <QFileInfo>
#include <QThread>

namespace dedupvault::core {

AppSettings::AppSettings(QObject* parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("DedupVault"), QStringLiteral("DedupVault"))
{
}

// --- Working Folder ---

QString AppSettings::workingFolder() const {
    const QString defaultPath = QDir::home().filePath(QStringLiteral("DedupVault_data"));
    return m_settings.value(KEY_WORKING_FOLDER, defaultPath).toString();
}

void AppSettings::setWorkingFolder(const QString& path) {
    if (path != workingFolder()) {
        m_settings.setValue(KEY_WORKING_FOLDER, path);
        emit workingFolderChanged(path);
        emit settingsChanged(KEY_WORKING_FOLDER);
    }
}

bool AppSettings::isWorkingFolderValid() const {
    return isFolderWritable(workingFolder());
}

bool AppSettings::isFolderWritable(const QString& path) {
    const QFileInfo info(path);
    return info.exists() && info.isDir() && info.isWritable();
}

bool AppSettings::createFolder(const QString& path) {
    return QDir().mkpath(path);
}

// --- Theme ---

AppSettings::Theme AppSettings::theme() const {
    const int val = m_settings.value(KEY_THEME, static_cast<int>(Theme::Dark)).toInt();
    return static_cast<Theme>(val);
}

void AppSettings::setTheme(Theme theme) {
    if (theme != this->theme()) {
        m_settings.setValue(KEY_THEME, static_cast<int>(theme));
        emit themeChanged(theme);
        emit settingsChanged(KEY_THEME);
    }
}

// --- Logging ---

bool AppSettings::loggingEnabled() const {
    return m_settings.value(KEY_LOGGING_ENABLED, true).toBool();
}

void AppSettings::setLoggingEnabled(bool enabled) {
    m_settings.setValue(KEY_LOGGING_ENABLED, enabled);
    emit settingsChanged(KEY_LOGGING_ENABLED);
}

int AppSettings::logLevel() const {
    return m_settings.value(KEY_LOG_LEVEL, DEFAULT_LOG_LEVEL).toInt();
}

void AppSettings::setLogLevel(int level) {
    m_settings.setValue(KEY_LOG_LEVEL, level);
    emit settingsChanged(KEY_LOG_LEVEL);
}

qint64 AppSettings::maxLogFileSize() const {
    return m_settings.value(KEY_MAX_LOG_FILE_SIZE, DEFAULT_MAX_LOG_FILE_SIZE).toLongLong();
}

void AppSettings::setMaxLogFileSize(qint64 bytes) {
    m_settings.setValue(KEY_MAX_LOG_FILE_SIZE, bytes);
    emit settingsChanged(KEY_MAX_LOG_FILE_SIZE);
}

int AppSettings::maxLogFiles() const {
    return m_settings.value(KEY_MAX_LOG_FILES, DEFAULT_MAX_LOG_FILES).toInt();
}

void AppSettings::setMaxLogFiles(int count) {
    m_settings.setValue(KEY_MAX_LOG_FILES, count);
    emit settingsChanged(KEY_MAX_LOG_FILES);
}

// --- Hashing ---

int AppSettings::hashingThreadCount() const {
    const int maxThreads = QThread::idealThreadCount();
    return m_settings.value(KEY_HASHING_THREADS,
                            qBound(1, DEFAULT_HASHING_THREADS, maxThreads)).toInt();
}

void AppSettings::setHashingThreadCount(int count) {
    m_settings.setValue(KEY_HASHING_THREADS, qBound(1, count, 16));
    emit settingsChanged(KEY_HASHING_THREADS);
}

// --- Remembered Paths ---

QString AppSettings::lastMainFolder() const {
    return m_settings.value(KEY_LAST_MAIN_FOLDER).toString();
}

void AppSettings::setLastMainFolder(const QString& path) {
    m_settings.setValue(KEY_LAST_MAIN_FOLDER, path);
    emit settingsChanged(KEY_LAST_MAIN_FOLDER);
}

QStringList AppSettings::lastCandidateFolders() const {
    return m_settings.value(KEY_LAST_CANDIDATES).toStringList();
}

void AppSettings::setLastCandidateFolders(const QStringList& paths) {
    m_settings.setValue(KEY_LAST_CANDIDATES, paths);
    emit settingsChanged(KEY_LAST_CANDIDATES);
}

QString AppSettings::lastDestinationFolder() const {
    return m_settings.value(KEY_LAST_DESTINATION).toString();
}

void AppSettings::setLastDestinationFolder(const QString& path) {
    m_settings.setValue(KEY_LAST_DESTINATION, path);
    emit settingsChanged(KEY_LAST_DESTINATION);
}

// --- Window State ---

QByteArray AppSettings::windowGeometry() const {
    return m_settings.value(KEY_WINDOW_GEOMETRY).toByteArray();
}

void AppSettings::setWindowGeometry(const QByteArray& geometry) {
    m_settings.setValue(KEY_WINDOW_GEOMETRY, geometry);
}

QByteArray AppSettings::windowState() const {
    return m_settings.value(KEY_WINDOW_STATE).toByteArray();
}

void AppSettings::setWindowState(const QByteArray& state) {
    m_settings.setValue(KEY_WINDOW_STATE, state);
}

// --- Generic ---

QVariant AppSettings::value(const QString& key, const QVariant& defaultValue) const {
    return m_settings.value(key, defaultValue);
}

void AppSettings::setValue(const QString& key, const QVariant& value) {
    m_settings.setValue(key, value);
    emit settingsChanged(key);
}

void AppSettings::sync() {
    m_settings.sync();
}

} // namespace dedupvault::core
