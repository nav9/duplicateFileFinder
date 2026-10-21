#include "filesystemutils.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryFile>
#include <QStorageInfo>
#include <algorithm>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace dedupvault::core {

QString FileSystemUtils::normalizePath(const QString& path) {
    if (path.isEmpty()) return path;
    // Use std::filesystem for consistent normalization
    std::filesystem::path fsPath(path.toStdString());
    std::error_code ec;
    auto normalized = fsPath.lexically_normal();
    QString result = QString::fromStdString(normalized.string());
    // Ensure no trailing separator (except root)
    if (result.size() > 1 && result.endsWith(QDir::separator())) {
        result.chop(1);
    }
    return result;
}

QString FileSystemUtils::canonicalPath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical; // empty if path doesn't exist or can't be resolved
}

bool FileSystemUtils::isContainedIn(const QString& childPath, const QString& parentPath) {
    const QString normChild = normalizePath(QFileInfo(childPath).absoluteFilePath());
    const QString normParent = normalizePath(QFileInfo(parentPath).absoluteFilePath());

    if (normChild == normParent) return true;

    const QString parentPrefix = normParent + QDir::separator();
    return normChild.startsWith(parentPrefix);
}

bool FileSystemUtils::isSamePath(const QString& path1, const QString& path2) {
    // First try canonical comparison
    const QString canon1 = canonicalPath(path1);
    const QString canon2 = canonicalPath(path2);
    if (!canon1.isEmpty() && !canon2.isEmpty()) {
        return canon1 == canon2;
    }
    // Fallback to normalized comparison
    return normalizePath(QFileInfo(path1).absoluteFilePath()) ==
           normalizePath(QFileInfo(path2).absoluteFilePath());
}

QList<QPair<int, int>> FileSystemUtils::findOverlappingRoots(const QStringList& roots) {
    QList<QPair<int, int>> overlaps;
    for (int i = 0; i < roots.size(); ++i) {
        for (int j = i + 1; j < roots.size(); ++j) {
            if (isContainedIn(roots[i], roots[j]) ||
                isContainedIn(roots[j], roots[i]) ||
                isSamePath(roots[i], roots[j])) {
                overlaps.append({i, j});
            }
        }
    }
    return overlaps;
}

bool FileSystemUtils::isProtected(const QString& path, const QString& protectedRoot) {
    if (protectedRoot.isEmpty()) return false;
    return isContainedIn(path, protectedRoot);
}

bool FileSystemUtils::isCaseSensitiveFileSystem(const QString& path) {
#ifdef Q_OS_LINUX
    Q_UNUSED(path);
    return true; // Most Linux filesystems are case-sensitive
#elif defined(Q_OS_WIN)
    Q_UNUSED(path);
    return false; // NTFS is case-insensitive by default
#elif defined(Q_OS_MACOS)
    Q_UNUSED(path);
    return false; // APFS/HFS+ default to case-insensitive
#else
    Q_UNUSED(path);
    return true;
#endif
}

bool FileSystemUtils::isSymlink(const QString& path) {
    return QFileInfo(path).isSymLink();
}

bool FileSystemUtils::isDirectorySymlink(const QString& path) {
    const QFileInfo info(path);
    return info.isSymLink() && info.isDir();
}

std::optional<FileIdentity> FileSystemUtils::getFileIdentity(const QString& path) {
#ifdef Q_OS_UNIX
    struct stat st;
    if (::stat(path.toLocal8Bit().constData(), &st) == 0) {
        FileIdentity id;
        id.deviceId = static_cast<uint64_t>(st.st_dev);
        id.inodeOrFileId = static_cast<uint64_t>(st.st_ino);
        return id;
    }
    return std::nullopt;
#elif defined(Q_OS_WIN)
    HANDLE hFile = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return std::nullopt;

    BY_HANDLE_FILE_INFORMATION info;
    bool ok = GetFileInformationByHandle(hFile, &info);
    CloseHandle(hFile);

    if (ok) {
        FileIdentity id;
        id.deviceId = info.dwVolumeSerialNumber;
        id.inodeOrFileId = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) |
                           info.nFileIndexLow;
        return id;
    }
    return std::nullopt;
#else
    Q_UNUSED(path);
    return std::nullopt;
#endif
}

bool FileSystemUtils::isReadable(const QString& path) {
    return QFileInfo(path).isReadable();
}

bool FileSystemUtils::isWritable(const QString& path) {
    return QFileInfo(path).isWritable();
}

QString FileSystemUtils::generateSafeName(const QString& destDir, const QString& fileName) {
    const QDir dir(destDir);
    if (!dir.exists(fileName)) {
        return fileName;
    }

    const QString stem = fileStem(fileName);
    const QString ext = fileExtension(fileName);

    for (int i = 1; i < 100000; ++i) {
        const QString candidate = QStringLiteral("%1_%2%3").arg(stem).arg(i).arg(ext);
        if (!dir.exists(candidate)) {
            return candidate;
        }
    }

    // Extremely unlikely fallback
    return QStringLiteral("%1_%2%3").arg(stem).arg(
        QDateTime::currentMSecsSinceEpoch()).arg(ext);
}

QString FileSystemUtils::joinPath(const QString& base, const QString& relative) {
    return QDir(base).filePath(relative);
}

QString FileSystemUtils::relativePath(const QString& parentPath, const QString& childPath) {
    const QDir parentDir(parentPath);
    return parentDir.relativeFilePath(childPath);
}

bool FileSystemUtils::isSameFileSystem(const QString& path1, const QString& path2) {
    const QStorageInfo storage1(path1);
    const QStorageInfo storage2(path2);
    return storage1.device() == storage2.device();
}

FileSystemUtils::MoveValidation FileSystemUtils::validateMove(
    const QString& sourcePath, const QString& destDir, const QString& protectedRoot) {

    MoveValidation result;

    // Source must exist
    if (!QFileInfo::exists(sourcePath)) {
        result.error = QStringLiteral("Source does not exist: %1").arg(sourcePath);
        return result;
    }

    // Source must not be inside protected root
    if (isProtected(sourcePath, protectedRoot)) {
        result.error = QStringLiteral("Source is inside the protected root: %1").arg(sourcePath);
        return result;
    }

    // Destination must not be inside source
    if (isContainedIn(destDir, sourcePath)) {
        result.error = QStringLiteral("Destination is inside the source: %1 -> %2")
                           .arg(sourcePath, destDir);
        return result;
    }

    // Source must not be the destination itself
    if (isSamePath(sourcePath, destDir)) {
        result.error = QStringLiteral("Source and destination are the same path");
        return result;
    }

    result.valid = true;
    return result;
}

QString FileSystemUtils::fileExtension(const QString& path) {
    const QFileInfo info(path);
    const QString suffix = info.suffix();
    return suffix.isEmpty() ? QString() : QStringLiteral(".%1").arg(suffix);
}

QString FileSystemUtils::fileStem(const QString& path) {
    return QFileInfo(path).completeBaseName();
}

} // namespace dedupvault::core
