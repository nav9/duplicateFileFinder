#include "safefileops.h"
#include "core/logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <exception>
#include <filesystem>

namespace fs = std::filesystem;

namespace dedupvault::operations {

namespace {

/// Lossless QString -> std::filesystem::path conversion.
fs::path toFs(const QString& s) {
#ifdef Q_OS_WIN
    return fs::path(reinterpret_cast<const wchar_t*>(s.utf16()));
#else
    return fs::path(s.toStdString());
#endif
}

QString fromFs(const fs::path& p) {
#ifdef Q_OS_WIN
    return QString::fromWCharArray(p.c_str());
#else
    return QString::fromUtf8(p.c_str());
#endif
}

constexpr const char* PARTIAL_SUFFIX = ".dvpartial";

QString sha256OfActionIdentity(const QString& expectedIdentity) {
    // identity format: "sha256=<hex>;size=<n>"
    if (expectedIdentity.startsWith(QLatin1String("sha256="))) {
        QString rest = expectedIdentity.mid(7);
        const int cut = rest.indexOf(QLatin1Char(';'));
        if (cut >= 0) rest = rest.left(cut);
        return rest;
    }
    return QString();
}

} // namespace

// ---------------------------------------------------------------------------

SafeFileOps::SafeFileOps(core::DatabaseManager& db, QObject* parent)
    : QObject(parent)
    , m_db(db)
{
}

core::FileSystemUtils::MoveValidation SafeFileOps::validate(
    const core::ActionRow& action, const QString& protectedRoot)
{
    const QString& source = action.sourcePath;
    const QString& dest = action.destPath;

    if (!QFileInfo::exists(source)) {
        return {false, tr("Source no longer exists: %1").arg(source)};
    }
    if (!protectedRoot.isEmpty() &&
        core::FileSystemUtils::isProtected(source, protectedRoot)) {
        return {false, tr("Refusing to move anything inside the protected "
                          "main folder: %1").arg(source)};
    }
    if (core::FileSystemUtils::isSamePath(source, dest)) {
        return {false, tr("Source and destination are identical: %1").arg(source)};
    }
    if (core::FileSystemUtils::isContainedIn(dest, source)) {
        return {false, tr("Destination lies inside the source: %1 -> %2")
                           .arg(source, dest)};
    }
    if (core::FileSystemUtils::isContainedIn(source, dest)) {
        return {false, tr("Source lies inside the destination: %1 -> %2")
                           .arg(source, dest)};
    }
    const QFileInfo destInfo(dest);
    const QString destDir = destInfo.absolutePath();

    // Validation must be side-effect free (dry-run relies on it): walk up to
    // the nearest existing ancestor and require it to be writable instead of
    // creating anything here. Creation happens only during a real move.
    QString existingAncestor = destDir;
    while (!existingAncestor.isEmpty() && !QFileInfo::exists(existingAncestor)) {
        const QString parent = QFileInfo(existingAncestor).absolutePath();
        if (parent == existingAncestor) break;
        existingAncestor = parent;
    }
    if (existingAncestor.isEmpty() ||
        !core::FileSystemUtils::isWritable(existingAncestor)) {
        return {false, tr("Destination folder is not writable: %1").arg(destDir)};
    }
    return {true, QString()};
}

bool SafeFileOps::verifyFileHash(const QString& path, const QString& expectedSha256,
                                 QString* error) {
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = in.errorString();
        return false;
    }
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    static constexpr int CHUNK = 1024 * 1024;
    QByteArray buf;
    buf.resize(CHUNK);
    while (!in.atEnd()) {
        const qint64 n = in.read(buf.data(), CHUNK);
        if (n < 0) {
            if (error) *error = in.errorString();
            return false;
        }
        if (n == 0) break;
        hasher.addData(QByteArray::fromRawData(buf.constData(),
                                               static_cast<qsizetype>(n)));
    }
    in.close();
    const QString actual = QString::fromLatin1(hasher.result().toHex());
    if (actual.compare(expectedSha256, Qt::CaseInsensitive) != 0) {
        if (error) {
            *error = tr("Content hash mismatch for %1 (expected %2, got %3)")
                         .arg(path, expectedSha256, actual);
        }
        return false;
    }
    return true;
}

