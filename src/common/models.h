#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

namespace pastetry {

struct CapturedFormat {
    QString mimeType;
    QByteArray data;
};

struct CapturedEntry {
    QString sourceApp;
    QString sourceWindow;
    QString preview;
    QVector<CapturedFormat> formats;
};

struct FormatDescriptor {
    QString mimeType;
    qint64 byteSize = 0;
    QString blobHash;
};

struct EntrySummary {
    qint64 id = 0;
    qint64 createdAtMs = 0;
    QString preview;
    QString sourceApp;
    bool pinned = false;
    int formatCount = 0;
    QString imageBlobHash;
};

struct EntryDetail {
    qint64 id = 0;
    qint64 createdAtMs = 0;
    QString preview;
    QString sourceApp;
    QString sourceWindow;
    bool pinned = false;
    QVector<FormatDescriptor> formats;
};

struct SearchResult {
    QVector<EntrySummary> entries;
    int nextCursor = -1;
};

}  // namespace pastetry
