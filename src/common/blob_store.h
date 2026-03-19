#pragma once

#include <QSqlDatabase>
#include <QString>

namespace pastetry {

class BlobStore {
public:
    explicit BlobStore(QString blobRootDir);

    bool ensureTables(QSqlDatabase &db, QString *error);
    QString putBlob(QSqlDatabase &db, const QByteArray &bytes, QString *error);
    QByteArray loadBlob(const QString &hash, QString *error) const;
    bool releaseBlob(QSqlDatabase &db, const QString &hash, QString *error);
    bool releaseBlobRefs(QSqlDatabase &db, const QString &hash, int count, QString *error);

private:
    QString pathForHash(const QString &hash) const;
    QString hashBytes(const QByteArray &bytes) const;

    QString m_blobRootDir;
};

}  // namespace pastetry
