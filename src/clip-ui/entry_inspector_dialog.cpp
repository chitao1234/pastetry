#include "clip-ui/entry_inspector_dialog.h"

#include <QCborArray>
#include <QCborMap>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace pastetry {
namespace {

QString yesNo(bool value) {
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

bool isLikelyTextPayload(const QByteArray &payload) {
    if (payload.isEmpty()) {
        return true;
    }

    int disallowedControlCount = 0;
    for (char ch : payload) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (byte == 0) {
            return false;
        }
        if (byte < 0x20 && byte != '\n' && byte != '\r' && byte != '\t') {
            ++disallowedControlCount;
            if (disallowedControlCount > 4) {
                return false;
            }
        }
    }

    return true;
}

bool looksTextualMime(const QString &mimeType) {
    const QString lower = mimeType.toLower();
    return lower.startsWith(QStringLiteral("text/")) ||
           lower.contains(QStringLiteral("json")) ||
           lower.contains(QStringLiteral("xml")) ||
           lower.contains(QStringLiteral("yaml")) ||
           lower.contains(QStringLiteral("javascript")) ||
           lower.endsWith(QStringLiteral("+json")) ||
           lower.endsWith(QStringLiteral("+xml")) ||
           lower.contains(QStringLiteral("rtf"));
}

QString payloadKind(const QString &mimeType, const QByteArray &payload) {
    const QString lower = mimeType.toLower();
    if (lower == QStringLiteral("text/uri-list")) {
        return QStringLiteral("URLs");
    }
    if (lower == QStringLiteral("text/html")) {
        return QStringLiteral("HTML");
    }
    if (lower.startsWith(QStringLiteral("image/")) ||
        lower == QStringLiteral("application/x-qt-image")) {
        return QStringLiteral("Image");
    }
    if (looksTextualMime(lower)) {
        return QStringLiteral("Text");
    }
    if (payload.isEmpty()) {
        return QStringLiteral("Empty");
    }
    if (isLikelyTextPayload(payload)) {
        return QStringLiteral("Text-like");
    }
    return QStringLiteral("Binary");
}

QString hexPreview(const QByteArray &payload, int maxBytes = 128) {
    const QByteArray clipped = payload.left(maxBytes);
    QStringList chunks;
    chunks.reserve(clipped.size());
    for (char ch : clipped) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        chunks.push_back(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')));
    }
    QString result = chunks.join(' ');
    if (payload.size() > maxBytes) {
        result.append(QStringLiteral(" ..."));
    }
    return result;
}

QString decodeTextPayload(const QByteArray &payload) {
    QString text = QString::fromUtf8(payload.constData(), payload.size());
    if (text.isEmpty() && !payload.isEmpty()) {
        text = QString::fromLocal8Bit(payload.constData(), payload.size());
    }
    return text;
}

QString formatTimestamp(qint64 msSinceEpoch) {
    if (msSinceEpoch <= 0) {
        return QStringLiteral("Unknown");
    }

    return QDateTime::fromMSecsSinceEpoch(msSinceEpoch)
        .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
}

}  // namespace

