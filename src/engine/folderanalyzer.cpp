#include "folderanalyzer.h"
#include "core/logger.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QVector>
#include <algorithm>

namespace dedupvault::engine {

namespace {

struct FStats {
    int    direct = 0;
    qint64 rec = 0;
    qint64 size = 0;
};

struct SubEntry {
    QString suffix; // path relative to the compared folder
    qint64  size = 0;
    QString hash;   // empty when not hashed yet
};

/// Parent directory of a relative path ("" for the top level of a root).
QString parentRel(const QString& rel) {
    const int idx = qMax(rel.lastIndexOf(QLatin1Char('/')), rel.lastIndexOf(QLatin1Char('\\')));
    if (idx < 0) return QString();
    return rel.left(idx);
}

/// All ancestor folder keys of a relative directory path, self first.
QVector<QString> ancestorsOf(const QString& dir) {
    QVector<QString> out;
    QString cur = dir;
    while (true) {
        out.append(cur);
        if (cur.isEmpty()) break;
        const QString next = parentRel(cur);
        if (next == cur) break;
        cur = next;
    }
    return out;
}

/// Files of one subtree as (suffix, size, hash) using an index range scan
/// rather than LIKE (no wildcard escaping problems, no per-character
/// encoding mismatches between SQLite substr and QString sizes).
QVector<SubEntry> fetchSubtree(core::DatabaseManager& db, qint64 rootId,
                               const QString& rel) {
    QVector<SubEntry> out;

    const QString sep(QDir::separator());
    const QString prefix = rel.isEmpty() ? QString() : rel + sep;
    const QString upperBound = prefix + QString(QChar(0xFFFF));

    QSqlQuery q(db.database());
    q.prepare(QStringLiteral(
        "SELECT rel_path, size, sha256 FROM file "
        "WHERE root_id=? AND rel_path>=? AND rel_path<? ORDER BY rel_path"));
    q.addBindValue(rootId);
    q.addBindValue(prefix);
    q.addBindValue(upperBound);
    if (!q.exec()) {
        core::Logger::instance().warning(QStringLiteral("subtree query failed: %1")
                                       .arg(q.lastError().text()));
        return out;
    }
    while (q.next()) {
        SubEntry e;
        e.suffix = q.value(0).toString().mid(prefix.size());
        e.size = q.value(1).toLongLong();
        e.hash = q.value(2).toString();
        out.append(std::move(e));
    }
    return out;
}

/// Deterministic subtree signature from sorted (suffix, fingerprint) pairs.
/// Only used for pairs whose files are fully hashed, so the fingerprint is
/// always the SHA-256 of content: path alone is never proof of identity.
QString subtreeSignature(QVector<SubEntry>& entries, bool& allHashed) {
    allHashed = true;
    std::sort(entries.begin(), entries.end(),
              [](const SubEntry& a, const SubEntry& b) {
                  return QString::compare(a.suffix, b.suffix, Qt::CaseSensitive) < 0;
              });

    QCryptographicHash h(QCryptographicHash::Sha256);
    for (const SubEntry& e : entries) {
        if (e.hash.isEmpty()) {
            allHashed = false;
            h.addData("S:");
            h.addData(QByteArray::number(e.size));
        } else {
            h.addData("H:");
            h.addData(e.hash.toLatin1());
        }
        h.addData("\t");
        h.addData(e.suffix.toUtf8());
        h.addData("\n");
    }
    return QString::fromLatin1(h.result().toHex());
}

} // namespace

// ---------------------------------------------------------------------------

QString FolderAnalyzer::classifyFolder(const QString& absPath) {
    const QDir dir(absPath);
    if (!dir.exists()) return QStringLiteral("unknown");

    // Git repository: identity and history matter; never scatter automatically.
    if (QFileInfo::exists(dir.filePath(QStringLiteral(".git")))) {
        return QStringLiteral("git_repo");
    }

    // Common software project manifests.
    static const char* manifests[] = {
        "package.json", "Cargo.toml", "pom.xml", "CMakeLists.txt",
        "setup.py", "pyproject.toml", "go.mod", "*.sln", "Makefile",
        "build.gradle", "composer.json"
    };
    for (const char* m : manifests) {
        const QString name = QString::fromLatin1(m);
        if (name.startsWith(QLatin1Char('*'))) {
            if (!dir.entryList(QStringList{name}, QDir::Files).isEmpty()) {
                return QStringLiteral("project");
            }
        } else if (QFileInfo::exists(dir.filePath(name))) {
            return QStringLiteral("project");
        }
    }

    // Website: HTML together with styles/scripts/assets.
    const bool hasHtml = !dir.entryList(
        QStringList{QStringLiteral("*.html"), QStringLiteral("*.htm")},
        QDir::Files).isEmpty();
    if (hasHtml) {
        const bool hasSupport =
            !dir.entryList(QStringList{QStringLiteral("*.css")}, QDir::Files).isEmpty() ||
            !dir.entryList(QStringList{QStringLiteral("*.js")}, QDir::Files).isEmpty() ||
            dir.exists(QStringLiteral("assets")) ||
            dir.exists(QStringLiteral("img")) ||
            dir.exists(QStringLiteral("images"));
        if (hasSupport) return QStringLiteral("website");
    }

    return QStringLiteral("normal");
}

// ---------------------------------------------------------------------------

void FolderAnalyzer::computeFolderStats(core::DatabaseManager& db) {
    const QVector<core::RootRecord> roots = db.roots(false);
    for (const core::RootRecord& root : roots) {
        if (root.role == core::RootRole::Destination) continue;

        const QVector<core::FolderRow> folders = db.foldersByRoot(root.id);
        if (folders.isEmpty()) continue;

        QHash<QString, FStats> stats;
        QHash<QString, core::FolderRow> byRel;
        stats.reserve(folders.size());
        byRel.reserve(folders.size());
        for (const core::FolderRow& f : folders) {
            stats.insert(f.relPath, FStats{});
            byRel.insert(f.relPath, f);
        }

        QSqlQuery q(db.database());
        q.prepare(QStringLiteral(
            "SELECT rel_path, size FROM file WHERE root_id=? ORDER BY rel_path"));
        q.addBindValue(root.id);
        if (!q.exec()) {
            core::Logger::instance().warning(QStringLiteral("folder stats query failed: %1")
                                           .arg(q.lastError().text()));
            continue;
        }
        while (q.next()) {
            const QString rel = q.value(0).toString();
            const qint64 size = q.value(1).toLongLong();
            const QString dir = parentRel(rel);

            auto directIt = stats.find(dir);
            if (directIt != stats.end()) {
                directIt->direct += 1;
            }
            for (const QString& anc : ancestorsOf(dir)) {
                auto it = stats.find(anc);
                if (it != stats.end()) {
                    it->rec += 1;
                    it->size += size;
                }
            }
        }

        for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
            const core::FolderRow& row = byRel.value(it.key());
            const QString classification = classifyFolder(row.absPath);
            db.updateFolderStats(row.folderId, it.value().direct, it.value().rec,
                                 it.value().size, QString(), classification);
        }
    }

