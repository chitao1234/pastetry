#include "clipd/clipboard_daemon.h"

#include "common/ipc_protocol.h"

#include <QCborArray>
#include <QBuffer>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>

Q_LOGGING_CATEGORY(logClipd, "pastetry.clipd")

namespace pastetry {
namespace {
constexpr int kMaxMimePayloadBytes = 10 * 1024 * 1024;
constexpr int kDedupWindowMs = 300;

QString htmlToPreview(const QString &html) {
    QString plain = html;
    plain.remove(QRegularExpression("<[^>]*>"));
    plain.replace("&nbsp;", " ");
    plain.replace("&amp;", "&");
    return plain.simplified();
}

QString normalizeTextPreview(const QString &text) {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar('\r'), QChar('\n'));

    const QStringList rawLines = normalized.split(QChar('\n'));
    QStringList lines;
    lines.reserve(rawLines.size());
    for (const QString &line : rawLines) {
        QString cleaned = line;
        cleaned.replace(QRegularExpression("[\\t ]+"), QStringLiteral(" "));
        lines.push_back(cleaned.trimmed());
    }

    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }

    return lines.join(QChar('\n'));
}

QCborMap toCbor(const EntrySummary &entry) {
    QCborMap map;
    map.insert(QStringLiteral("id"), entry.id);
    map.insert(QStringLiteral("created_at_ms"), entry.createdAtMs);
    map.insert(QStringLiteral("preview"), entry.preview);
    map.insert(QStringLiteral("source_app"), entry.sourceApp);
    map.insert(QStringLiteral("pinned"), entry.pinned);
    map.insert(QStringLiteral("format_count"), entry.formatCount);
    return map;
}

QCborMap toCbor(const EntryDetail &entry) {
    QCborArray formats;
    for (const auto &format : entry.formats) {
        QCborMap f;
        f.insert(QStringLiteral("mime_type"), format.mimeType);
        f.insert(QStringLiteral("byte_size"), format.byteSize);
        f.insert(QStringLiteral("blob_hash"), format.blobHash);
        formats.append(f);
    }

    QCborMap map;
    map.insert(QStringLiteral("id"), entry.id);
    map.insert(QStringLiteral("created_at_ms"), entry.createdAtMs);
    map.insert(QStringLiteral("preview"), entry.preview);
    map.insert(QStringLiteral("source_app"), entry.sourceApp);
    map.insert(QStringLiteral("source_window"), entry.sourceWindow);
    map.insert(QStringLiteral("pinned"), entry.pinned);
    map.insert(QStringLiteral("formats"), formats);
    return map;
}

QVector<QUrl> parseUriList(const QByteArray &bytes) {
    QVector<QUrl> urls;
    for (const auto &line : bytes.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        urls.push_back(QUrl::fromEncoded(trimmed));
    }
    return urls;
}
}  // namespace

ClipboardDaemon::ClipboardDaemon(AppPaths paths, QObject *parent)
    : QObject(parent),
      m_paths(std::move(paths)),
      m_repo(m_paths.dbPath, m_paths.blobDir, QStringLiteral("clipd-main")) {}

bool ClipboardDaemon::start(QString *error) {
    if (!m_repo.open(error) || !m_repo.initialize(error)) {
        return false;
    }

    if (QLocalServer::removeServer(m_paths.socketName)) {
        qCInfo(logClipd) << "Removed stale socket" << m_paths.socketName;
    }

    if (!m_server.listen(m_paths.socketName)) {
        if (error) {
            *error = m_server.errorString();
        }
        return false;
    }

    connect(&m_server, &QLocalServer::newConnection, this,
            &ClipboardDaemon::onNewConnection);

    m_clipboard = QGuiApplication::clipboard();
    connect(m_clipboard, &QClipboard::dataChanged, this,
            &ClipboardDaemon::onClipboardChanged);

    qCInfo(logClipd) << "Daemon started, socket:" << m_paths.socketName;
    return true;
}

