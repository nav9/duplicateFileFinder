#include "filescanner.h"
#include "core/filesystemutils.h"
#include "core/logger.h"

#include <QFileInfo>
#include <QDir>

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace dedupvault::engine {

namespace {

/// Cross-platform path -> QString without lossy narrow conversions.
QString fsToQString(const fs::path& p) {
#ifdef Q_OS_WIN
    return QString::fromWCharArray(p.c_str());
#else
    return QString::fromUtf8(p.c_str());
#endif
}

struct PendingDir {
    fs::path absolute;
    fs::path relative; // empty for the root itself
};

} // namespace

TraversalResult FileScanner::scanRoot(qint64 rootId,
                                      const QString& rootPath,
                                      const ScannerOptions& options,
                                      ScanControl& control,
                                      const ScanBatchSink& sink) {
    TraversalResult result;
    result.rootId = rootId;
    result.rootPath = rootPath;

    const fs::path rootFs = fs::path(rootPath.toStdString()).lexically_normal();

    std::error_code ec;
    if (!fs::exists(rootFs, ec) || !fs::is_directory(rootFs, ec)) {
        result.errorCount = 1;
        result.errors.append(QStringLiteral("Root is not a directory: %1").arg(rootPath));
        core::Logger::instance().error(result.errors.last());
        return result;
    }

    ScanBatch batch;
    batch.rootId = rootId;
    auto flush = [&]() {
        if (!batch.files.isEmpty() && sink) {
            sink(std::move(batch));
            batch = ScanBatch{};
            batch.rootId = rootId;
        }
    };

    std::vector<PendingDir> stack;
    stack.push_back({rootFs, fs::path()});

    while (!stack.empty()) {
        if (!control.checkpoint()) {
            result.cancelled = true;
            flush();
            return result;
        }

        const PendingDir current = stack.back();
        stack.pop_back();

        ++result.dirsVisited;

        fs::directory_iterator it(current.absolute, ec);
        if (ec) {
            ++result.errorCount;
            const QString msg = QStringLiteral("Cannot list %1: %2")
                                    .arg(fsToQString(current.absolute),
                                         QString::fromStdString(ec.message()));
            if (result.errors.size() < options.maxErrorSamples) {
                result.errors.append(msg);
            }
            core::Logger::instance().warning(msg);
            continue;
        }

        for (fs::directory_iterator end; it != end; it.increment(ec)) {
            if (ec) {
                ++result.errorCount;
                core::Logger::instance().warning(
                    QStringLiteral("Iteration error under %1: %2")
                        .arg(fsToQString(current.absolute), QString::fromStdString(ec.message())));
                ec.clear();
                continue;
            }

            if (!control.checkpoint()) {
                result.cancelled = true;
                flush();
                return result;
            }

            const fs::path& entryPath = it->path();

            std::error_code statusEc;
            const fs::file_status st = it->symlink_status(statusEc);
            if (statusEc) {
                ++result.errorCount;
                continue;
            }

            const bool isSymlink = fs::is_symlink(st);

            if (isSymlink) {
                // Explicit symlink policy: resolve the target only to decide
                // what it is, never to traverse it unless the user enabled
                // following directory symlinks. A symlink can therefore not
                // smuggle the walk past the protected root by default.
                std::error_code targetEc;
                const fs::file_status target = fs::status(entryPath, targetEc);
                if (!targetEc && fs::is_directory(target)) {
                    if (!options.followDirSymlinks) {
                        core::Logger::instance().debug(
                            QStringLiteral("Skipping directory symlink: %1")
                                .arg(fsToQString(entryPath)));
                        continue;
                    }
                    stack.push_back({entryPath,
                                     current.relative.empty()
                                         ? entryPath.filename()
                                         : current.relative / entryPath.filename()});
                    continue;
                }
                if (options.skipFileSymlinks) {
                    continue;
                }
                if (targetEc || !fs::is_regular_file(target)) continue;
            } else if (fs::is_directory(st)) {
                stack.push_back({entryPath,
                                 current.relative.empty() ? entryPath.filename()
                                                          : current.relative / entryPath.filename()});
                continue;
            }

            if (!isSymlink && !fs::is_regular_file(st)) {
                continue; // sockets, fifos, devices: not deduplicatable
            }

            const fs::path relPath = current.relative.empty()
                ? entryPath.filename()
                : current.relative / entryPath.filename();
            const fs::path relDir = relPath.parent_path();

            const QString absPath = fsToQString(entryPath);
            // Use QFileInfo for metadata so traversal and hashing compare
            // identical timestamp semantics (same API, same ms precision).
            const QFileInfo qfi(absPath);
            const qint64 size = qfi.size();
            if (size < 0) {
                ++result.errorCount;
                continue;
            }
            const qint64 mtime = qfi.lastModified().toMSecsSinceEpoch();

            if (size < options.minFileSize) continue;
            if (options.maxFileSize >= 0 && size > options.maxFileSize) continue;

            ScannedFile sf;
            sf.relPath = fsToQString(relPath);
            sf.relDir = fsToQString(relDir);
            sf.size = size;
            sf.mtimeMsec = mtime;

            if (const auto id = core::FileSystemUtils::getFileIdentity(absPath)) {
                sf.deviceId = id->deviceId;
                sf.inodeId = id->inodeOrFileId;
            }

            batch.files.append(std::move(sf));
            ++result.filesFound;
            result.bytesFound += size;

            if (batch.files.size() >= options.batchFileCount) {
                flush();
                if (!control.checkpoint()) {
                    result.cancelled = true;
                    return result;
                }
            }
        }
    }

    flush();
    core::Logger::instance().info(
        QStringLiteral("Traversal of %1 finished: %2 files, %3 dirs, %4 errors")
            .arg(rootPath)
            .arg(result.filesFound)
            .arg(result.dirsVisited)
            .arg(result.errorCount));
    return result;
}

} // namespace dedupvault::engine
