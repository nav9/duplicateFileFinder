#pragma once

#include <QString>
#include <QStringList>
#include <QFileInfo>
#include <filesystem>
#include <optional>
#include <cstdint>

namespace dedupvault::core {

/**
 * @brief File identity information (device + inode on Unix, file ID on Windows).
 */
struct FileIdentity {
    uint64_t deviceId = 0;
    uint64_t inodeOrFileId = 0;

    bool operator==(const FileIdentity& other) const {
        return deviceId == other.deviceId && inodeOrFileId == other.inodeOrFileId;
    }
    bool operator!=(const FileIdentity& other) const { return !(*this == other); }
};

/**
 * @brief Centralized, cross-platform filesystem utility layer.
 *
 * All path comparisons, containment checks, and move operations
 * go through this class to ensure consistent behavior.
 */
class FileSystemUtils {
public:
    FileSystemUtils() = delete;

    /// Normalize a path for consistent comparison (resolves . and .. but not symlinks)
    static QString normalizePath(const QString& path);

    /// Get the canonical (fully resolved) path, returns empty on error
    static QString canonicalPath(const QString& path);

    /// Check if child is contained within parent directory
    static bool isContainedIn(const QString& childPath, const QString& parentPath);

    /// Check if two paths refer to the same filesystem entry
    static bool isSamePath(const QString& path1, const QString& path2);

    /// Detect overlapping roots in a list of paths. Returns pairs of (i, j) indices.
    static QList<QPair<int, int>> findOverlappingRoots(const QStringList& roots);

    /// Check if a path is or is inside the protected root
    static bool isProtected(const QString& path, const QString& protectedRoot);

    /// Check if the filesystem at the given path is case-sensitive
    static bool isCaseSensitiveFileSystem(const QString& path);

    /// Check if a path is a symbolic link
    static bool isSymlink(const QString& path);

    /// Check if a path is a directory symbolic link
    static bool isDirectorySymlink(const QString& path);

    /// Get file identity (device + inode / file ID)
    static std::optional<FileIdentity> getFileIdentity(const QString& path);

    /// Check if a file/directory is readable
    static bool isReadable(const QString& path);

    /// Check if a directory is writable
    static bool isWritable(const QString& path);

    /// Generate a safe, deterministic name to avoid collision at destDir
    /// e.g., if "file.txt" exists, returns "file_1.txt", "file_2.txt", etc.
    static QString generateSafeName(const QString& destDir, const QString& fileName);

    /// Construct a path using QDir (never manual concatenation)
    static QString joinPath(const QString& base, const QString& relative);

    /// Get relative path of child with respect to parent
    static QString relativePath(const QString& parentPath, const QString& childPath);

    /// Check if two paths are on the same filesystem (same device)
    static bool isSameFileSystem(const QString& path1, const QString& path2);

    /// Validate that a move from source to dest is safe:
    /// - source exists
    /// - dest is not inside source
    /// - source is not inside protected root
    struct MoveValidation {
        bool valid = false;
        QString error;
    };
    static MoveValidation validateMove(const QString& sourcePath,
                                        const QString& destDir,
                                        const QString& protectedRoot);

    /// Get the file extension (including the dot)
    static QString fileExtension(const QString& path);

    /// Get the file stem (name without extension)
    static QString fileStem(const QString& path);
};

} // namespace dedupvault::core