EntryInspectorDialog::EntryInspectorDialog(IpcClient client, QWidget *parent)
    : QDialog(parent), m_client(std::move(client)) {
    setWindowTitle(QStringLiteral("Item Inspector"));
    resize(1020, 650);

    auto *mainLayout = new QVBoxLayout(this);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    mainLayout->addWidget(m_summaryLabel);

    auto *previewLabel = new QLabel(QStringLiteral("Entry Preview"), this);
    mainLayout->addWidget(previewLabel);

    m_entryPreview = new QPlainTextEdit(this);
    m_entryPreview->setReadOnly(true);
    m_entryPreview->setFixedHeight(90);
    mainLayout->addWidget(m_entryPreview);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({QStringLiteral("MIME Type"),
                                        QStringLiteral("Size"),
                                        QStringLiteral("Kind"),
                                        QStringLiteral("Blob Hash")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    auto *detailContainer = new QWidget(splitter);
    auto *detailLayout = new QVBoxLayout(detailContainer);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->addWidget(
        new QLabel(QStringLiteral("Selected Format Details"), detailContainer));

    m_detailView = new QPlainTextEdit(detailContainer);
    m_detailView->setReadOnly(true);
    m_detailView->setLineWrapMode(QPlainTextEdit::NoWrap);
    detailLayout->addWidget(m_detailView, 1);

    m_imagePreviewLabel = new QLabel(detailContainer);
    m_imagePreviewLabel->setMinimumHeight(180);
    m_imagePreviewLabel->setAlignment(Qt::AlignCenter);
    m_imagePreviewLabel->setFrameShape(QFrame::StyledPanel);
    detailLayout->addWidget(m_imagePreviewLabel);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *refreshButton =
        buttons->addButton(QStringLiteral("Refresh"), QDialogButtonBox::ActionRole);
    connect(refreshButton, &QPushButton::clicked, this, &EntryInspectorDialog::refresh);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(buttons);

    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &EntryInspectorDialog::updateSelectedFormatDetails);

    clearEntry();
}

void EntryInspectorDialog::inspectEntry(qint64 entryId) {
    m_entryId = entryId;
    setWindowTitle(QStringLiteral("Item Inspector - Entry #%1").arg(entryId));
    show();
    raise();
    activateWindow();
    refresh();
}

void EntryInspectorDialog::refresh() {
    loadEntryDetail();
}

void EntryInspectorDialog::loadEntryDetail() {
    clearEntry();
    if (m_entryId <= 0) {
        m_summaryLabel->setText(QStringLiteral("No entry selected"));
        return;
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), m_entryId);
    const QCborMap detail =
        m_client.request(QStringLiteral("GetEntryDetail"), params, 2500, &error);
    if (!error.isEmpty()) {
        m_summaryLabel->setText(
            QStringLiteral("Failed to load entry #%1: %2").arg(m_entryId).arg(error));
        return;
    }

    const qint64 createdAtMs = detail.value(QStringLiteral("created_at_ms")).toInteger();
    const QString sourceApp = detail.value(QStringLiteral("source_app")).toString().trimmed();
    const QString sourceWindow =
        detail.value(QStringLiteral("source_window")).toString().trimmed();
    const bool pinned = detail.value(QStringLiteral("pinned")).toBool();
    const QString preview = detail.value(QStringLiteral("preview")).toString();
    const QCborArray formats = detail.value(QStringLiteral("formats")).toArray();

    m_summaryLabel->setText(
        QStringLiteral(
            "Entry: #%1 | Created: %2 | Source app: %3 | Source window: %4 | Pinned: %5 | Formats: %6")
            .arg(m_entryId)
            .arg(formatTimestamp(createdAtMs))
            .arg(sourceApp.isEmpty() ? QStringLiteral("Unknown") : sourceApp)
            .arg(sourceWindow.isEmpty() ? QStringLiteral("Unknown") : sourceWindow)
            .arg(yesNo(pinned))
            .arg(formats.size()));
    m_entryPreview->setPlainText(preview);

    m_table->setRowCount(formats.size());
    m_formats.reserve(formats.size());
    for (int row = 0; row < formats.size(); ++row) {
        const QCborMap formatMap = formats.at(row).toMap();
        FormatSnapshot snapshot;
        snapshot.mimeType = formatMap.value(QStringLiteral("mime_type")).toString();
        snapshot.byteSize = formatMap.value(QStringLiteral("byte_size")).toInteger();
        snapshot.blobHash = formatMap.value(QStringLiteral("blob_hash")).toString();

        m_formats.push_back(snapshot);

        auto *mimeItem = new QTableWidgetItem(snapshot.mimeType);
        auto *sizeItem = new QTableWidgetItem(
            QLocale().formattedDataSize(qMax<qint64>(0, snapshot.byteSize)));
        auto *kindItem = new QTableWidgetItem(payloadKind(snapshot.mimeType, {}));
        auto *hashItem = new QTableWidgetItem(snapshot.blobHash);

        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hashItem->setToolTip(snapshot.blobHash);

        m_table->setItem(row, 0, mimeItem);
        m_table->setItem(row, 1, sizeItem);
        m_table->setItem(row, 2, kindItem);
        m_table->setItem(row, 3, hashItem);
    }

    if (!m_formats.isEmpty()) {
        m_table->selectRow(0);
    }
}

void EntryInspectorDialog::clearEntry() {
    m_formats.clear();
    m_table->setRowCount(0);
    m_entryPreview->clear();
    clearFormatDetails();
}

void EntryInspectorDialog::clearFormatDetails() {
    m_detailView->clear();
    m_imagePreviewLabel->clear();
    m_imagePreviewLabel->setText(QStringLiteral("No image preview"));
    m_imagePreviewLabel->setToolTip(QString());
}

bool EntryInspectorDialog::ensureFormatPayloadLoaded(int index, QString *error) {
    if (index < 0 || index >= m_formats.size()) {
        if (error) {
            *error = QStringLiteral("Invalid format index");
        }
        return false;
    }

    FormatSnapshot &snapshot = m_formats[index];
    if (snapshot.payloadFetched) {
        return true;
    }

    QString requestError;
    bool truncated = false;
    qint64 originalSize = 0;
    const QByteArray bytes = formatPayloadRequest(snapshot, &requestError, &truncated,
                                                  &originalSize);
    if (!requestError.isEmpty()) {
        if (error) {
            *error = requestError;
        }
        return false;
    }

    snapshot.payloadFetched = true;
    snapshot.payload = bytes;
    snapshot.payloadTruncated = truncated;
    snapshot.payloadOriginalSize = originalSize;
    return true;
}