    core::Logger::instance().info(QStringLiteral("Folder statistics computed for %1 roots")
                                .arg(roots.size()));
}

// ---------------------------------------------------------------------------

void FolderAnalyzer::compareFolders(core::DatabaseManager& db, double signatureFloor) {
    db.clearFolderMatches();

    const QVector<core::RootRecord> allRoots = db.roots(false);
    QVector<core::RootRecord> roots;
    for (const core::RootRecord& r : allRoots) {
        if (r.role != core::RootRole::Destination) roots.append(r);
    }
    if (roots.size() < 2) return;

    auto buildNote = [](const core::FolderRow& a, const core::FolderRow& b) {
        const QString ca = a.classification;
        const QString cb = b.classification;
        QString note;
        const bool structA = !ca.isEmpty() && ca != QLatin1String("normal");
        const bool structB = !cb.isEmpty() && cb != QLatin1String("normal");
        if (structA || structB) {
            note = QStringLiteral(
                "Structured content (A: %1, B: %2) — explicit review required "
                "before moving anything.").arg(ca, cb);
        }
        return note;
    };

    int matchCount = 0;

    // --- Pass 1: same relative path mirrored across roots -------------------
    for (int i = 0; i < roots.size(); ++i) {
        for (int j = i + 1; j < roots.size(); ++j) {
            const core::RootRecord& ra = roots[i];
            const core::RootRecord& rb = roots[j];

            const QVector<core::FolderRow> faList = db.foldersByRoot(ra.id);
            const QVector<core::FolderRow> fbList = db.foldersByRoot(rb.id);
            QHash<QString, core::FolderRow> fbByRel;
            fbByRel.reserve(fbList.size());
            for (const core::FolderRow& f : fbList) fbByRel.insert(f.relPath, f);

            for (const core::FolderRow& fa : faList) {
                const auto foundB = fbByRel.constFind(fa.relPath);
                if (foundB == fbByRel.cend()) continue;
                const core::FolderRow& fb = foundB.value();

                const QVector<SubEntry> ea = fetchSubtree(db, ra.id, fa.relPath);
                const QVector<SubEntry> eb = fetchSubtree(db, rb.id, fb.relPath);
                if (ea.isEmpty() || eb.isEmpty()) continue;

                QHash<QString, const SubEntry*> mapB;
                mapB.reserve(eb.size());
                for (const SubEntry& e : eb) mapB.insert(e.suffix, &e);

                int strict = 0;
                int sizeOnly = 0;
                for (const SubEntry& a : ea) {
                    const auto mit = mapB.constFind(a.suffix);
                    if (mit == mapB.cend()) continue;
                    const SubEntry* b = mit.value();
                    if (a.size != b->size) continue;
                    if (!a.hash.isEmpty() && !b->hash.isEmpty() && a.hash == b->hash) {
                        ++strict;
                    } else if (a.hash.isEmpty() || b->hash.isEmpty()) {
                        ++sizeOnly;
                    }
                }

                const int denom = qMax(ea.size(), eb.size());
                const double score =
                    denom > 0 ? double(strict + sizeOnly) / double(denom) : 0.0;
                if (score < signatureFloor) continue;

                const bool exact = strict == ea.size() && strict == eb.size() &&
                                   ea.size() == eb.size();
                QString type = exact ? QStringLiteral("exact_subtree")
                                     : QStringLiteral("partial_overlap");
                QString note = buildNote(fa, fb);
                if (!exact && strict + sizeOnly == denom) {
                    const QString extra = QStringLiteral(
                        "Sizes and paths match but content hashes are incomplete — "
                        "run a complete hash pass to confirm exact identity.");
                    note = note.isEmpty() ? extra : note + QLatin1Char(' ') + extra;
                }

                if (db.addFolderMatch(fa.folderId, fb.folderId, type, score, note)) {
                    ++matchCount;
                }
            }
        }
    }

    // --- Pass 2: identical subtrees under different relative paths ----------
    // Bucket folders by (recursive file count, total size); only buckets with
    // more than one entry can contain duplicates, which bounds both memory
    // and the number of signature computations (capped per bucket as well).
    QVector<core::FolderRow> allFolders;
    for (const core::RootRecord& r : roots) {
        const auto list = db.foldersByRoot(r.id);
        for (const core::FolderRow& f : list) {
            if (f.recursiveFileCount <= 0) continue;
            allFolders.append(f);
        }
    }

    QVector<QVector<int>> buckets; // indices into allFolders
    QHash<QString, int> bucketIndex; // key: "<recursiveCount>:<totalSize>"
    for (int i = 0; i < allFolders.size(); ++i) {
        const core::FolderRow& f = allFolders[i];
        const QString key = QStringLiteral("%1:%2")
                                .arg(f.recursiveFileCount).arg(f.totalSize);
        auto it = bucketIndex.find(key);
        if (it == bucketIndex.end()) {
            bucketIndex.insert(key, buckets.size());
            QVector<int> single;
            single.append(i);
            buckets.append(single);
        } else {
            buckets[it.value()].append(i);
        }
    }

    static constexpr int MAX_BUCKET_PAIRS = 4000;
    int pairBudget = MAX_BUCKET_PAIRS;

    for (const QVector<int>& bucket : buckets) {
        if (bucket.size() < 2) continue;
        for (int x = 0; x < bucket.size() && pairBudget > 0; ++x) {
            for (int y = x + 1; y < bucket.size() && pairBudget > 0; ++y) {
                const core::FolderRow& a = allFolders[bucket[x]];
                const core::FolderRow& b = allFolders[bucket[y]];
                if (a.rootId == b.rootId) continue;
                if (a.relPath == b.relPath) continue; // covered by pass 1
                if (a.recursiveFileCount != b.recursiveFileCount ||
                    a.totalSize != b.totalSize) {
                    continue;
                }
                --pairBudget;

                QVector<SubEntry> ea = fetchSubtree(db, a.rootId, a.relPath);
                QVector<SubEntry> eb = fetchSubtree(db, b.rootId, b.relPath);
                bool ha = false;
                bool hb = false;
                const QString sigA = subtreeSignature(ea, ha);
                const QString sigB = subtreeSignature(eb, hb);

                if (!ha || !hb || sigA != sigB) continue;

                QString note = buildNote(a, b);
                const QString extra = QStringLiteral(
                    "Exact duplicate subtree under a different path — moving the "
                    "whole subtree is offered as a single reviewable action.");
                note = note.isEmpty() ? extra : note + QLatin1Char(' ') + extra;

                if (db.addFolderMatch(a.folderId, b.folderId, QStringLiteral("exact_subtree"),
                                      1.0, note)) {
                    ++matchCount;
                }
            }
        }
    }

    core::Logger::instance().info(
        QStringLiteral("Folder analysis: %1 folders compared, %2 matches recorded")
            .arg(allFolders.size()).arg(matchCount));
}

} // namespace dedupvault::engine