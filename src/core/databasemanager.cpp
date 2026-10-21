#include "databasemanager.h"
#include "logger.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QUuid>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace dedupvault::core {

static const int SCHEMA_VERSION = 1;

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent)
    , m_connectionName(QStringLiteral("dedupvault_%1")
                           .arg(QUuid::createUuid().toString(QUuid::Id128)))
{
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open(const QString& dbPath) {
    m_dbPath = dbPath;

    const QFileInfo info(dbPath);
    QDir().mkpath(info.absolutePath());

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        Logger::instance().error(QStringLiteral("Failed to open database: %1").arg(m_lastError));
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA temp_store = MEMORY"));

    return migrate();
}

void DatabaseManager::close() {
    if (m_db.isOpen()) {
        {
            QSqlQuery q(m_db);
            q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
        }
        m_db.close();
    }
    // Drop our reference before removing the connection, otherwise Qt warns
    // that the connection is still in use.
    const QString name = m_connectionName;
    m_db = QSqlDatabase();
    if (QSqlDatabase::contains(name)) {
        QSqlDatabase::removeDatabase(name);
    }
    m_connectionName = QStringLiteral("dedupvault_%1")
                           .arg(QUuid::createUuid().toString(QUuid::Id128));
}

bool DatabaseManager::isOpen() const {
    return m_db.isOpen();
}

QString DatabaseManager::lastError() const {
    return m_lastError;
}

QString DatabaseManager::databasePath() const {
    return m_dbPath;
}

bool DatabaseManager::exec(const QString& sql) {
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        Logger::instance().error(QStringLiteral("SQL error: %1 -- %2")
                                     .arg(m_lastError, sql.left(200)));
        return false;
    }
    return true;
}

// --- Schema ---------------------------------------------------------------

int DatabaseManager::schemaVersion() const {
    if (!m_db.isOpen()) return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("PRAGMA user_version"))) return 0;
    if (q.next()) return q.value(0).toInt();
    return 0;
}