QByteArray EntryInspectorDialog::formatPayloadRequest(const FormatSnapshot &snapshot,
                                                      QString *error,
                                                      bool *truncated,
                                                      qint64 *originalSize) const {
    if (truncated) {
        *truncated = false;
    }
    if (originalSize) {
        *originalSize = 0;
    }
    if (error) {
        error->clear();
    }

    QCborMap params;
    params.insert(QStringLiteral("blob_hash"), snapshot.blobHash);
    params.insert(QStringLiteral("mime_type"), snapshot.mimeType);
    params.insert(QStringLiteral("max_bytes"), 256 * 1024);

    QString requestError;
    const QCborMap result =
        m_client.request(QStringLiteral("GetFormatPayload"), params, 3000, &requestError);
    if (!requestError.isEmpty()) {
        if (error) {
            *error = requestError;
        }
        return {};
    }

    if (truncated) {
        *truncated = result.value(QStringLiteral("truncated")).toBool();
    }
    if (originalSize) {
        *originalSize = result.value(QStringLiteral("original_size")).toInteger();
    }
    return result.value(QStringLiteral("bytes")).toByteArray();
}

QPixmap EntryInspectorDialog::imagePreviewRequest(const QString &blobHash,
                                                  QString *error) const {
    if (error) {
        error->clear();
    }

    QCborMap params;
    params.insert(QStringLiteral("blob_hash"), blobHash);
    params.insert(QStringLiteral("max_edge"), 420);

    QString requestError;
    const QCborMap result =
        m_client.request(QStringLiteral("GetImagePreview"), params, 2000, &requestError);
    if (!requestError.isEmpty()) {
        if (error) {
            *error = requestError;
        }
        return {};
    }

    const QByteArray bytes = result.value(QStringLiteral("bytes")).toByteArray();
    if (bytes.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Empty image preview payload");
        }
        return {};
    }

    const QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("Failed to decode image preview");
        }
        return {};
    }

    return QPixmap::fromImage(image);
}

void EntryInspectorDialog::updateSelectedFormatDetails() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_formats.size()) {
        clearFormatDetails();
        return;
    }

    QString payloadError;
    if (!ensureFormatPayloadLoaded(row, &payloadError)) {
        clearFormatDetails();
        m_detailView->setPlainText(
            QStringLiteral("Failed to load format payload:\n%1").arg(payloadError));
        return;
    }

    const FormatSnapshot &snapshot = m_formats.at(row);
    const QString kind = payloadKind(snapshot.mimeType, snapshot.payload);

    QStringList detail;
    detail.push_back(QStringLiteral("MIME Type: %1").arg(snapshot.mimeType));
    detail.push_back(QStringLiteral("Kind: %1").arg(kind));
    detail.push_back(
        QStringLiteral("Stored size: %1 (%2)")
            .arg(snapshot.byteSize)
            .arg(QLocale().formattedDataSize(qMax<qint64>(0, snapshot.byteSize))));
    if (snapshot.payloadFetched) {
        detail.push_back(
            QStringLiteral("Payload loaded: %1 bytes (%2)")
                .arg(snapshot.payload.size())
                .arg(QLocale().formattedDataSize(qMax<qint64>(0, snapshot.payload.size()))));
    }
    if (snapshot.payloadTruncated) {
        detail.push_back(
            QStringLiteral("Payload truncated for inspector (original: %1 bytes)")
                .arg(snapshot.payloadOriginalSize));
    }
    detail.push_back(QStringLiteral("Blob hash: %1").arg(snapshot.blobHash));
    detail.push_back(QString());

    if (kind == QLatin1String("URLs")) {
        detail.push_back(QStringLiteral("URI list:"));
        const QList<QByteArray> lines = snapshot.payload.split('\n');
        for (const QByteArray &lineBytes : lines) {
            const QString line = QString::fromUtf8(lineBytes).trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
                detail.push_back(line);
            }
        }
    } else if (kind == QLatin1String("Text") || kind == QLatin1String("Text-like") ||
               kind == QLatin1String("HTML")) {
        QString text = decodeTextPayload(snapshot.payload);
        if (text.size() > 8000) {
            text = text.left(8000);
            detail.push_back(QStringLiteral("[Text truncated to 8000 characters]"));
        }
        detail.push_back(QStringLiteral("Text preview:"));
        detail.push_back(text);
    } else if (!snapshot.payload.isEmpty()) {
        detail.push_back(QStringLiteral("Hex preview:"));
        detail.push_back(hexPreview(snapshot.payload));
    }

    m_detailView->setPlainText(detail.join('\n'));

    if (kind == QLatin1String("Image")) {
        QString imageError;
        const QPixmap pixmap = imagePreviewRequest(snapshot.blobHash, &imageError);
        if (!pixmap.isNull()) {
            const QSize maxSize(460, 240);
            const QPixmap scaled =
                pixmap.scaled(maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            m_imagePreviewLabel->setPixmap(scaled);
            m_imagePreviewLabel->setToolTip(
                QStringLiteral("%1 x %2").arg(pixmap.width()).arg(pixmap.height()));
            return;
        }

        m_imagePreviewLabel->clear();
        m_imagePreviewLabel->setText(
            QStringLiteral("Image preview unavailable: %1")
                .arg(imageError.isEmpty() ? QStringLiteral("Unknown error")
                                          : imageError));
        m_imagePreviewLabel->setToolTip(QString());
        return;
    }

    m_imagePreviewLabel->clear();
    m_imagePreviewLabel->setText(QStringLiteral("No image preview"));
    m_imagePreviewLabel->setToolTip(QString());
}

}  // namespace pastetry
