#pragma once

#include "common/blob_store.h"
#include "common/models.h"

#include <QSqlDatabase>

namespace pastetry {

class ClipboardRepository {
public:
    ClipboardRepository(QString dbPath, QString blobDir, QString connectionName);
    ~ClipboardRepository();

    bool open(QString *error);
    bool initialize(QString *error);

    qint64 insertEntry(const CapturedEntry &entry, QString *error);
    SearchResult searchEntries(const SearchRequest &request, QString *error) const;
    EntryDetail getEntryDetail(qint64 entryId, QString *error) const;
    QByteArray loadBlob(const QString &hash, QString *error) const;
    bool loadCapturePolicy(CapturePolicy *policy, QString *error) const;
    bool saveCapturePolicy(const CapturePolicy &policy, QString *error);
    bool setPinned(qint64 entryId, bool pinned, QString *error);
    bool movePinnedEntry(qint64 entryId, int targetPinnedIndex, QString *error);
    qint64 resolveSlotEntry(bool pinnedGroup, int slotOneBased, QString *error) const;
    bool deleteEntry(qint64 entryId, QString *error);
    bool clearHistory(bool keepPinned, QString *error);

private:
    bool cleanupEntryBlobs(qint64 entryId, QString *error);

    QString m_dbPath;
    QString m_connectionName;
    QSqlDatabase m_db;
    BlobStore m_blobStore;
};

}  // namespace pastetry
