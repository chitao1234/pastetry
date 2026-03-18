#include "common/clipboard_repository.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace pastetry {

ClipboardRepository::ClipboardRepository(QString dbPath, QString blobDir,
                                         QString connectionName)
    : m_dbPath(std::move(dbPath)),
      m_connectionName(std::move(connectionName)),
      m_blobStore(std::move(blobDir)) {}

ClipboardRepository::~ClipboardRepository() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    const QString name = m_connectionName;
    m_db = {};
    QSqlDatabase::removeDatabase(name);
}

bool ClipboardRepository::open(QString *error) {
    if (QSqlDatabase::contains(m_connectionName)) {
        m_db = QSqlDatabase::database(m_connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        m_db.setDatabaseName(m_dbPath);
    }

    if (!m_db.open()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA journal_mode=WAL");
    pragma.exec("PRAGMA synchronous=NORMAL");
    pragma.exec("PRAGMA temp_store=MEMORY");
    pragma.exec("PRAGMA mmap_size=268435456");

    return true;
}

bool ClipboardRepository::initialize(QString *error) {
    QSqlQuery query(m_db);

    const char *schemaSql[] = {
        "CREATE TABLE IF NOT EXISTS entries ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "created_at_ms INTEGER NOT NULL,"
        "source_app TEXT NOT NULL DEFAULT '',"
        "source_window TEXT NOT NULL DEFAULT '',"
        "preview TEXT NOT NULL DEFAULT '',"
        "pinned INTEGER NOT NULL DEFAULT 0"
        ")",
        "CREATE TABLE IF NOT EXISTS entry_formats ("
        "entry_id INTEGER NOT NULL,"
        "mime_type TEXT NOT NULL,"
        "byte_size INTEGER NOT NULL,"
        "blob_hash TEXT NOT NULL,"
        "PRIMARY KEY (entry_id, mime_type),"
        "FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE"
        ")",
        "CREATE INDEX IF NOT EXISTS idx_entries_created ON entries(created_at_ms DESC)",
        "CREATE INDEX IF NOT EXISTS idx_entry_formats_entry ON entry_formats(entry_id)",
        "CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5(preview, source_app)",
        "CREATE TRIGGER IF NOT EXISTS entries_ai AFTER INSERT ON entries BEGIN "
        "INSERT INTO entries_fts(rowid, preview, source_app) "
        "VALUES (new.id, new.preview, new.source_app);"
        "END",
        "CREATE TRIGGER IF NOT EXISTS entries_ad AFTER DELETE ON entries BEGIN "
        "DELETE FROM entries_fts WHERE rowid = old.id;"
        "END",
        "CREATE TRIGGER IF NOT EXISTS entries_au AFTER UPDATE ON entries BEGIN "
        "UPDATE entries_fts SET preview = new.preview, source_app = new.source_app "
        "WHERE rowid = new.id;"
        "END",
    };

    for (const auto &sql : schemaSql) {
        if (!query.exec(sql)) {
            if (error) {
                *error = query.lastError().text();
            }
            return false;
        }
    }

    return m_blobStore.ensureTables(m_db, error);
}

qint64 ClipboardRepository::insertEntry(const CapturedEntry &entry, QString *error) {
    if (entry.formats.isEmpty()) {
        if (error) {
            *error = "Entry has no formats";
        }
        return -1;
    }

    if (!m_db.transaction()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return -1;
    }

    QSqlQuery insertEntryQuery(m_db);
    insertEntryQuery.prepare(
        "INSERT INTO entries(created_at_ms, source_app, source_window, preview, pinned) "
        "VALUES (?, ?, ?, ?, 0)");
    insertEntryQuery.addBindValue(QDateTime::currentMSecsSinceEpoch());
    insertEntryQuery.addBindValue(entry.sourceApp.isNull() ? QStringLiteral("") : entry.sourceApp);
    insertEntryQuery.addBindValue(entry.sourceWindow.isNull() ? QStringLiteral("") : entry.sourceWindow);
    insertEntryQuery.addBindValue(entry.preview.isNull() ? QStringLiteral("") : entry.preview.left(512));

    if (!insertEntryQuery.exec()) {
        m_db.rollback();
        if (error) {
            *error = insertEntryQuery.lastError().text();
        }
        return -1;
    }

    const qint64 entryId = insertEntryQuery.lastInsertId().toLongLong();

    QSqlQuery insertFormat(m_db);
    insertFormat.prepare(
        "INSERT INTO entry_formats(entry_id, mime_type, byte_size, blob_hash) "
        "VALUES (?, ?, ?, ?)");

    for (const auto &format : entry.formats) {
        QString blobError;
        const QString hash = m_blobStore.putBlob(m_db, format.data, &blobError);
        if (hash.isEmpty()) {
            m_db.rollback();
            if (error) {
                *error = blobError;
            }
            return -1;
        }

        insertFormat.bindValue(0, entryId);
        insertFormat.bindValue(1, format.mimeType);
        insertFormat.bindValue(2, static_cast<qint64>(format.data.size()));
        insertFormat.bindValue(3, hash);

        if (!insertFormat.exec()) {
            m_db.rollback();
            if (error) {
                *error = insertFormat.lastError().text();
            }
            return -1;
        }
    }

    if (!m_db.commit()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return -1;
    }

    return entryId;
}

SearchResult ClipboardRepository::searchEntries(const QString &query, int cursor, int limit,
                                                QString *error) const {
    SearchResult result;

    const int safeLimit = qBound(1, limit, 500);
    const int safeCursor = qMax(0, cursor);

    QSqlQuery select(m_db);

    if (query.trimmed().isEmpty()) {
        select.prepare(
            "SELECT e.id, e.created_at_ms, e.preview, e.source_app, e.pinned, "
            "(SELECT COUNT(*) FROM entry_formats ef WHERE ef.entry_id = e.id) AS format_count "
            "FROM entries e "
            "ORDER BY e.pinned DESC, e.created_at_ms DESC "
            "LIMIT ? OFFSET ?");
        select.addBindValue(safeLimit + 1);
        select.addBindValue(safeCursor);
    } else {
        select.prepare(
            "SELECT e.id, e.created_at_ms, e.preview, e.source_app, e.pinned, "
            "(SELECT COUNT(*) FROM entry_formats ef WHERE ef.entry_id = e.id) AS format_count "
            "FROM entries_fts f "
            "JOIN entries e ON e.id = f.rowid "
            "WHERE entries_fts MATCH ? "
            "ORDER BY e.pinned DESC, bm25(entries_fts), e.created_at_ms DESC "
            "LIMIT ? OFFSET ?");
        select.addBindValue(query + "*");
        select.addBindValue(safeLimit + 1);
        select.addBindValue(safeCursor);
    }

    if (!select.exec()) {
        if (error) {
            *error = select.lastError().text();
        }
        return result;
    }

    while (select.next()) {
        EntrySummary summary;
        summary.id = select.value(0).toLongLong();
        summary.createdAtMs = select.value(1).toLongLong();
        summary.preview = select.value(2).toString();
        summary.sourceApp = select.value(3).toString();
        summary.pinned = select.value(4).toInt() == 1;
        summary.formatCount = select.value(5).toInt();
        result.entries.push_back(summary);
    }

    if (result.entries.size() > safeLimit) {
        result.entries.removeLast();
        result.nextCursor = safeCursor + safeLimit;
    }

    return result;
}

EntryDetail ClipboardRepository::getEntryDetail(qint64 entryId, QString *error) const {
    EntryDetail detail;

    QSqlQuery entryQuery(m_db);
    entryQuery.prepare(
        "SELECT id, created_at_ms, preview, source_app, source_window, pinned "
        "FROM entries WHERE id = ?");
    entryQuery.addBindValue(entryId);

    if (!entryQuery.exec()) {
        if (error) {
            *error = entryQuery.lastError().text();
        }
        return detail;
    }

    if (!entryQuery.next()) {
        if (error) {
            *error = "Entry not found";
        }
        return detail;
    }

    detail.id = entryQuery.value(0).toLongLong();
    detail.createdAtMs = entryQuery.value(1).toLongLong();
    detail.preview = entryQuery.value(2).toString();
    detail.sourceApp = entryQuery.value(3).toString();
    detail.sourceWindow = entryQuery.value(4).toString();
    detail.pinned = entryQuery.value(5).toInt() == 1;

    QSqlQuery formatQuery(m_db);
    formatQuery.prepare(
        "SELECT mime_type, byte_size, blob_hash "
        "FROM entry_formats WHERE entry_id = ? "
        "ORDER BY mime_type");
    formatQuery.addBindValue(entryId);
    if (!formatQuery.exec()) {
        if (error) {
            *error = formatQuery.lastError().text();
        }
        return {};
    }

    while (formatQuery.next()) {
        FormatDescriptor format;
        format.mimeType = formatQuery.value(0).toString();
        format.byteSize = formatQuery.value(1).toLongLong();
        format.blobHash = formatQuery.value(2).toString();
        detail.formats.push_back(format);
    }

    return detail;
}

QByteArray ClipboardRepository::loadBlob(const QString &hash, QString *error) const {
    return m_blobStore.loadBlob(hash, error);
}

bool ClipboardRepository::setPinned(qint64 entryId, bool pinned, QString *error) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE entries SET pinned = ? WHERE id = ?");
    query.addBindValue(pinned ? 1 : 0);
    query.addBindValue(entryId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool ClipboardRepository::cleanupEntryBlobs(qint64 entryId, QString *error) {
    QSqlQuery query(m_db);
    query.prepare("SELECT blob_hash FROM entry_formats WHERE entry_id = ?");
    query.addBindValue(entryId);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    QStringList blobHashes;
    while (query.next()) {
        blobHashes.push_back(query.value(0).toString());
    }

    for (const auto &hash : blobHashes) {
        if (!m_blobStore.releaseBlob(m_db, hash, error)) {
            return false;
        }
    }

    return true;
}

bool ClipboardRepository::deleteEntry(qint64 entryId, QString *error) {
    if (!m_db.transaction()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    if (!cleanupEntryBlobs(entryId, error)) {
        m_db.rollback();
        return false;
    }

    QSqlQuery del(m_db);
    del.prepare("DELETE FROM entries WHERE id = ?");
    del.addBindValue(entryId);
    if (!del.exec()) {
        m_db.rollback();
        if (error) {
            *error = del.lastError().text();
        }
        return false;
    }

    if (!m_db.commit()) {
        if (error) {
            *error = m_db.lastError().text();
        }
        return false;
    }

    return true;
}

bool ClipboardRepository::clearHistory(bool keepPinned, QString *error) {
    QSqlQuery query(m_db);
    if (!query.exec(keepPinned ? "SELECT id FROM entries WHERE pinned = 0"
                               : "SELECT id FROM entries")) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    QVector<qint64> ids;
    while (query.next()) {
        ids.push_back(query.value(0).toLongLong());
    }

    for (const auto id : ids) {
        if (!deleteEntry(id, error)) {
            return false;
        }
    }

    return true;
}

}  // namespace pastetry