bool DatabaseManager::migrate() {
    const int current = schemaVersion();

    if (current >= SCHEMA_VERSION && current > 0) {
        return true;
    }

    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    bool ok = true;

    if (current < 1) {
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS root ("
            "  root_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  path TEXT NOT NULL UNIQUE,"
            "  role TEXT NOT NULL,"
            "  enabled INTEGER NOT NULL DEFAULT 1,"
            "  added_at TEXT)"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS folder ("
            "  folder_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  root_id INTEGER NOT NULL REFERENCES root(root_id) ON DELETE CASCADE,"
            "  parent_id INTEGER REFERENCES folder(folder_id) ON DELETE CASCADE,"
            "  rel_path TEXT NOT NULL,"
            "  direct_file_count INTEGER NOT NULL DEFAULT 0,"
            "  recursive_file_count INTEGER NOT NULL DEFAULT 0,"
            "  total_size INTEGER NOT NULL DEFAULT 0,"
            "  subtree_signature TEXT,"
            "  classification TEXT,"
            "  UNIQUE(root_id, rel_path))"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS file ("
            "  file_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  root_id INTEGER NOT NULL REFERENCES root(root_id) ON DELETE CASCADE,"
            "  folder_id INTEGER REFERENCES folder(folder_id) ON DELETE CASCADE,"
            "  rel_path TEXT NOT NULL,"
            "  size INTEGER NOT NULL DEFAULT 0,"
            "  mtime_msec INTEGER NOT NULL DEFAULT 0,"
            "  device_id INTEGER NOT NULL DEFAULT 0,"
            "  inode_id INTEGER NOT NULL DEFAULT 0,"
            "  sha256 TEXT,"
            "  hash_algo TEXT NOT NULL DEFAULT 'sha256',"
            "  scan_status INTEGER NOT NULL DEFAULT 0,"
            "  error_message TEXT,"
            "  last_seen_generation INTEGER NOT NULL DEFAULT 0,"
            "  UNIQUE(root_id, rel_path))"));

        ok = ok && exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_file_size ON file(size)"));
        ok = ok && exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_file_hash ON file(sha256)"));
        ok = ok && exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_file_size_hash ON file(size, sha256)"));
        ok = ok && exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_file_root ON file(root_id)"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS job ("
            "  job_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  status TEXT NOT NULL,"
            "  phase TEXT NOT NULL DEFAULT 'idle',"
            "  started_at TEXT,"
            "  finished_at TEXT)"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS duplicate_group ("
            "  group_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  file_size INTEGER NOT NULL,"
            "  content_hash TEXT NOT NULL,"
            "  keeper_file_id INTEGER,"
            "  created_at TEXT,"
            "  UNIQUE(content_hash, file_size))"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS duplicate_member ("
            "  group_id INTEGER NOT NULL REFERENCES duplicate_group(group_id) ON DELETE CASCADE,"
            "  file_id INTEGER NOT NULL REFERENCES file(file_id) ON DELETE CASCADE,"
            "  is_keeper INTEGER NOT NULL DEFAULT 0,"
            "  PRIMARY KEY(group_id, file_id))"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS folder_match ("
            "  match_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  folder_a INTEGER NOT NULL REFERENCES folder(folder_id) ON DELETE CASCADE,"
            "  folder_b INTEGER NOT NULL REFERENCES folder(folder_id) ON DELETE CASCADE,"
            "  match_type TEXT NOT NULL,"
            "  overlap_score REAL NOT NULL DEFAULT 0,"
            "  review_status TEXT NOT NULL DEFAULT 'pending',"
            "  notes TEXT)"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS task ("
            "  task_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  job_id INTEGER NOT NULL REFERENCES job(job_id) ON DELETE CASCADE,"
            "  task_type TEXT NOT NULL,"
            "  target_id INTEGER,"
            "  status TEXT NOT NULL DEFAULT 'pending',"
            "  attempts INTEGER NOT NULL DEFAULT 0,"
            "  error TEXT,"
            "  updated_at TEXT)"));

        ok = ok && exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_task_job_status ON task(job_id, status)"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS action_log ("
            "  action_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  job_id INTEGER,"
            "  op_type TEXT NOT NULL,"
            "  source_path TEXT NOT NULL,"
            "  dest_path TEXT NOT NULL,"
            "  expected_identity TEXT,"
            "  status TEXT NOT NULL DEFAULT 'planned',"
            "  created_at TEXT,"
            "  completed_at TEXT,"
            "  recovery_info TEXT)"));

        // Reserved for the optional perceptual-matching module. Kept separate
        // from exact-content duplicate groups by design.
        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS perceptual_group ("
            "  perceptual_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  algorithm TEXT NOT NULL,"
            "  algo_version INTEGER NOT NULL DEFAULT 1,"
            "  center_hash TEXT,"
            "  threshold INTEGER NOT NULL DEFAULT 8,"
            "  review_status TEXT NOT NULL DEFAULT 'pending',"
            "  created_at TEXT)"));

        ok = ok && exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS perceptual_member ("
            "  perceptual_id INTEGER NOT NULL REFERENCES perceptual_group(perceptual_id) ON DELETE CASCADE,"
            "  file_id INTEGER NOT NULL REFERENCES file(file_id) ON DELETE CASCADE,"
            "  phash TEXT,"
            "  distance INTEGER,"
            "  PRIMARY KEY(perceptual_id, file_id))"));
    }

    if (ok) {
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("PRAGMA user_version = %1").arg(SCHEMA_VERSION));
        ok = m_db.commit();
        if (!ok) m_lastError = m_db.lastError().text();
    } else {
        m_db.rollback();
    }

    if (ok) {
        Logger::instance().info(QStringLiteral("Database ready at %1 (schema v%2)")
                                    .arg(m_dbPath).arg(SCHEMA_VERSION));
    }
    return ok;
}

// --- Roots ----------------------------------------------------------------

QString DatabaseManager::roleToString(RootRole role) {
    switch (role) {
        case RootRole::Main:        return QStringLiteral("main");
        case RootRole::Candidate:   return QStringLiteral("candidate");
        case RootRole::Destination: return QStringLiteral("destination");
    }
    return QStringLiteral("candidate");
}

RootRole DatabaseManager::roleFromString(const QString& role) {
    if (role == QLatin1String("main")) return RootRole::Main;
    if (role == QLatin1String("destination")) return RootRole::Destination;
    return RootRole::Candidate;
}

qint64 DatabaseManager::upsertRoot(const QString& path, RootRole role, bool enabled) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO root(path, role, enabled, added_at) VALUES(?,?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET role=excluded.role, enabled=excluded.enabled"));
    q.addBindValue(path);
    q.addBindValue(roleToString(role));
    q.addBindValue(enabled ? 1 : 0);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    if (q.lastInsertId().isValid()) return q.lastInsertId().toLongLong();

    QSqlQuery sel(m_db);
    sel.prepare(QStringLiteral("SELECT root_id FROM root WHERE path=?"));
    sel.addBindValue(path);
    if (sel.exec() && sel.next()) return sel.value(0).toLongLong();
    return -1;
}

