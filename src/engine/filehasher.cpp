#include "filehasher.h"
#include "core/filesystemutils.h"
#include "core/logger.h"

#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>

namespace dedupvault::engine {

HashResult FileHasher::hashFile(const core::FileRow& file,
                                const HasherOptions& options,
                                ScanControl& control) {
    HashResult result;
    result.fileId = file.fileId;
    result.absPath = file.absPath;

    // Expectations start from the catalog row and are refreshed whenever the
    // file is observed to have changed, so a retry can succeed.
    qint64 expectSize = file.size;
    qint64 expectMtime = file.mtimeMsec;

    for (int attempt = 0; attempt <= options.maxRetries; ++attempt) {
        if (!control.checkpoint()) {
            result.error = QStringLiteral("cancelled");
            return result;
        }

        const auto before = core::FileSystemUtils::getFileIdentity(file.absPath);
        const QFileInfo beforeInfo(file.absPath);
        if (!beforeInfo.exists()) {
            result.error = QStringLiteral("File no longer exists");
            return result;
        }
        if (!beforeInfo.isReadable()) {
            result.error = QStringLiteral("File is not readable");
            return result;
        }

        QFile in(file.absPath);
        if (!in.open(QIODevice::ReadOnly)) {
            result.error = in.errorString();
            return result;
        }

        QCryptographicHash hasher(QCryptographicHash::Sha256);
        QByteArray buffer;
        buffer.resize(options.bufferSizeBytes);

        qint64 total = 0;
        bool readError = false;
        while (!in.atEnd()) {
            if (!control.checkpoint()) {
                in.close();
                result.error = QStringLiteral("cancelled");
                return result;
            }
            const qint64 n = in.read(buffer.data(), options.bufferSizeBytes);
            if (n < 0) {
                readError = true;
                result.error = in.errorString();
                break;
            }
            if (n == 0) break;
            // addData(QByteArrayView) only exists from Qt 6.3 on, while the
            // (const char*, qsizetype) overload is deprecated there; a raw
            // QByteArray view is valid on every supported Qt 6 release.
            hasher.addData(QByteArray::fromRawData(
                buffer.constData(), static_cast<qsizetype>(n)));
            total += n;
        }
        in.close();

        if (readError) {
            result.error = readError ? result.error : QStringLiteral("read error");
            return result;
        }

        // Detect modification between stat time and EOF.
        const auto after = core::FileSystemUtils::getFileIdentity(file.absPath);
        const QFileInfo afterInfo(file.absPath);
        const bool identityChanged = before.has_value() && after.has_value() &&
                                     (*before != *after);
        const bool metaChanged = afterInfo.size() != expectSize ||
                                 afterInfo.lastModified().toMSecsSinceEpoch() != expectMtime;
        const bool truncated = afterInfo.size() != total;
        const bool changed = identityChanged || metaChanged || truncated;

        if (changed) {
            core::Logger::instance().debug(
                QStringLiteral("File changed during hashing (attempt %1): %2")
                    .arg(attempt + 1).arg(file.absPath));
            result.changed = true;
            result.bytesHashed = total;
            // Adopt the fresh metadata as the expectation for a retry; the
            // engine persists it once the worker reports back.
            expectSize = afterInfo.size();
            expectMtime = afterInfo.lastModified().toMSecsSinceEpoch();
            result.freshSize = expectSize;
            result.freshMtimeMsec = expectMtime;
            if (after.has_value()) {
                result.freshDeviceId = after->deviceId;
                result.freshInodeId = after->inodeOrFileId;
            }
            continue;
        }

        result.ok = true;
        result.changed = false;
        result.bytesHashed = total;
        result.sha256 = QString::fromLatin1(hasher.result().toHex());
        return result;
    }

    if (result.error.isEmpty()) {
        result.error = QStringLiteral("File kept changing while hashing");
    }
    return result;
}

} // namespace dedupvault::engine
