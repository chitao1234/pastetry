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
constexpr int kDedupWindowMs = 300;
constexpr qint64 kMinPolicyBytes = 1024;
constexpr qint64 kMaxPolicyBytes = 1024LL * 1024 * 1024;

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
    map.insert(QStringLiteral("image_blob_hash"), entry.imageBlobHash);
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

QByteArray makeImagePreviewPng(const QByteArray &rawImageBytes, int maxEdge) {
    const QImage source = QImage::fromData(rawImageBytes);
    if (source.isNull()) {
        return {};
    }

    const int safeMaxEdge = qBound(16, maxEdge, 512);
    const QImage scaled =
        source.scaled(safeMaxEdge, safeMaxEdge, Qt::KeepAspectRatio,
                      Qt::SmoothTransformation);

    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    buffer.open(QIODevice::WriteOnly);
    if (!scaled.save(&buffer, "PNG")) {
        return {};
    }
    return pngBytes;
}

QStringList defaultAllowlistPatterns(CaptureProfile profile) {
    switch (profile) {
        case CaptureProfile::Strict:
            return {
                QStringLiteral("text/plain"),
                QStringLiteral("text/html"),
                QStringLiteral("text/rtf"),
                QStringLiteral("application/rtf"),
                QStringLiteral("text/uri-list"),
                QStringLiteral("image/png"),
            };
        case CaptureProfile::Broad:
            return {QStringLiteral("*")};
        case CaptureProfile::Balanced:
        default:
            return {
                QStringLiteral("text/plain"),
                QStringLiteral("text/html"),
                QStringLiteral("text/rtf"),
                QStringLiteral("application/rtf"),
                QStringLiteral("text/uri-list"),
                QStringLiteral("image/png"),
                QStringLiteral("text/*"),
                QStringLiteral("image/*"),
                QStringLiteral("application/x-qt-*"),
            };
    }
}

QStringList normalizedPatterns(const QStringList &patterns) {
    QStringList normalized;
    normalized.reserve(patterns.size());
    for (const QString &raw : patterns) {
        const QString trimmed = raw.trimmed().toLower();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        normalized.push_back(trimmed);
    }
    normalized.removeDuplicates();
    return normalized;
}