bool DatabaseManager::removeRoot(qint64 rootId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM root WHERE root_id=?"));
    q.addBindValue(rootId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVector<RootRecord> DatabaseManager::roots(bool enabledOnly) const {
    QVector<RootRecord> out;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT root_id, path, role, enabled FROM root");
    if (enabledOnly) sql += QStringLiteral(" WHERE enabled=1");
    sql += QStringLiteral(" ORDER BY root_id");
    q.prepare(sql);
    if (!q.exec()) return out;
    while (q.next()) {
        RootRecord r;
        r.id      = q.value(0).toLongLong();
        r.path    = q.value(1).toString();
        r.role    = roleFromString(q.value(2).toString());
        r.enabled = q.value(3).toInt() != 0;
        out.append(r);
    }
    return out;
}

std::optional<RootRecord> DatabaseManager::rootById(qint64 rootId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT root_id, path, role, enabled FROM root WHERE root_id=?"));
    q.addBindValue(rootId);
    if (q.exec() && q.next()) {
        RootRecord r;
        r.id      = q.value(0).toLongLong();
        r.path    = q.value(1).toString();
        r.role    = roleFromString(q.value(2).toString());
        r.enabled = q.value(3).toInt() != 0;
        return r;
    }
    return std::nullopt;
}

// --- Folders --------------------------------------------------------------

qint64 DatabaseManager::upsertFolder(qint64 rootId, qint64 parentId, const QString& relPath) {
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
            "SELECT folder_id FROM folder WHERE root_id=? AND rel_path=?"));
        q.addBindValue(rootId);
        q.addBindValue(relPath);
        if (q.exec() && q.next()) return q.value(0).toLongLong();
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO folder(root_id, parent_id, rel_path) VALUES(?,?,?)"));
    q.addBindValue(rootId);
    if (parentId > 0) q.addBindValue(parentId); else q.addBindValue(QVariant());
    q.addBindValue(relPath);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::updateFolderStats(qint64 folderId, int directFiles, qint64 recursiveFiles,
                                        qint64 totalSize, const QString& signature,
                                        const QString& classification) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE folder SET direct_file_count=?, recursive_file_count=?, total_size=?, "
        "subtree_signature=?, classification=? WHERE folder_id=?"));
    q.addBindValue(directFiles);
    q.addBindValue(recursiveFiles);
    q.addBindValue(totalSize);
    q.addBindValue(signature);
    q.addBindValue(classification);
    q.addBindValue(folderId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

std::optional<FolderRow> DatabaseManager::folderById(qint64 folderId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT f.folder_id, f.root_id, f.parent_id, f.rel_path, r.path, "
        "f.direct_file_count, f.recursive_file_count, f.total_size, "
        "f.subtree_signature, f.classification "
        "FROM folder f JOIN root r ON r.root_id=f.root_id WHERE f.folder_id=?"));
    q.addBindValue(folderId);
    if (q.exec() && q.next()) {
        FolderRow f;
        f.folderId = q.value(0).toLongLong();
        f.rootId = q.value(1).toLongLong();
        f.parentId = q.value(2).toLongLong();
        f.relPath = q.value(3).toString();
        f.rootPath = q.value(4).toString();
        f.directFileCount = q.value(5).toInt();
        f.recursiveFileCount = q.value(6).toLongLong();
        f.totalSize = q.value(7).toLongLong();
        f.subtreeSignature = q.value(8).toString();
        f.classification = q.value(9).toString();
        f.absPath = f.relPath.isEmpty()
            ? f.rootPath
            : QDir(f.rootPath).filePath(f.relPath);
        return f;
    }
    return std::nullopt;
}

QVector<FolderRow> DatabaseManager::foldersByRoot(qint64 rootId) const {
    QVector<FolderRow> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT f.folder_id, f.root_id, f.parent_id, f.rel_path, r.path, "
        "f.direct_file_count, f.recursive_file_count, f.total_size, "
        "f.subtree_signature, f.classification "
        "FROM folder f JOIN root r ON r.root_id=f.root_id "
        "WHERE f.root_id=? ORDER BY f.rel_path"));
    q.addBindValue(rootId);
    if (!q.exec()) return out;
    while (q.next()) {
        FolderRow f;
        f.folderId = q.value(0).toLongLong();
        f.rootId = q.value(1).toLongLong();
        f.parentId = q.value(2).toLongLong();
        f.relPath = q.value(3).toString();
        f.rootPath = q.value(4).toString();
        f.directFileCount = q.value(5).toInt();
        f.recursiveFileCount = q.value(6).toLongLong();
        f.totalSize = q.value(7).toLongLong();
        f.subtreeSignature = q.value(8).toString();
        f.classification = q.value(9).toString();
        f.absPath = f.relPath.isEmpty()
            ? f.rootPath
            : QDir(f.rootPath).filePath(f.relPath);
        out.append(f);
    }
    return out;
}

// --- Files ----------------------------------------------------------------