CapturedEntry ClipboardDaemon::captureFromMimeData(const QMimeData *mimeData) const {
    CapturedEntry entry;
    entry.sourceApp = QStringLiteral("unknown");
    entry.sourceWindow = QStringLiteral("");

    if (!mimeData) {
        return entry;
    }

    QSet<QString> seen;

    auto addFormat = [&](const QString &mimeType, const QByteArray &data) {
        if (seen.contains(mimeType) || data.isEmpty() || data.size() > kMaxMimePayloadBytes) {
            return;
        }
        seen.insert(mimeType);
        entry.formats.push_back(CapturedFormat{mimeType, data});
    };

    if (mimeData->hasText()) {
        const QString text = mimeData->text();
        entry.preview = normalizeTextPreview(text).left(2048);
        addFormat("text/plain", text.toUtf8());
    }

    if (mimeData->hasHtml()) {
        const QString html = mimeData->html();
        if (entry.preview.isEmpty()) {
            entry.preview = htmlToPreview(html).left(200);
        }
        addFormat("text/html", html.toUtf8());
    }

    if (mimeData->hasFormat("text/rtf")) {
        addFormat("text/rtf", mimeData->data("text/rtf"));
    }
    if (mimeData->hasFormat("application/rtf")) {
        addFormat("application/rtf", mimeData->data("application/rtf"));
    }

    if (mimeData->hasImage()) {
        const auto variant = mimeData->imageData();
        const QImage image = variant.value<QImage>();
        if (!image.isNull()) {
            QByteArray pngBytes;
            QBuffer buffer(&pngBytes);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            addFormat("image/png", pngBytes);
            if (entry.preview.isEmpty()) {
                entry.preview = QStringLiteral("[Image] %1x%2")
                                    .arg(image.width())
                                    .arg(image.height());
            }
        }
    }

    if (mimeData->hasUrls()) {
        QByteArray uriList;
        for (const QUrl &url : mimeData->urls()) {
            uriList += url.toEncoded();
            uriList += '\n';
        }
        addFormat("text/uri-list", uriList);
        if (entry.preview.isEmpty() && !mimeData->urls().isEmpty()) {
            entry.preview = QStringLiteral("[Files] %1 item(s)").arg(mimeData->urls().size());
        }
    }

    return entry;
}

QString ClipboardDaemon::fingerprint(const CapturedEntry &entry) const {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &format : entry.formats) {
        hash.addData(format.mimeType.toUtf8());
        hash.addData(format.data);
    }
    return QString::fromLatin1(hash.result().toHex());
}

void ClipboardDaemon::onClipboardChanged() {
    if (m_suppressCapture) {
        return;
    }

    const CapturedEntry entry = captureFromMimeData(m_clipboard->mimeData());
    if (entry.formats.isEmpty()) {
        return;
    }

    const QString fp = fingerprint(entry);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (fp == m_lastFingerprint && (now - m_lastCaptureAtMs) < kDedupWindowMs) {
        return;
    }

    QString error;
    const qint64 id = m_repo.insertEntry(entry, &error);
    if (id < 0) {
        qCWarning(logClipd) << "Failed to insert clipboard entry:" << error;
        return;
    }

    m_lastFingerprint = fp;
    m_lastCaptureAtMs = now;
}