bool SafeFileOps::dirStats(const QString& path, qint64* fileCount, qint64* totalSize,
                           QString* error) {
    *fileCount = 0;
    *totalSize = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        ++(*fileCount);
        *totalSize += fi.size();
        if (fi.size() < 0) {
            if (error) *error = tr("Unreadable entry: %1").arg(fi.absoluteFilePath());
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------

bool SafeFileOps::copyFileVerified(const QString& src, const QString& dst,
                                   bool verifyHash, QString* error) {
    const QString partial = dst + QString::fromLatin1(PARTIAL_SUFFIX);
    QFile::remove(partial);

    if (!QFile::copy(src, partial)) {
        if (error) *error = tr("Copy failed: %1 -> %2").arg(src, partial);
        return false;
    }

    const QFileInfo sInfo(src);
    const QFileInfo dInfo(partial);
    if (!dInfo.exists() || sInfo.size() != dInfo.size()) {
        if (error) *error = tr("Destination size mismatch after copy: %1").arg(partial);
        QFile::remove(partial);
        return false;
    }
    if (verifyHash) {
        // Verify by comparing source and destination content directly.
        QString err1;
        QString err2;
        auto hashOf = [](const QString& p, QString* err) -> QString {
            QFile in(p);
            if (!in.open(QIODevice::ReadOnly)) {
                if (err) *err = in.errorString();
                return QString();
            }
            QCryptographicHash h(QCryptographicHash::Sha256);
            static constexpr int CHUNK = 1024 * 1024;
            QByteArray buf;
            buf.resize(CHUNK);
            while (!in.atEnd()) {
                const qint64 n = in.read(buf.data(), CHUNK);
                if (n < 0) { if (err) *err = in.errorString(); return QString(); }
                if (n == 0) break;
                h.addData(QByteArray::fromRawData(buf.constData(),
                                                  static_cast<qsizetype>(n)));
            }
            return QString::fromLatin1(h.result().toHex());
        };
        const QString hSrc = hashOf(src, &err1);
        const QString hDst = hashOf(partial, &err2);
        if (hSrc.isEmpty() || hDst.isEmpty() || hSrc != hDst) {
            if (error) *error = tr("Copy verification failed for %1 (%2)")
                                    .arg(dst, err1.isEmpty() ? err2 : err1);
            QFile::remove(partial);
            return false;
        }
    }

    // Publish atomically: rename is only ever same-directory here.
    QFile::remove(dst);
    if (!QFile::rename(partial, dst)) {
        if (error) *error = tr("Could not publish verified copy to %1").arg(dst);
        QFile::remove(partial);
        return false;
    }
    return true;
}

bool SafeFileOps::copyTreeVerified(const QString& src, const QString& dst,
                                   QString* error) {
    std::error_code ec;
    fs::copy(toFs(src), toFs(dst),
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error) *error = tr("Directory copy failed: %1").arg(
            QString::fromStdString(ec.message()));
        return false;
    }

    qint64 srcFiles = 0;
    qint64 srcSize = 0;
    qint64 dstFiles = 0;
    qint64 dstSize = 0;
    QString err;
    if (!dirStats(src, &srcFiles, &srcSize, &err)) {
        if (error) *error = err;
        return false;
    }
    if (!dirStats(dst, &dstFiles, &dstSize, &err)) {
        if (error) *error = err;
        return false;
    }
    if (srcFiles != dstFiles || srcSize != dstSize) {
        if (error) {
            *error = tr("Directory verification failed (%1/%2 files, %3/%4 bytes)")
                         .arg(srcFiles).arg(dstFiles).arg(srcSize).arg(dstSize);
        }
        return false;
    }
    return true;
}

void SafeFileOps::removePartialCopy(const QString& destPath) {
    QFile::remove(destPath + QString::fromLatin1(PARTIAL_SUFFIX));
}

bool SafeFileOps::movePath(const QString& source, const QString& dest, MoveKind kind,
                           bool verifyHash, QString* error) {
    // 1) Fast path: atomic rename on the same filesystem.
    std::error_code ec;
    fs::rename(toFs(source), toFs(dest), ec);
    if (!ec) return true;

    // 2) Cross-filesystem (or recoverable) path: copy, verify, remove source.
    core::Logger::instance().debug(
        QStringLiteral("rename() failed (%1); falling back to verified copy")
            .arg(QString::fromStdString(ec.message())));

    if (kind == MoveKind::File) {
        if (!copyFileVerified(source, dest, verifyHash, error)) return false;
        if (!QFile::remove(source)) {
            if (error) {
                *error = tr("Destination verified at %1 but the source could not "
                            "be removed: %2").arg(dest, source);
            }
            return false;
        }
        return true;
    }

    if (!copyTreeVerified(source, dest, error)) return false;
    QDir srcDir(source);
    if (!srcDir.removeRecursively()) {
        if (error) {
            *error = tr("Destination verified at %1 but the source folder could "
                        "not be removed: %2").arg(dest, source);
        }
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------

MoveOutcome SafeFileOps::execute(const core::ActionRow& action, const Options& options) {
    const bool isDir = QFileInfo(action.sourcePath).isDir();
    return perform(action.actionId, action.sourcePath, action.destPath,
                   isDir ? MoveKind::Directory : MoveKind::File,
                   options, /*isUndo=*/false);
}

MoveOutcome SafeFileOps::undo(const core::ActionRow& action) {
    if (action.status != QLatin1String("done")) {
        return {false, false,
                tr("Only completed operations can be undone (action %1 is %2)")
                    .arg(action.actionId).arg(action.status),
                action.actionId, QString()};
    }
    // Restore from destination back to the original source location. The
    // protected root is intentionally empty here: restoring the original
    // location is the definition of undo and our planner never proposes
    // sources inside the protected root in the first place.
    Options opts;
    opts.dryRun = false;
    const bool isDir = QFileInfo(action.destPath).isDir();
    return perform(action.actionId, action.destPath, action.sourcePath,
                   isDir ? MoveKind::Directory : MoveKind::File,
                   opts, /*isUndo=*/true);
}

MoveOutcome SafeFileOps::perform(qint64 actionId, const QString& source,
                                 const QString& dest, MoveKind kind,
                                 const Options& options, bool isUndo) {
    MoveOutcome outcome;
    outcome.actionId = actionId;
    outcome.actualDest = dest;

    core::ActionRow row;
    row.actionId = actionId;
    row.sourcePath = source;
    row.destPath = dest;
    row.status = isUndo ? QStringLiteral("done") : QStringLiteral("planned");

    const auto check = validate(row, options.protectedRoot);
    if (!check.valid) {
        outcome.error = check.error;
        m_db.finalizeAction(actionId, QStringLiteral("failed"));
        m_db.setActionRecoveryInfo(actionId, outcome.error);
        core::Logger::instance().error(
            tr("Refused action %1: %2").arg(actionId).arg(outcome.error));
        emit actionFailed(actionId, outcome.error);
        return outcome;
    }

    if (options.dryRun) {
        outcome.ok = true;
        outcome.dryRun = true;
        return outcome;
    }

    // Collision-safe deterministic naming at execution time.
    QString finalDest = dest;
    if (QFileInfo::exists(finalDest)) {
        const QFileInfo destInfo(finalDest);
        const QString safeName =
            core::FileSystemUtils::generateSafeName(destInfo.absolutePath(),
                                                    destInfo.fileName());
        finalDest = QDir(destInfo.absolutePath()).filePath(safeName);
        core::Logger::instance().info(
            tr("Destination exists; using deterministic name %1").arg(finalDest));
    }
    outcome.actualDest = finalDest;

    // High-assurance byte-for-byte verification before destroying a source.
    if (options.verifyHash && kind == MoveKind::File) {
        // The caller-provided expected hash lives in the journal row.
        if (actionId > 0) {
            const auto journal = m_db.actionById(actionId);
            if (journal) {
                const QString expected = sha256OfActionIdentity(journal->expectedIdentity);
                if (!expected.isEmpty()) {
                    QString err;
                    if (!verifyFileHash(source, expected, &err)) {
                        outcome.error = tr("Source content changed since planning: %1")
                                            .arg(err);
                        m_db.finalizeAction(actionId, QStringLiteral("failed"));
                        m_db.setActionRecoveryInfo(actionId, outcome.error);
                        emit actionFailed(actionId, outcome.error);
                        return outcome;
                    }
                }
            }
        }
    }

    // Journal the intent before touching the filesystem.
    m_db.finalizeAction(actionId, QStringLiteral("planned"));

    // Only a real move creates missing destination directories.
    const QString destDir = QFileInfo(finalDest).absolutePath();
    if (!QFileInfo::exists(destDir) && !QDir().mkpath(destDir)) {
        outcome.error = tr("Cannot create destination folder: %1").arg(destDir);
        m_db.finalizeAction(actionId, QStringLiteral("failed"));
        m_db.setActionRecoveryInfo(actionId, outcome.error);
        emit actionFailed(actionId, outcome.error);
        return outcome;
    }

    QString err;
    const bool moved = movePath(source, finalDest, kind, options.verifyHash, &err);
    if (!moved) {
        removePartialCopy(finalDest); // clean partial state
        outcome.error = err;
        m_db.finalizeAction(actionId, QStringLiteral("failed"));
        m_db.setActionRecoveryInfo(actionId, err);
        core::Logger::instance().error(
            tr("Action %1 failed: %2").arg(actionId).arg(err));
        emit actionFailed(actionId, err);
        return outcome;
    }

    if (QFileInfo::exists(finalDest) || !QFileInfo::exists(source)) {
        const QString newStatus =
            isUndo ? QStringLiteral("undone") : QStringLiteral("done");
        m_db.finalizeAction(actionId, newStatus);
        m_db.setActionRecoveryInfo(
            actionId,
            QStringLiteral("resolved path: %1").arg(finalDest));
        outcome.ok = true;
        core::Logger::instance().info(
            tr("Action %1 %2: %3 -> %4")
                .arg(actionId).arg(newStatus).arg(source, finalDest));
        emit actionCompleted(actionId, finalDest);
    } else {
        outcome.error = tr("Verification after move failed for %1").arg(source);
        m_db.finalizeAction(actionId, QStringLiteral("failed"));
        m_db.setActionRecoveryInfo(actionId, outcome.error);
        emit actionFailed(actionId, outcome.error);
    }
    return outcome;
}

// ---------------------------------------------------------------------------

int SafeFileOps::reconcile() {
    int corrected = 0;

    // 1) Rows still 'planned': did the move happen anyway?
    for (const core::ActionRow& action : m_db.actions(QStringLiteral("planned"))) {
        const bool sourceExists = QFileInfo::exists(action.sourcePath);
        const bool destExists = QFileInfo::exists(action.destPath);
        const QString partial = action.destPath +
                                QString::fromLatin1(PARTIAL_SUFFIX);

        if (!sourceExists && destExists) {
            // Crash landed between the move and the commit: adopt it.
            m_db.finalizeAction(action.actionId, QStringLiteral("done"));
            ++corrected;
            core::Logger::instance().info(
                tr("Reconciled interrupted move of %1 as completed.")
                    .arg(action.sourcePath));
        } else if (sourceExists) {
            // Clean up any half-written copy so a retry starts clean.
            if (QFileInfo::exists(partial)) {
                QFile::remove(partial);
                core::Logger::instance().info(
                    tr("Removed partial copy: %1").arg(partial));
            }
            if (destExists && !QFileInfo::exists(partial)) {
                // A complete destination but an intact source: let a retry
                // resolve the collision deterministically.
                m_db.setActionRecoveryInfo(
                    action.actionId,
                    tr("partial copy detected and cleared; retry safe"));
            }
        } else {
            m_db.finalizeAction(action.actionId, QStringLiteral("failed"));
            m_db.setActionRecoveryInfo(
                action.actionId, tr("neither source nor destination could be found"));
            ++corrected;
        }
    }

    // 2) Rows 'done' whose destination vanished (changed under our feet).
    for (const core::ActionRow& action : m_db.actions(QStringLiteral("done"))) {
        if (!QFileInfo::exists(action.destPath)) {
            m_db.finalizeAction(action.actionId, QStringLiteral("failed"));
            m_db.setActionRecoveryInfo(
                action.actionId,
                tr("destination disappeared after the move; undo impossible"));
            ++corrected;
            core::Logger::instance().warning(
                tr("Journal entry %1 marked failed: destination %2 is missing.")
                    .arg(action.actionId).arg(action.destPath));
        }
    }

    if (corrected > 0) {
        core::Logger::instance().info(
            tr("Reconciliation corrected %1 journal entr(y/ies).").arg(corrected));
    }
    return corrected;
}

} // namespace dedupvault::operations