bool DatabaseManager::upsertFileMetadata(const QVector<FileRecord>& batch) {
    if (batch.isEmpty()) return true;
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    bool ok = true;
    QSqlQuery q(m_db);
    // 8 placeholders: 7 value columns + generation (also used for the
    // upsert-excluded copy so unchanged rows keep their valid hash).
    q.prepare(QStringLiteral(
        "INSERT INTO file(root_id, folder_id, rel_path, size, mtime_msec, device_id, "
        "inode_id, scan_status, error_message, last_seen_generation) "
        "VALUES(?,?,?,?,?,?,?,1,NULL,?) "
        "ON CONFLICT(root_id, rel_path) DO UPDATE SET "
        "  scan_status=CASE "
        "    WHEN file.size<>excluded.size OR file.mtime_msec<>excluded.mtime_msec "
        "      THEN 1 ELSE file.scan_status END, "
        "  sha256=CASE "
        "    WHEN file.size<>excluded.size OR file.mtime_msec<>excluded.mtime_msec "
        "      THEN NULL ELSE file.sha256 END, "
        "  size=excluded.size, mtime_msec=excluded.mtime_msec, "
        "  device_id=excluded.device_id, inode_id=excluded.inode_id, "
        "  last_seen_generation=excluded.last_seen_generation"));

    for (const FileRecord& rec : batch) {
        q.bindValue(0, rec.rootId);
        q.bindValue(1, rec.folderId > 0 ? QVariant(rec.folderId) : QVariant());
        q.bindValue(2, rec.relPath);
        q.bindValue(3, rec.size);
        q.bindValue(4, rec.mtimeMsec);
        q.bindValue(5, QVariant::fromValue(rec.deviceId));
        q.bindValue(6, QVariant::fromValue(rec.inodeId));
        q.bindValue(7, rec.generation);
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            Logger::instance().error(QStringLiteral("file upsert failed: %1").arg(m_lastError));
            ok = false;
            break;
        }
    }

    if (ok) {
        if (!m_db.commit()) {
            m_lastError = m_db.lastError().text();
            ok = false;
        }
    } else {
        m_db.rollback();
    }
    return ok;
}

bool DatabaseManager::updateFileHash(qint64 fileId, const QString& sha256) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE file SET sha256=?, hash_algo='sha256', scan_status=2, error_message=NULL "
        "WHERE file_id=?"));
    q.addBindValue(sha256);
    q.addBindValue(fileId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::markFileError(qint64 fileId, const QString& error) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE file SET scan_status=3, error_message=? WHERE file_id=?"));
    q.addBindValue(error);
    q.addBindValue(fileId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::resetFileAfterChange(qint64 fileId, qint64 size, qint64 mtimeMsec,
                                           quint64 deviceId, quint64 inodeId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE file SET size=?, mtime_msec=?, device_id=?, inode_id=?, "
        "sha256=NULL, scan_status=1, error_message=NULL WHERE file_id=?"));
    q.addBindValue(size);
    q.addBindValue(mtimeMsec);
    q.addBindValue(QVariant::fromValue(deviceId));
    q.addBindValue(QVariant::fromValue(inodeId));
    q.addBindValue(fileId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::markFilesMissing(qint64 rootId, const QVector<QString>& relPaths) {
    if (relPaths.isEmpty()) return true;
    if (!m_db.transaction()) return false;
    bool ok = true;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM file WHERE root_id=? AND rel_path=?"));
    for (const QString& rel : relPaths) {
        q.bindValue(0, rootId);
        q.bindValue(1, rel);
        if (!q.exec()) { ok = false; break; }
    }
    if (ok) {
        if (!m_db.commit()) ok = false;
    } else {
        m_db.rollback();
    }
    return ok;
}

int DatabaseManager::markStaleFiles(qint64 rootId, qint64 generation) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "DELETE FROM file WHERE root_id=? AND last_seen_generation<>?"));
    q.addBindValue(rootId);
    q.addBindValue(generation);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

int DatabaseManager::removeFilesByAbsPath(const QStringList& absPaths) {
    if (absPaths.isEmpty()) return 0;

    const QVector<RootRecord> allRoots = roots(false);
    if (!m_db.transaction()) return 0;

    int removed = 0;
    bool ok = true;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM file WHERE root_id=? AND rel_path=?"));

    for (const QString& abs : absPaths) {
        // Longest matching root path wins (roots never overlap: validated).
        const RootRecord* best = nullptr;
        for (const RootRecord& r : allRoots) {
            if (abs == r.path || abs.startsWith(r.path + QDir::separator())) {
                if (!best || r.path.size() > best->path.size()) best = &r;
            }
        }
        if (!best) continue;

        QString rel = abs.mid(best->path.size());
        const QChar sep = QDir::separator();
        while (!rel.isEmpty() && (rel.front() == QLatin1Char('/') || rel.front() == sep)) {
            rel.remove(0, 1);
        }
        q.bindValue(0, best->id);
        q.bindValue(1, rel);
        if (!q.exec()) {
            m_lastError = q.lastError().text();
            ok = false;
            break;
        }
        removed += q.numRowsAffected();
    }

    if (ok) {
        if (!m_db.commit()) return removed;
    } else {
        m_db.rollback();
    }
    return removed;
}