void ClipboardDaemon::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        QLocalSocket *socket = m_server.nextPendingConnection();
        m_clientBuffers.insert(socket, QByteArray{});

        connect(socket, &QLocalSocket::readyRead, this,
                [this, socket] { onClientReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_clientBuffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void ClipboardDaemon::onClientReadyRead(QLocalSocket *socket) {
    QByteArray &buffer = m_clientBuffers[socket];
    buffer.append(socket->readAll());

    QCborMap request;
    while (ipc::tryDecodeFrame(&buffer, &request)) {
        const QCborMap response = handleRequest(request);
        socket->write(ipc::encodeFrame(response));
    }
}

QCborMap ClipboardDaemon::handleRequest(const QCborMap &request) {
    const QString id = request.value("id").toString();
    const QString method = request.value("method").toString();
    const QCborMap params = request.value("params").toMap();

    QString error;

    if (method == "SearchEntries") {
        const QString query = params.value("query").toString();
        const int cursor = params.value("cursor").toInteger();
        const int limit = params.value("limit").toInteger();

        const SearchResult result = m_repo.searchEntries(query, cursor, limit, &error);
        if (!error.isEmpty()) {
            return ipc::makeError(id, error);
        }

        QCborArray entries;
        for (const auto &entry : result.entries) {
            entries.append(toCbor(entry));
        }

        QCborMap payload;
        payload.insert(QStringLiteral("entries"), entries);
        payload.insert(QStringLiteral("next_cursor"), result.nextCursor);
        return ipc::makeResponse(id, payload);
    }

    if (method == "GetEntryDetail") {
        const qint64 entryId = params.value("entry_id").toInteger();
        const EntryDetail detail = m_repo.getEntryDetail(entryId, &error);
        if (!error.isEmpty()) {
            return ipc::makeError(id, error);
        }
        return ipc::makeResponse(id, toCbor(detail));
    }

    if (method == "ActivateEntry") {
        const qint64 entryId = params.value("entry_id").toInteger();
        const QString preferredFormat = params.value("preferred_format").toString();
        if (!activateEntry(entryId, preferredFormat, &error)) {
            return ipc::makeError(id, error);
        }
        QCborMap payload;
        payload.insert(QStringLiteral("status"), "ok");
        return ipc::makeResponse(id, payload);
    }

    if (method == "PinEntry") {
        const qint64 entryId = params.value("entry_id").toInteger();
        const bool pinned = params.value("pinned").toBool();
        if (!m_repo.setPinned(entryId, pinned, &error)) {
            if (error.isEmpty()) {
                error = "Entry not found";
            }
            return ipc::makeError(id, error);
        }
        QCborMap payload;
        payload.insert(QStringLiteral("status"), "ok");
        return ipc::makeResponse(id, payload);
    }

    if (method == "DeleteEntry") {
        const qint64 entryId = params.value("entry_id").toInteger();
        if (!m_repo.deleteEntry(entryId, &error)) {
            return ipc::makeError(id, error);
        }
        QCborMap payload;
        payload.insert(QStringLiteral("status"), "ok");
        return ipc::makeResponse(id, payload);
    }

    if (method == "ClearHistory") {
        const bool keepPinned = params.value("keep_pinned").toBool();
        if (!m_repo.clearHistory(keepPinned, &error)) {
            return ipc::makeError(id, error);
        }
        QCborMap payload;
        payload.insert(QStringLiteral("status"), "ok");
        return ipc::makeResponse(id, payload);
    }

    return ipc::makeError(id, QStringLiteral("Unknown method: %1").arg(method));
}

bool ClipboardDaemon::activateEntry(qint64 entryId, const QString &preferredFormat,
                                    QString *error) {
    const EntryDetail detail = m_repo.getEntryDetail(entryId, error);
    if (error && !error->isEmpty()) {
        return false;
    }

    auto mimeData = std::make_unique<QMimeData>();

    bool appliedPreferred = preferredFormat.isEmpty();
    for (const auto &format : detail.formats) {
        QString blobError;
        const QByteArray bytes = m_repo.loadBlob(format.blobHash, &blobError);
        if (!blobError.isEmpty()) {
            if (error) {
                *error = blobError;
            }
            return false;
        }

        if (!preferredFormat.isEmpty() && format.mimeType == preferredFormat) {
            appliedPreferred = true;
        }

        mimeData->setData(format.mimeType, bytes);

        if (format.mimeType == "text/plain") {
            mimeData->setText(QString::fromUtf8(bytes));
        } else if (format.mimeType == "text/html") {
            mimeData->setHtml(QString::fromUtf8(bytes));
        } else if (format.mimeType == "text/uri-list") {
            QVector<QUrl> urls = parseUriList(bytes);
            mimeData->setUrls(urls);
        } else if (format.mimeType.startsWith("image/")) {
            const QImage image = QImage::fromData(bytes);
            if (!image.isNull()) {
                mimeData->setImageData(image);
            }
        }
    }

    if (!appliedPreferred) {
        if (error) {
            *error = QStringLiteral("Preferred format unavailable");
        }
        return false;
    }

    m_suppressCapture = true;
    m_clipboard->setMimeData(mimeData.release());
    QTimer::singleShot(250, this, [this] { m_suppressCapture = false; });

    return true;
}

}  // namespace pastetry
