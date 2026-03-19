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

enum class SearchMode {
    Plain = 0,
    Regex = 1,
    Advanced = 2,
};

inline QString searchModeToString(SearchMode mode) {
    switch (mode) {
        case SearchMode::Regex:
            return QStringLiteral("regex");
        case SearchMode::Advanced:
            return QStringLiteral("advanced");
        case SearchMode::Plain:
        default:
            return QStringLiteral("plain");
    }
}

inline SearchMode searchModeFromString(const QString &modeText) {
    const QString normalized = modeText.trimmed().toLower();
    if (normalized == QStringLiteral("regex")) {
        return SearchMode::Regex;
    }
    if (normalized == QStringLiteral("advanced")) {
        return SearchMode::Advanced;
    }
    return SearchMode::Plain;
}

struct SearchRequest {
    SearchMode mode = SearchMode::Plain;
    QString query;
    int cursor = 0;
    int limit = 100;
    bool regexStrict = false;
};

struct SearchResult {
    QVector<EntrySummary> entries;
    int nextCursor = -1;
    bool queryValid = true;
    QString queryError;
};

}  // namespace pastetry