QVector<FileRow> DatabaseManager::filesNeedingHash(int limit, bool sharedSizeOnly) const {
    QVector<FileRow> out;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT f.file_id, f.root_id, r.path, r.role, f.rel_path, f.size, "
        "f.mtime_msec, f.sha256, f.scan_status, f.error_message "
        "FROM file f JOIN root r ON r.root_id=f.root_id "
        "WHERE f.sha256 IS NULL AND f.scan_status IN (0,1) "
        "  AND f.size > 0");
    if (sharedSizeOnly) {
        sql += QStringLiteral(
            "  AND f.size IN (SELECT size FROM file WHERE size>0 GROUP BY size "
            "                  HAVING COUNT(*)>1)");
    }
    sql += QStringLiteral(" ORDER BY f.size DESC LIMIT ?");
    q.prepare(sql);
    q.addBindValue(limit);
    if (!q.exec()) return out;
    while (q.next()) {
        FileRow f;
        f.fileId = q.value(0).toLongLong();
        f.rootId = q.value(1).toLongLong();
        f.rootPath = q.value(2).toString();
        f.rootRole = q.value(3).toString();
        f.relPath = q.value(4).toString();
        f.size = q.value(5).toLongLong();
        f.mtimeMsec = q.value(6).toLongLong();
        f.sha256 = q.value(7).toString();
        f.scanStatus = q.value(8).toInt();
        f.errorMessage = q.value(9).toString();
        f.absPath = QDir(f.rootPath).filePath(f.relPath);
        out.append(f);
    }
    return out;
}

QVector<FileRow> DatabaseManager::hashedFilesWithSharedSize() const {
    QVector<FileRow> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT f.file_id, f.root_id, r.path, r.role, f.rel_path, f.size, "
        "f.mtime_msec, f.sha256, f.scan_status, f.error_message "
        "FROM file f JOIN root r ON r.root_id=f.root_id "
        "WHERE f.sha256 IS NOT NULL "
        "  AND f.size IN (SELECT size FROM file GROUP BY size HAVING COUNT(*)>1) "
        "ORDER BY f.size, f.sha256, f.rel_path"));
    if (!q.exec()) return out;
    while (q.next()) {
        FileRow f;
        f.fileId = q.value(0).toLongLong();
        f.rootId = q.value(1).toLongLong();
        f.rootPath = q.value(2).toString();
        f.rootRole = q.value(3).toString();
        f.relPath = q.value(4).toString();
        f.size = q.value(5).toLongLong();
        f.mtimeMsec = q.value(6).toLongLong();
        f.sha256 = q.value(7).toString();
        f.scanStatus = q.value(8).toInt();
        f.errorMessage = q.value(9).toString();
        f.absPath = QDir(f.rootPath).filePath(f.relPath);
        out.append(f);
    }
    return out;
}

// --- Duplicate groups -----------------------------------------------------

bool DatabaseManager::clearDuplicateGroups() {
    if (!exec(QStringLiteral("DELETE FROM duplicate_member"))) return false;
    if (!exec(QStringLiteral("DELETE FROM duplicate_group"))) return false;
    return true;
}

int DatabaseManager::rebuildDuplicateGroups(const QHash<qint64, bool>& keeperFlags) {
    clearDuplicateGroups();

    QVector<QPair<qint64, QString>> keys; // (size, hash)
    {
        QSqlQuery q(m_db);
        if (!q.exec(QStringLiteral(
                "SELECT size, sha256 FROM file WHERE sha256 IS NOT NULL "
                "GROUP BY size, sha256 HAVING COUNT(*) > 1"))) {
            m_lastError = q.lastError().text();
            return 0;
        }
        while (q.next()) {
            keys.append({q.value(0).toLongLong(), q.value(1).toString()});
        }
    }

    if (keys.isEmpty()) return 0;
    if (!m_db.transaction()) return 0;

    int groupCount = 0;
    const QString createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);

    for (const auto& key : keys) {
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT INTO duplicate_group(file_size, content_hash, keeper_file_id, created_at) "
            "VALUES(?,?,NULL,?)"));
        ins.addBindValue(key.first);
        ins.addBindValue(key.second);
        ins.addBindValue(createdAt);
        if (!ins.exec()) {
            m_lastError = ins.lastError().text();
            m_db.rollback();
            return 0;
        }
        const qint64 groupId = ins.lastInsertId().toLongLong();

        QSqlQuery sel(m_db);
        sel.prepare(QStringLiteral(
            "SELECT file_id FROM file WHERE size=? AND sha256=? ORDER BY rel_path"));
        sel.addBindValue(key.first);
        sel.addBindValue(key.second);
        if (!sel.exec()) {
            m_lastError = sel.lastError().text();
            m_db.rollback();
            return 0;
        }

        qint64 keeperId = -1;
        QVector<qint64> memberIds;
        while (sel.next()) {
            const qint64 fid = sel.value(0).toLongLong();
            memberIds.append(fid);
            if (keeperId < 0 && keeperFlags.value(fid, false)) keeperId = fid;
        }
        if (keeperId < 0 && !memberIds.isEmpty()) keeperId = memberIds.first();

        {
            QSqlQuery upd(m_db);
            upd.prepare(QStringLiteral(
                "UPDATE duplicate_group SET keeper_file_id=? WHERE group_id=?"));
            upd.addBindValue(keeperId);
            upd.addBindValue(groupId);
            upd.exec();
        }

        QSqlQuery mem(m_db);
        mem.prepare(QStringLiteral(
            "INSERT INTO duplicate_member(group_id, file_id, is_keeper) VALUES(?,?,?)"));
        for (const qint64 fid : memberIds) {
            mem.bindValue(0, groupId);
            mem.bindValue(1, fid);
            mem.bindValue(2, fid == keeperId ? 1 : 0);
            if (!mem.exec()) {
                m_lastError = mem.lastError().text();
                m_db.rollback();
                return 0;
            }
        }
        ++groupCount;
    }

    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        return 0;
    }
    return groupCount;
}

