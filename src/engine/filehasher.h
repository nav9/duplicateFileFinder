#pragma once

#include "filescanner.h"
#include "core/databasemanager.h"

#include <QString>
#include <cstdint>

namespace dedupvault::engine {

/**
 * @brief Outcome of hashing one file.
 */
struct HashResult {
    qint64  fileId = 0;
    QString absPath;
    bool    ok = false;
    bool    changed = false;  // file was modified while being read
    QString sha256;
    QString error;
    qint64  bytesHashed = 0;
    // Populated when changed==true so the engine can persist fresh metadata.
    qint64   freshSize = 0;
    qint64   freshMtimeMsec = 0;
    quint64  freshDeviceId = 0;
    quint64  freshInodeId = 0;
};

struct HasherOptions {
    int  bufferSizeBytes = 1024 * 1024; // streaming chunk size (bounded memory)
    int  maxRetries = 2;
};

/**
 * @brief Streaming SHA-256 file hashing with change detection.
 *
 * Never loads a whole file into memory: data is read in fixed-size chunks.
 * File identity/size/mtime are captured before and after reading; a
 * mismatch marks the result as changed so the catalog invalidates the hash.
 */
class FileHasher {
public:
    FileHasher() = delete;

    static HashResult hashFile(const core::FileRow& file,
                               const HasherOptions& options,
                               ScanControl& control);
};

} // namespace dedupvault::engine
