#include "common/blob_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace pastetry {

BlobStore::BlobStore(QString blobRootDir) : m_blobRootDir(std::move(blobRootDir)) {
    QDir().mkpath(m_blobRootDir);
}

bool BlobStore::ensureTables(QSqlDatabase &db, QString *error) {
    QSqlQuery query(db);
    if (!query.exec(
            "CREATE TABLE IF NOT EXISTS blobs ("
            "hash TEXT PRIMARY KEY,"
            "path TEXT NOT NULL,"
            "size INTEGER NOT NULL,"
            "ref_count INTEGER NOT NULL"
            ")")) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }
    return true;
}

QString BlobStore::hashBytes(const QByteArray &bytes) const {
    // SHA-256 is widely available in Qt and good enough for dedupe identity.
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString BlobStore::pathForHash(const QString &hash) const {
    const QString shard = hash.left(2);
    const QString shardDir = QDir(m_blobRootDir).filePath(shard);
    QDir().mkpath(shardDir);
    return QDir(shardDir).filePath(hash + ".bin");
}

QString BlobStore::putBlob(QSqlDatabase &db, const QByteArray &bytes, QString *error) {
    const QString hash = hashBytes(bytes);

    QSqlQuery find(db);
    find.prepare("SELECT ref_count FROM blobs WHERE hash = ?");
    find.addBindValue(hash);
    if (!find.exec()) {
        if (error) {
            *error = find.lastError().text();
        }
        return {};
    }

    if (find.next()) {
        QSqlQuery update(db);
        update.prepare("UPDATE blobs SET ref_count = ref_count + 1 WHERE hash = ?");
        update.addBindValue(hash);
        if (!update.exec()) {
            if (error) {
                *error = update.lastError().text();
            }
            return {};
        }
        return hash;
    }

    const QString path = pathForHash(hash);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = file.errorString();
        }
        return {};
    }
    if (file.write(bytes) != bytes.size()) {
        if (error) {
            *error = file.errorString();
        }
        return {};
    }

    QSqlQuery insert(db);
    insert.prepare(
        "INSERT INTO blobs(hash, path, size, ref_count) VALUES (?, ?, ?, 1)");
    insert.addBindValue(hash);
    insert.addBindValue(path);
    insert.addBindValue(static_cast<qint64>(bytes.size()));
    if (!insert.exec()) {
        if (error) {
            *error = insert.lastError().text();
        }
        return {};
    }

    return hash;
}

QByteArray BlobStore::loadBlob(const QString &hash, QString *error) const {
    const QString path = pathForHash(hash);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return {};
    }
    return file.readAll();
}

bool BlobStore::releaseBlob(QSqlDatabase &db, const QString &hash, QString *error) {
    return releaseBlobRefs(db, hash, 1, error);
}

bool BlobStore::releaseBlobRefs(QSqlDatabase &db, const QString &hash, int count,
                                QString *error) {
    if (count <= 0) {
        return true;
    }

    QSqlQuery query(db);
    query.prepare("SELECT ref_count, path FROM blobs WHERE hash = ?");
    query.addBindValue(hash);
    if (!query.exec()) {
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    if (!query.next()) {
        return true;
    }

    const int refCount = query.value(0).toInt();
    const QString path = query.value(1).toString();

    if (refCount > count) {
        QSqlQuery dec(db);
        dec.prepare("UPDATE blobs SET ref_count = ref_count - ? WHERE hash = ?");
        dec.addBindValue(count);
        dec.addBindValue(hash);
        if (!dec.exec()) {
            if (error) {
                *error = dec.lastError().text();
            }
            return false;
        }
        return true;
    }

    QFile blobFile(path);
    if (blobFile.exists() && !blobFile.remove()) {
        if (error) {
            *error = QStringLiteral("Failed to remove blob file '%1': %2")
                         .arg(path, blobFile.errorString());
        }
        return false;
    }

    QSqlQuery del(db);
    del.prepare("DELETE FROM blobs WHERE hash = ?");
    del.addBindValue(hash);
    if (!del.exec()) {
        if (error) {
            *error = del.lastError().text();
        }
        return false;
    }

    return true;
}

}  // namespace pastetry