QVector<DuplicateGroupRow> DatabaseManager::duplicateGroups() const {
    QVector<DuplicateGroupRow> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT g.group_id, g.file_size, g.content_hash, g.keeper_file_id, "
            "COUNT(m.file_id) AS members "
            "FROM duplicate_group g JOIN duplicate_member m ON m.group_id=g.group_id "
            "GROUP BY g.group_id ORDER BY g.file_size DESC"))) {
        return out;
    }
    while (q.next()) {
        DuplicateGroupRow g;
        g.groupId = q.value(0).toLongLong();
        g.size = q.value(1).toLongLong();
        g.hash = q.value(2).toString();
        g.keeperFileId = q.value(3).toLongLong();
        g.memberCount = q.value(4).toInt();
        g.wastedBytes = g.size * (g.memberCount - 1);
        out.append(g);
    }

    QSqlQuery mq(m_db);
    mq.prepare(QStringLiteral(
        "SELECT f.file_id, f.root_id, r.path, r.role, f.rel_path, f.size, "
        "f.mtime_msec, f.sha256, f.scan_status, f.error_message "
        "FROM duplicate_member m JOIN file f ON f.file_id=m.file_id "
        "JOIN root r ON r.root_id=f.root_id "
        "WHERE m.group_id=? ORDER BY f.rel_path"));
    for (auto& g : out) {
        mq.bindValue(0, g.groupId);
        if (!mq.exec()) continue;
        while (mq.next()) {
            FileRow f;
            f.fileId = mq.value(0).toLongLong();
            f.rootId = mq.value(1).toLongLong();
            f.rootPath = mq.value(2).toString();
            f.rootRole = mq.value(3).toString();
            f.relPath = mq.value(4).toString();
            f.size = mq.value(5).toLongLong();
            f.mtimeMsec = mq.value(6).toLongLong();
            f.sha256 = mq.value(7).toString();
            f.scanStatus = mq.value(8).toInt();
            f.errorMessage = mq.value(9).toString();
            f.absPath = QDir(f.rootPath).filePath(f.relPath);
            g.members.append(f);
            if (f.fileId == g.keeperFileId) g.keeperPath = f.absPath;
        }
    }
    return out;
}

// --- Folder matches -------------------------------------------------------

bool DatabaseManager::clearFolderMatches() {
    return exec(QStringLiteral("DELETE FROM folder_match"));
}

bool DatabaseManager::addFolderMatch(qint64 folderA, qint64 folderB, const QString& matchType,
                                     double score, const QString& notes) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO folder_match(folder_a, folder_b, match_type, overlap_score, "
        "review_status, notes) VALUES(?,?,?,?, 'pending', ?)"));
    q.addBindValue(folderA);
    q.addBindValue(folderB);
    q.addBindValue(matchType);
    q.addBindValue(score);
    q.addBindValue(notes);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::setFolderMatchReview(qint64 matchId, const QString& status) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE folder_match SET review_status=? WHERE match_id=?"));
    q.addBindValue(status);
    q.addBindValue(matchId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVector<FolderMatchRow> DatabaseManager::folderMatches() const {
    QVector<FolderMatchRow> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT m.match_id, m.folder_a, m.folder_b, fa.rel_path, ra.path, "
        "fb.rel_path, rb.path, m.match_type, m.overlap_score, m.review_status, m.notes "
        "FROM folder_match m "
        "JOIN folder fa ON fa.folder_id=m.folder_a JOIN root ra ON ra.root_id=fa.root_id "
        "JOIN folder fb ON fb.folder_id=m.folder_b JOIN root rb ON rb.root_id=fb.root_id "
        "ORDER BY m.overlap_score DESC"));
    if (!q.exec()) return out;
    while (q.next()) {
        FolderMatchRow m;
        m.matchId = q.value(0).toLongLong();
        m.folderAId = q.value(1).toLongLong();
        m.folderBId = q.value(2).toLongLong();
        const QString relA = q.value(3).toString();
        const QString rootA = q.value(4).toString();
        const QString relB = q.value(5).toString();
        const QString rootB = q.value(6).toString();
        m.pathA = relA.isEmpty() ? rootA : QDir(rootA).filePath(relA);
        m.pathB = relB.isEmpty() ? rootB : QDir(rootB).filePath(relB);
        m.matchType = q.value(7).toString();
        m.overlapScore = q.value(8).toDouble();
        m.reviewStatus = q.value(9).toString();
        m.notes = q.value(10).toString();
        out.append(m);
    }
    return out;
}