bool wildcardMatchCaseInsensitive(const QString &pattern, const QString &value) {
    const QRegularExpression re(
        QRegularExpression::wildcardToRegularExpression(pattern),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(value).hasMatch();
}

bool policyAllowsMime(const CapturePolicy &policy, const QString &mimeType) {
    const QString normalizedMime = mimeType.trimmed().toLower();
    if (normalizedMime.isEmpty()) {
        return false;
    }

    QStringList patterns = defaultAllowlistPatterns(policy.profile);
    patterns.append(normalizedPatterns(policy.customAllowlistPatterns));

    for (const QString &pattern : patterns) {
        if (wildcardMatchCaseInsensitive(pattern, normalizedMime)) {
            return true;
        }
    }
    return false;
}

QCborMap toCbor(const CapturePolicy &policy) {
    QCborMap map;
    map.insert(QStringLiteral("profile"), captureProfileToString(policy.profile));
    QCborArray customAllowlist;
    for (const QString &pattern : policy.customAllowlistPatterns) {
        const QString trimmed = pattern.trimmed();
        if (!trimmed.isEmpty()) {
            customAllowlist.append(trimmed);
        }
    }
    map.insert(QStringLiteral("custom_allowlist"), customAllowlist);
    map.insert(QStringLiteral("max_format_bytes"), policy.maxFormatBytes);
    map.insert(QStringLiteral("max_entry_bytes"), policy.maxEntryBytes);
    return map;
}

bool capturePolicyFromCbor(const QCborMap &params, CapturePolicy *policy, QString *error) {
    if (!policy) {
        if (error) {
            *error = QStringLiteral("policy output required");
        }
        return false;
    }

    const QString profileText = params.value(QStringLiteral("profile")).toString();
    const QString normalizedProfile = profileText.trimmed().toLower();
    if (normalizedProfile.isEmpty()) {
        if (error) {
            *error = QStringLiteral("profile is required");
        }
        return false;
    }
    if (normalizedProfile != QStringLiteral("strict") &&
        normalizedProfile != QStringLiteral("balanced") &&
        normalizedProfile != QStringLiteral("broad")) {
        if (error) {
            *error = QStringLiteral("Unknown capture profile: %1").arg(profileText);
        }
        return false;
    }

    CapturePolicy parsed;
    parsed.profile = captureProfileFromString(normalizedProfile);
    parsed.maxFormatBytes = params.value(QStringLiteral("max_format_bytes")).toInteger();
    parsed.maxEntryBytes = params.value(QStringLiteral("max_entry_bytes")).toInteger();

    const QCborValue allowlistValue = params.value(QStringLiteral("custom_allowlist"));
    if (allowlistValue.isArray()) {
        for (const QCborValue &item : allowlistValue.toArray()) {
            const QString pattern = item.toString().trimmed();
            if (!pattern.isEmpty()) {
                parsed.customAllowlistPatterns.push_back(pattern);
            }
        }
    } else if (allowlistValue.isString()) {
        for (const QString &line :
             allowlistValue.toString().split('\n', Qt::SkipEmptyParts)) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty()) {
                parsed.customAllowlistPatterns.push_back(trimmed);
            }
        }
    }

    *policy = parsed;
    return true;
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

    if (!loadCapturePolicy(error)) {
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
    qint64 totalBytes = 0;

    auto addFormat = [&](const QString &mimeType, const QByteArray &data,
                         const QString &sourceTag) {
        const QString normalizedMime = mimeType.trimmed().toLower();
        if (normalizedMime.isEmpty()) {
            return;
        }
        if (seen.contains(normalizedMime)) {
            return;
        }
        if (data.isEmpty()) {
            qCInfo(logClipd) << "Dropped format" << normalizedMime << "from" << sourceTag
                             << "- empty payload";
            return;
        }
        if (!policyAllowsMime(m_capturePolicy, normalizedMime)) {
            qCInfo(logClipd) << "Dropped format" << normalizedMime << "from" << sourceTag
                             << "- not allowed by capture policy";
            return;
        }
        if (data.size() > m_capturePolicy.maxFormatBytes) {
            qCInfo(logClipd) << "Dropped format" << normalizedMime << "from" << sourceTag
                             << "- exceeds per-format cap";
            return;
        }
        if ((totalBytes + data.size()) > m_capturePolicy.maxEntryBytes) {
            qCInfo(logClipd) << "Dropped format" << normalizedMime << "from" << sourceTag
                             << "- exceeds per-entry cap";
            return;
        }
        seen.insert(normalizedMime);
        totalBytes += data.size();
        entry.formats.push_back(CapturedFormat{normalizedMime, data});
    };

    const QStringList formats = mimeData->formats();
    for (const QString &mimeType : formats) {
        addFormat(mimeType, mimeData->data(mimeType), QStringLiteral("mimeData->formats"));
    }

    if (mimeData->hasText()) {
        const QString text = mimeData->text();
        entry.preview = normalizeTextPreview(text).left(2048);
        addFormat(QStringLiteral("text/plain"), text.toUtf8(), QStringLiteral("typed-text"));
    }

    if (mimeData->hasHtml()) {
        const QString html = mimeData->html();
        if (entry.preview.isEmpty()) {
            entry.preview = htmlToPreview(html).left(200);
        }
        addFormat(QStringLiteral("text/html"), html.toUtf8(), QStringLiteral("typed-html"));
    }

    if (mimeData->hasFormat("text/rtf")) {
        addFormat(QStringLiteral("text/rtf"), mimeData->data("text/rtf"),
                  QStringLiteral("typed-rtf"));
    }
    if (mimeData->hasFormat("application/rtf")) {
        addFormat(QStringLiteral("application/rtf"), mimeData->data("application/rtf"),
                  QStringLiteral("typed-rtf"));
    }

    if (mimeData->hasImage()) {
        const auto variant = mimeData->imageData();
        const QImage image = variant.value<QImage>();
        if (!image.isNull()) {
            bool hasImageMime = false;
            for (const QString &mime : seen) {
                if (mime.startsWith(QStringLiteral("image/"))) {
                    hasImageMime = true;
                    break;
                }
            }

            if (!hasImageMime) {
                QByteArray pngBytes;
                QBuffer buffer(&pngBytes);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "PNG");
                addFormat(QStringLiteral("image/png"), pngBytes,
                          QStringLiteral("typed-image-fallback"));
            }
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
        addFormat(QStringLiteral("text/uri-list"), uriList, QStringLiteral("typed-urls"));
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
        SearchRequest request;
        request.query = params.value(QStringLiteral("query")).toString();
        request.cursor = params.value(QStringLiteral("cursor")).toInteger();
        request.limit = params.value(QStringLiteral("limit")).toInteger();
        request.mode = searchModeFromString(
            params.value(QStringLiteral("mode")).toString());
        request.regexStrict = params.value(QStringLiteral("regex_strict")).toBool();

        const SearchResult result = m_repo.searchEntries(request, &error);
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
        payload.insert(QStringLiteral("query_valid"), result.queryValid);
        payload.insert(QStringLiteral("query_error"), result.queryError);
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

    if (method == "Ping") {
        QCborMap payload;
        payload.insert(QStringLiteral("status"), QStringLiteral("ok"));
        return ipc::makeResponse(id, payload);
    }

    if (method == "GetCapturePolicy") {
        return ipc::makeResponse(id, toCbor(m_capturePolicy));
    }

    if (method == "SetCapturePolicy") {
        CapturePolicy policy;
        if (!capturePolicyFromCbor(params, &policy, &error)) {
            return ipc::makeError(id, error);
        }
        if (!setCapturePolicy(policy, &error)) {
            return ipc::makeError(id, error);
        }
        return ipc::makeResponse(id, toCbor(m_capturePolicy));
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

    if (method == "GetImagePreview") {
        const QString blobHash = params.value("blob_hash").toString();
        if (blobHash.isEmpty()) {
            return ipc::makeError(id, QStringLiteral("blob_hash is required"));
        }

        const int maxEdge = params.value("max_edge").toInteger();
        const QByteArray rawImageBytes = m_repo.loadBlob(blobHash, &error);
        if (!error.isEmpty()) {
            return ipc::makeError(id, error);
        }

        const QByteArray pngBytes = makeImagePreviewPng(rawImageBytes, maxEdge);
        if (pngBytes.isEmpty()) {
            return ipc::makeError(id, QStringLiteral("Failed to decode image blob"));
        }

        QCborMap payload;
        payload.insert(QStringLiteral("mime_type"), QStringLiteral("image/png"));
        payload.insert(QStringLiteral("bytes"), pngBytes);
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

    QVector<FormatDescriptor> orderedFormats = detail.formats;
    if (!preferredFormat.isEmpty()) {
        const QString normalizedPreferred = preferredFormat.trimmed().toLower();
        int preferredIndex = -1;
        for (int i = 0; i < orderedFormats.size(); ++i) {
            if (orderedFormats.at(i).mimeType.compare(normalizedPreferred, Qt::CaseInsensitive) ==
                0) {
                preferredIndex = i;
                break;
            }
        }
        if (preferredIndex < 0) {
            if (error) {
                *error = QStringLiteral("Preferred format unavailable");
            }
            return false;
        }
        if (preferredIndex > 0) {
            const FormatDescriptor preferred = orderedFormats.takeAt(preferredIndex);
            orderedFormats.prepend(preferred);
        }
    }

    for (const auto &format : orderedFormats) {
        QString blobError;
        const QByteArray bytes = m_repo.loadBlob(format.blobHash, &blobError);
        if (!blobError.isEmpty()) {
            if (error) {
                *error = blobError;
            }
            return false;
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

    m_suppressCapture = true;
    m_clipboard->setMimeData(mimeData.release());
    QTimer::singleShot(250, this, [this] { m_suppressCapture = false; });

    return true;
}

bool ClipboardDaemon::validateCapturePolicy(const CapturePolicy &policy,
                                            QString *error) const {
    if (policy.maxFormatBytes < kMinPolicyBytes || policy.maxFormatBytes > kMaxPolicyBytes) {
        if (error) {
            *error = QStringLiteral("max_format_bytes must be between %1 and %2")
                         .arg(kMinPolicyBytes)
                         .arg(kMaxPolicyBytes);
        }
        return false;
    }
    if (policy.maxEntryBytes < kMinPolicyBytes || policy.maxEntryBytes > kMaxPolicyBytes) {
        if (error) {
            *error = QStringLiteral("max_entry_bytes must be between %1 and %2")
                         .arg(kMinPolicyBytes)
                         .arg(kMaxPolicyBytes);
        }
        return false;
    }
    if (policy.maxEntryBytes < policy.maxFormatBytes) {
        if (error) {
            *error =
                QStringLiteral("max_entry_bytes must be >= max_format_bytes");
        }
        return false;
    }

    for (const QString &rawPattern : policy.customAllowlistPatterns) {
        const QString pattern = rawPattern.trimmed();
        if (pattern.isEmpty() || pattern.startsWith('#')) {
            continue;
        }
        if (!pattern.contains('/')) {
            if (error) {
                *error = QStringLiteral("Invalid MIME allowlist pattern: %1").arg(pattern);
            }
            return false;
        }
    }

    return true;
}

bool ClipboardDaemon::setCapturePolicy(const CapturePolicy &policy, QString *error) {
    CapturePolicy normalized = policy;
    normalized.customAllowlistPatterns = normalizedPatterns(policy.customAllowlistPatterns);

    if (!validateCapturePolicy(normalized, error)) {
        return false;
    }

    if (!m_repo.saveCapturePolicy(normalized, error)) {
        return false;
    }

    m_capturePolicy = normalized;
    qCInfo(logClipd) << "Updated capture policy profile"
                     << captureProfileToString(m_capturePolicy.profile)
                     << "maxFormatBytes" << m_capturePolicy.maxFormatBytes
                     << "maxEntryBytes" << m_capturePolicy.maxEntryBytes;
    return true;
}

bool ClipboardDaemon::loadCapturePolicy(QString *error) {
    CapturePolicy loaded;
    if (!m_repo.loadCapturePolicy(&loaded, error)) {
        return false;
    }
    loaded.customAllowlistPatterns = normalizedPatterns(loaded.customAllowlistPatterns);
    if (!validateCapturePolicy(loaded, error)) {
        return false;
    }
    m_capturePolicy = loaded;
    return true;
}

}  // namespace pastetry