// --- Jobs -----------------------------------------------------------------

qint64 DatabaseManager::createJob() {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO job(status, phase, started_at) VALUES('running','traversal',?)"));
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::updateJobStatus(qint64 jobId, const QString& status,
                                      const QString& phase) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE job SET status=?, phase=? WHERE job_id=?"));
    q.addBindValue(status);
    q.addBindValue(phase);
    q.addBindValue(jobId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::finishJob(qint64 jobId, const QString& status) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE job SET status=?, phase='done', finished_at=? WHERE job_id=?"));
    q.addBindValue(status);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(jobId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

std::optional<JobRow> DatabaseManager::latestResumableJob() const {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT job_id, status, phase, started_at, finished_at FROM job "
            "WHERE status IN ('paused','running') ORDER BY job_id DESC LIMIT 1"))) {
        return std::nullopt;
    }
    if (q.next()) {
        JobRow j;
        j.jobId = q.value(0).toLongLong();
        j.status = q.value(1).toString();
        j.phase = q.value(2).toString();
        j.startedAt = q.value(3).toString();
        j.finishedAt = q.value(4).toString();
        return j;
    }
    return std::nullopt;
}

std::optional<JobRow> DatabaseManager::jobById(qint64 jobId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT job_id, status, phase, started_at, finished_at FROM job WHERE job_id=?"));
    q.addBindValue(jobId);
    if (q.exec() && q.next()) {
        JobRow j;
        j.jobId = q.value(0).toLongLong();
        j.status = q.value(1).toString();
        j.phase = q.value(2).toString();
        j.startedAt = q.value(3).toString();
        j.finishedAt = q.value(4).toString();
        return j;
    }
    return std::nullopt;
}

// --- Tasks ----------------------------------------------------------------

qint64 DatabaseManager::addTask(qint64 jobId, const QString& type, qint64 targetId) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO task(job_id, task_type, target_id, status, attempts, updated_at) "
        "VALUES(?,?,?,'pending',0,?)"));
    q.addBindValue(jobId);
    q.addBindValue(type);
    q.addBindValue(targetId);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::updateTaskStatus(qint64 taskId, const QString& status,
                                       const QString& error) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE task SET status=?, error=?, attempts=attempts+1, updated_at=? "
        "WHERE task_id=?"));
    q.addBindValue(status);
    q.addBindValue(error.isEmpty() ? QVariant() : QVariant(error));
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(taskId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVector<TaskRow> DatabaseManager::tasksByJob(qint64 jobId) const {
    QVector<TaskRow> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT task_id, job_id, task_type, target_id, status, attempts, error "
        "FROM task WHERE job_id=? ORDER BY task_id"));
    q.addBindValue(jobId);
    if (!q.exec()) return out;
    while (q.next()) {
        TaskRow t;
        t.taskId = q.value(0).toLongLong();
        t.jobId = q.value(1).toLongLong();
        t.taskType = q.value(2).toString();
        t.targetId = q.value(3).toLongLong();
        t.status = q.value(4).toString();
        t.attempts = q.value(5).toInt();
        t.error = q.value(6).toString();
        out.append(t);
    }
    return out;
}

int DatabaseManager::pendingTaskCount(qint64 jobId) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM task WHERE job_id=? AND status IN ('pending','running')"));
    q.addBindValue(jobId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int DatabaseManager::resetTasks(qint64 jobId, const QString& taskType) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE task SET status='pending', error=NULL, updated_at=datetime('now') "
        "WHERE job_id=? AND task_type=?"));
    q.addBindValue(jobId);
    q.addBindValue(taskType);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return 0;
    }
    return q.numRowsAffected();
}

int DatabaseManager::recoverInterruptedTasks() {
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "UPDATE task SET status='pending', updated_at=datetime('now') "
            "WHERE status='running'"))) {
        return 0;
    }
    return q.numRowsAffected();
}

// --- Action log -----------------------------------------------------------

qint64 DatabaseManager::recordPlannedAction(qint64 jobId, const QString& opType,
                                            const QString& sourcePath,
                                            const QString& destPath,
                                            const QString& expectedIdentity,
                                            const QString& recoveryInfo) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO action_log(job_id, op_type, source_path, dest_path, "
        "expected_identity, status, created_at, recovery_info) "
        "VALUES(?,?,?,?,?,'planned',?,?)"));
    q.bindValue(0, jobId);
    q.bindValue(1, opType);
    q.bindValue(2, sourcePath);
    q.bindValue(3, destPath);
    q.bindValue(4, expectedIdentity);
    q.bindValue(5, QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(6, recoveryInfo);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toLongLong();
}

bool DatabaseManager::finalizeAction(qint64 actionId, const QString& status,
                                     const QString& completedAt) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE action_log SET status=?, completed_at=? WHERE action_id=?"));
    q.addBindValue(status);
    q.addBindValue(completedAt.isEmpty()
                       ? QDateTime::currentDateTime().toString(Qt::ISODate)
                       : completedAt);
    q.addBindValue(actionId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::setActionRecoveryInfo(qint64 actionId, const QString& info) {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE action_log SET recovery_info=? WHERE action_id=?"));
    q.addBindValue(info);
    q.addBindValue(actionId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    return true;
}

QVector<ActionRow> DatabaseManager::actions(const QString& statusFilter) const {
    QVector<ActionRow> out;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT action_id, job_id, op_type, source_path, dest_path, expected_identity, "
        "status, created_at, completed_at, recovery_info FROM action_log");
    if (!statusFilter.isEmpty()) sql += QStringLiteral(" WHERE status=?");
    sql += QStringLiteral(" ORDER BY action_id DESC");
    q.prepare(sql);
    if (!statusFilter.isEmpty()) q.addBindValue(statusFilter);
    if (!q.exec()) return out;
    while (q.next()) {
        ActionRow a;
        a.actionId = q.value(0).toLongLong();
        a.jobId = q.value(1).toLongLong();
        a.opType = q.value(2).toString();
        a.sourcePath = q.value(3).toString();
        a.destPath = q.value(4).toString();
        a.expectedIdentity = q.value(5).toString();
        a.status = q.value(6).toString();
        a.createdAt = q.value(7).toString();
        a.completedAt = q.value(8).toString();
        a.recoveryInfo = q.value(9).toString();
        out.append(a);
    }
    return out;
}

std::optional<ActionRow> DatabaseManager::actionById(qint64 actionId) const {
    for (const ActionRow& a : actions()) {
        if (a.actionId == actionId) return a;
    }
    return std::nullopt;
}

// --- Statistics -----------------------------------------------------------

DatabaseManager::ScanStats DatabaseManager::statistics(qint64 jobId) const {
    Q_UNUSED(jobId);
    ScanStats s;
    {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral(
                "SELECT COUNT(*), COALESCE(SUM(size),0), "
                "COALESCE(SUM(CASE WHEN sha256 IS NOT NULL THEN 1 ELSE 0 END),0), "
                "COALESCE(SUM(CASE WHEN scan_status=3 THEN 1 ELSE 0 END),0) FROM file"))) {
            if (q.next()) {
                s.fileCount = q.value(0).toLongLong();
                s.totalBytes = q.value(1).toLongLong();
                s.hashedCount = q.value(2).toLongLong();
                s.errorCount = q.value(3).toLongLong();
            }
        }
    }
    {
        QSqlQuery q(m_db);
        // Recoverable savings only count copies that may actually be moved:
        // members of the group that live outside the protected main root and
        // are not the keeper. Duplicates that exist only inside the main
        // folder are real duplicates but can never be consolidated.
        if (q.exec(QStringLiteral(
                "SELECT COUNT(*), "
                "COALESCE(SUM(movable), 0), "
                "COALESCE(SUM(g.file_size * movable), 0) "
                "FROM (SELECT g.group_id, g.file_size, "
                "  (SELECT COUNT(*) FROM duplicate_member dm "
                "     JOIN file f ON f.file_id = dm.file_id "
                "     JOIN root r ON r.root_id = f.root_id "
                "    WHERE dm.group_id = g.group_id "
                "      AND r.role <> 'main' "
                "      AND dm.file_id <> COALESCE(g.keeper_file_id, -1)) "
                "    AS movable "
                "  FROM duplicate_group g) AS g"))) {
            if (q.next()) {
                s.duplicateGroupCount = q.value(0).toLongLong();
                s.duplicateFileCount = q.value(1).toLongLong();
                s.wastedBytes = q.value(2).toLongLong();
            }
        }
    }
    {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral(
                "SELECT COUNT(*) FROM folder_match WHERE review_status='pending'"))) {
            if (q.next()) s.pendingReviewCount = q.value(0).toLongLong();
        }
    }
    return s;
}

} // namespace dedupvault::core
