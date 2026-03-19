#include "clip-ui/clipboard_inspector_dialog.h"

#include <QCborArray>
#include <QCborMap>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace pastetry {
namespace {

constexpr int kMaxPayloadBytes = 256 * 1024;

QString yesNo(bool value) {
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

QString sourceModeLabel(ClipboardInspectorDialog::SourceMode mode) {
    switch (mode) {
        case ClipboardInspectorDialog::SourceMode::StoredEntry:
            return QStringLiteral("Stored Item");
        case ClipboardInspectorDialog::SourceMode::Clipboard:
        default:
            return QStringLiteral("Current Clipboard");
    }
}

QString clipboardModeLabel(QClipboard::Mode mode) {
    switch (mode) {
        case QClipboard::Selection:
            return QStringLiteral("Selection");
        case QClipboard::FindBuffer:
            return QStringLiteral("Find Buffer");
        case QClipboard::Clipboard:
        default:
            return QStringLiteral("Clipboard");
    }
}

QString formatTimestamp(qint64 msSinceEpoch) {
    if (msSinceEpoch <= 0) {
        return QStringLiteral("Unknown");
    }

    return QDateTime::fromMSecsSinceEpoch(msSinceEpoch)
        .toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
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

QString decodeTextPayload(const QByteArray &payload) {
    QString text = QString::fromUtf8(payload.constData(), payload.size());
    if (text.isEmpty() && !payload.isEmpty()) {
        text = QString::fromLocal8Bit(payload.constData(), payload.size());
    }
    return text;
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

QImage decodeImagePayload(const QString &mimeType, const QByteArray &payload,
                          const QMimeData *mimeData) {
    const QString lower = mimeType.toLower();
    if (lower.startsWith(QStringLiteral("image/")) && !payload.isEmpty()) {
        const QImage image = QImage::fromData(payload);
        if (!image.isNull()) {
            return image;
        }
    }

    if (lower == QStringLiteral("application/x-qt-image") && mimeData &&
        mimeData->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            return image;
        }
    }

    return {};
}

QStringList parseUriListPreview(const QByteArray &payload) {
    QStringList urls;
    const QList<QByteArray> lines = payload.split('\n');
    for (const QByteArray &lineBytes : lines) {
        const QString line = QString::fromUtf8(lineBytes).trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#'))) {
            urls.push_back(line);
        }
    }
    return urls;
}

}  // namespace

ClipboardInspectorDialog::ClipboardInspectorDialog(IpcClient client, QWidget *parent)
    : QDialog(parent), m_client(std::move(client)),
      m_clipboard(QGuiApplication::clipboard()) {
    setWindowTitle(QStringLiteral("Inspector"));
    resize(1040, 680);

    auto *mainLayout = new QVBoxLayout(this);

    auto *sourceRow = new QHBoxLayout();
    sourceRow->addWidget(new QLabel(QStringLiteral("Source"), this));

    m_sourceModeCombo = new QComboBox(this);
    m_sourceModeCombo->addItem(
        sourceModeLabel(SourceMode::Clipboard),
        static_cast<int>(SourceMode::Clipboard));
    m_sourceModeCombo->addItem(
        sourceModeLabel(SourceMode::StoredEntry),
        static_cast<int>(SourceMode::StoredEntry));
    sourceRow->addWidget(m_sourceModeCombo);

    m_clipboardModeContainer = new QWidget(this);
    auto *clipboardModeLayout = new QHBoxLayout(m_clipboardModeContainer);
    clipboardModeLayout->setContentsMargins(0, 0, 0, 0);
    clipboardModeLayout->addWidget(
        new QLabel(QStringLiteral("Clipboard mode"), m_clipboardModeContainer));

    m_clipboardModeCombo = new QComboBox(m_clipboardModeContainer);
    m_clipboardModeCombo->addItem(QStringLiteral("Clipboard"),
                                  static_cast<int>(QClipboard::Clipboard));
    if (m_clipboard && m_clipboard->supportsSelection()) {
        m_clipboardModeCombo->addItem(QStringLiteral("Selection"),
                                      static_cast<int>(QClipboard::Selection));
    }
    if (m_clipboard && m_clipboard->supportsFindBuffer()) {
        m_clipboardModeCombo->addItem(QStringLiteral("Find Buffer"),
                                      static_cast<int>(QClipboard::FindBuffer));
    }
    clipboardModeLayout->addWidget(m_clipboardModeCombo);
    sourceRow->addWidget(m_clipboardModeContainer);

    sourceRow->addStretch(1);
    mainLayout->addLayout(sourceRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    mainLayout->addWidget(m_summaryLabel);

    m_entryPreviewContainer = new QWidget(this);
    auto *entryPreviewLayout = new QVBoxLayout(m_entryPreviewContainer);
    entryPreviewLayout->setContentsMargins(0, 0, 0, 0);
    entryPreviewLayout->addWidget(
        new QLabel(QStringLiteral("Entry Preview"), m_entryPreviewContainer));
    m_entryPreview = new QPlainTextEdit(m_entryPreviewContainer);
    m_entryPreview->setReadOnly(true);
    m_entryPreview->setFixedHeight(90);
    entryPreviewLayout->addWidget(m_entryPreview);
    mainLayout->addWidget(m_entryPreviewContainer);

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
    detailLayout->addWidget(new QLabel(QStringLiteral("Selected Format Details"),
                                       detailContainer));

    m_detailView = new QPlainTextEdit(detailContainer);
    m_detailView->setReadOnly(true);
    m_detailView->setLineWrapMode(QPlainTextEdit::NoWrap);
    detailLayout->addWidget(m_detailView, 1);

    m_imagePreviewLabel = new QLabel(detailContainer);
    m_imagePreviewLabel->setMinimumHeight(170);
    m_imagePreviewLabel->setAlignment(Qt::AlignCenter);
    m_imagePreviewLabel->setFrameShape(QFrame::StyledPanel);
    detailLayout->addWidget(m_imagePreviewLabel);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton *refreshButton = buttons->addButton(QStringLiteral("Refresh"),
                                                    QDialogButtonBox::ActionRole);
    connect(refreshButton, &QPushButton::clicked, this,
            &ClipboardInspectorDialog::refresh);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    mainLayout->addWidget(buttons);

    connect(m_sourceModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(m_clipboardModeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                if (currentSourceMode() == SourceMode::Clipboard) {
                    refresh();
                }
            });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &ClipboardInspectorDialog::updateSelectedDetails);

    if (m_clipboard) {
        connect(m_clipboard, &QClipboard::changed, this,
                [this](QClipboard::Mode changedMode) {
                    if (!isVisible() ||
                        currentSourceMode() != SourceMode::Clipboard ||
                        changedMode != currentClipboardMode()) {
                        return;
                    }
                    refresh();
                });
    }

    inspectClipboard();
}

ClipboardInspectorDialog::SourceMode ClipboardInspectorDialog::currentSourceMode() const {
    if (!m_sourceModeCombo) {
        return SourceMode::Clipboard;
    }

    const QVariant modeValue = m_sourceModeCombo->currentData();
    if (!modeValue.isValid()) {
        return SourceMode::Clipboard;
    }

    return static_cast<SourceMode>(modeValue.toInt());
}

void ClipboardInspectorDialog::setSourceMode(SourceMode mode) {
    if (!m_sourceModeCombo) {
        return;
    }

    const int targetIndex = m_sourceModeCombo->findData(static_cast<int>(mode));
    if (targetIndex < 0 || targetIndex == m_sourceModeCombo->currentIndex()) {
        return;
    }

    m_sourceModeCombo->blockSignals(true);
    m_sourceModeCombo->setCurrentIndex(targetIndex);
    m_sourceModeCombo->blockSignals(false);
}

void ClipboardInspectorDialog::inspectClipboard() {
    m_entryId = -1;
    setSourceMode(SourceMode::Clipboard);
    refresh();
    show();
    raise();
    activateWindow();
}

void ClipboardInspectorDialog::inspectEntry(qint64 entryId) {
    m_entryId = entryId;
    setSourceMode(SourceMode::StoredEntry);
    refresh();
    show();
    raise();
    activateWindow();
}

void ClipboardInspectorDialog::refresh() {
    if (currentSourceMode() == SourceMode::StoredEntry) {
        m_clipboardModeContainer->setVisible(false);
        m_entryPreviewContainer->setVisible(true);
        m_table->setColumnHidden(3, false);
        refreshStoredEntry();
        return;
    }

    m_clipboardModeContainer->setVisible(true);
    m_entryPreviewContainer->setVisible(false);
    m_table->setColumnHidden(3, true);
    m_entryPreview->clear();
    refreshClipboard();
}

QClipboard::Mode ClipboardInspectorDialog::currentClipboardMode() const {
    if (!m_clipboardModeCombo) {
        return QClipboard::Clipboard;
    }
    const QVariant modeValue = m_clipboardModeCombo->currentData();
    if (!modeValue.isValid()) {
        return QClipboard::Clipboard;
    }
    return static_cast<QClipboard::Mode>(modeValue.toInt());
}

void ClipboardInspectorDialog::clearFormats() {
    m_snapshots.clear();
    m_table->setRowCount(0);
    clearDetails();
}

void ClipboardInspectorDialog::clearDetails() {
    m_detailView->clear();
    m_imagePreviewLabel->clear();
    m_imagePreviewLabel->setText(QStringLiteral("No image preview"));
    m_imagePreviewLabel->setToolTip(QString());
}

void ClipboardInspectorDialog::refreshClipboard() {
    clearFormats();
    setWindowTitle(QStringLiteral("Inspector - Current Clipboard"));

    if (!m_clipboard) {
        m_summaryLabel->setText(
            QStringLiteral("Clipboard backend is unavailable in this session."));
        return;
    }

    const QClipboard::Mode mode = currentClipboardMode();
    const QMimeData *mimeData = m_clipboard->mimeData(mode);
    updateSummaryLabelClipboard(mimeData, mode);

    if (!mimeData) {
        return;
    }

    const QStringList formats = mimeData->formats();
    m_table->setRowCount(formats.size());
    m_snapshots.reserve(formats.size());
    for (int row = 0; row < formats.size(); ++row) {
        FormatSnapshot snapshot = buildSnapshotFromClipboard(formats.at(row), mimeData);
        appendSnapshotRow(row, snapshot);
        m_snapshots.push_back(snapshot);
    }

    if (!m_snapshots.isEmpty()) {
        m_table->selectRow(0);
    }
}

void ClipboardInspectorDialog::refreshStoredEntry() {
    clearFormats();

    if (m_entryId <= 0) {
        m_summaryLabel->setText(QStringLiteral("No entry selected"));
        setWindowTitle(QStringLiteral("Inspector - Stored Item"));
        return;
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), m_entryId);
    const QCborMap detail =
        m_client.request(QStringLiteral("GetEntryDetail"), params, 2500, &error);
    if (!error.isEmpty()) {
        setWindowTitle(QStringLiteral("Inspector - Stored Item"));
        m_summaryLabel->setText(
            QStringLiteral("Failed to load entry #%1: %2").arg(m_entryId).arg(error));
        return;
    }

    const QCborArray formats = detail.value(QStringLiteral("formats")).toArray();
    updateSummaryLabelStoredEntry(detail, formats.size());
    m_entryPreview->setPlainText(detail.value(QStringLiteral("preview")).toString());

    setWindowTitle(QStringLiteral("Inspector - Entry #%1").arg(m_entryId));

    m_table->setRowCount(formats.size());
    m_snapshots.reserve(formats.size());
    for (int row = 0; row < formats.size(); ++row) {
        const QCborMap formatMap = formats.at(row).toMap();
        FormatSnapshot snapshot;
        snapshot.mimeType = formatMap.value(QStringLiteral("mime_type")).toString();
        snapshot.byteSize = formatMap.value(QStringLiteral("byte_size")).toInteger();
        snapshot.blobHash = formatMap.value(QStringLiteral("blob_hash")).toString();
        snapshot.kind = payloadKind(snapshot.mimeType, {});

        appendSnapshotRow(row, snapshot);
        m_snapshots.push_back(snapshot);
    }

    if (!m_snapshots.isEmpty()) {
        m_table->selectRow(0);
    }
}

void ClipboardInspectorDialog::updateSummaryLabelClipboard(const QMimeData *mimeData,
                                                           QClipboard::Mode mode) {
    if (!mimeData) {
        m_summaryLabel->setText(
            QStringLiteral("Source: %1 | Clipboard mode: %2 | No data")
                .arg(sourceModeLabel(SourceMode::Clipboard))
                .arg(clipboardModeLabel(mode)));
        return;
    }

    m_summaryLabel->setText(
        QStringLiteral("Source: %1 | Clipboard mode: %2 | formats: %3 | text: %4 | html: %5 | image: %6 | urls: %7 | color: %8")
            .arg(sourceModeLabel(SourceMode::Clipboard))
            .arg(clipboardModeLabel(mode))
            .arg(mimeData->formats().size())
            .arg(yesNo(mimeData->hasText()))
            .arg(yesNo(mimeData->hasHtml()))
            .arg(yesNo(mimeData->hasImage()))
            .arg(yesNo(mimeData->hasUrls()))
            .arg(yesNo(mimeData->hasColor())));
}

void ClipboardInspectorDialog::updateSummaryLabelStoredEntry(const QCborMap &detail,
                                                             int formatCount) {
    const qint64 id = detail.value(QStringLiteral("id")).toInteger();
    const qint64 createdAtMs = detail.value(QStringLiteral("created_at_ms")).toInteger();
    const QString sourceApp = detail.value(QStringLiteral("source_app")).toString().trimmed();
    const QString sourceWindow =
        detail.value(QStringLiteral("source_window")).toString().trimmed();
    const bool pinned = detail.value(QStringLiteral("pinned")).toBool();

    m_summaryLabel->setText(
        QStringLiteral(
            "Source: %1 | Entry: #%2 | Created: %3 | Source app: %4 | Source window: %5 | Pinned: %6 | Formats: %7")
            .arg(sourceModeLabel(SourceMode::StoredEntry))
            .arg(id)
            .arg(formatTimestamp(createdAtMs))
            .arg(sourceApp.isEmpty() ? QStringLiteral("Unknown") : sourceApp)
            .arg(sourceWindow.isEmpty() ? QStringLiteral("Unknown") : sourceWindow)
            .arg(yesNo(pinned))
            .arg(formatCount));
}

ClipboardInspectorDialog::FormatSnapshot
ClipboardInspectorDialog::buildSnapshotFromClipboard(const QString &mimeType,
                                                     const QMimeData *mimeData) const {
    FormatSnapshot snapshot;
    snapshot.mimeType = mimeType;
    if (mimeData) {
        snapshot.payload = mimeData->data(mimeType);
    }
    snapshot.byteSize = snapshot.payload.size();
    snapshot.payloadFetched = true;
    snapshot.payloadOriginalSize = snapshot.payload.size();
    snapshot.payloadTruncated = false;
    snapshot.imagePreview = decodeImagePayload(mimeType, snapshot.payload, mimeData);

    populateSnapshotFromPayload(&snapshot);
    return snapshot;
}

void ClipboardInspectorDialog::populateSnapshotFromPayload(
    FormatSnapshot *snapshot) const {
    if (!snapshot) {
        return;
    }

    snapshot->kind = payloadKind(snapshot->mimeType, snapshot->payload);
    snapshot->textPreview.clear();
    snapshot->urlPreview.clear();
    snapshot->textTruncated = false;

    if (snapshot->mimeType.compare(QStringLiteral("text/uri-list"),
                                   Qt::CaseInsensitive) == 0) {
        snapshot->urlPreview = parseUriListPreview(snapshot->payload);
    }

    if (snapshot->kind == QLatin1String("Text") ||
        snapshot->kind == QLatin1String("Text-like") ||
        snapshot->kind == QLatin1String("HTML")) {
        QString text = decodeTextPayload(snapshot->payload);
        if (text.size() > 8000) {
            snapshot->textPreview = text.left(8000);
            snapshot->textTruncated = true;
        } else {
            snapshot->textPreview = text;
        }
    }

    if (snapshot->imagePreview.isNull() &&
        snapshot->kind == QLatin1String("Image")) {
        snapshot->imagePreview =
            decodeImagePayload(snapshot->mimeType, snapshot->payload, nullptr);
    }
}

bool ClipboardInspectorDialog::ensureStoredPayloadLoaded(int index, QString *error) {
    if (error) {
        error->clear();
    }

    if (index < 0 || index >= m_snapshots.size()) {
        if (error) {
            *error = QStringLiteral("Invalid format index");
        }
        return false;
    }

    FormatSnapshot &snapshot = m_snapshots[index];
    if (snapshot.payloadFetched) {
        return true;
    }

    bool truncated = false;
    qint64 originalSize = 0;
    QString requestError;
    const QByteArray payload =
        requestStoredPayload(snapshot, &requestError, &truncated, &originalSize);
    if (!requestError.isEmpty()) {
        if (error) {
            *error = requestError;
        }
        return false;
    }

    snapshot.payloadFetched = true;
    snapshot.payload = payload;
    snapshot.payloadTruncated = truncated;
    snapshot.payloadOriginalSize = originalSize;
    if (snapshot.byteSize <= 0) {
        snapshot.byteSize = originalSize;
    }

    populateSnapshotFromPayload(&snapshot);
    if (snapshot.kind == QLatin1String("Image") && !snapshot.blobHash.isEmpty() &&
        snapshot.imagePreview.isNull()) {
        QString imageError;
        snapshot.imagePreview = requestStoredImagePreview(snapshot.blobHash, &imageError);
        Q_UNUSED(imageError);
    }

    return true;
}

QByteArray ClipboardInspectorDialog::requestStoredPayload(
    const FormatSnapshot &snapshot, QString *error, bool *truncated,
    qint64 *originalSize) const {
    if (error) {
        error->clear();
    }
    if (truncated) {
        *truncated = false;
    }
    if (originalSize) {
        *originalSize = 0;
    }

    QCborMap params;
    params.insert(QStringLiteral("blob_hash"), snapshot.blobHash);
    params.insert(QStringLiteral("mime_type"), snapshot.mimeType);
    params.insert(QStringLiteral("max_bytes"), kMaxPayloadBytes);

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

QImage ClipboardInspectorDialog::requestStoredImagePreview(const QString &blobHash,
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
            *error = QStringLiteral("Empty image payload");
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

    return image;
}

void ClipboardInspectorDialog::appendSnapshotRow(int row,
                                                 const FormatSnapshot &snapshot) {
    auto *mimeItem = new QTableWidgetItem(snapshot.mimeType);
    auto *sizeItem = new QTableWidgetItem(
        QLocale().formattedDataSize(qMax<qint64>(0, snapshot.byteSize)));
    auto *kindItem = new QTableWidgetItem(snapshot.kind);
    auto *hashItem = new QTableWidgetItem(snapshot.blobHash);

    mimeItem->setToolTip(snapshot.mimeType);
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hashItem->setToolTip(snapshot.blobHash);

    m_table->setItem(row, 0, mimeItem);
    m_table->setItem(row, 1, sizeItem);
    m_table->setItem(row, 2, kindItem);
    m_table->setItem(row, 3, hashItem);
}

void ClipboardInspectorDialog::updateSelectedDetails() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_snapshots.size()) {
        clearDetails();
        return;
    }

    QString payloadError;
    if (currentSourceMode() == SourceMode::StoredEntry &&
        !ensureStoredPayloadLoaded(row, &payloadError)) {
        clearDetails();
        m_detailView->setPlainText(
            QStringLiteral("Failed to load format payload:\n%1").arg(payloadError));
        return;
    }

    FormatSnapshot &snapshot = m_snapshots[row];
    if (currentSourceMode() == SourceMode::StoredEntry) {
        if (auto *kindItem = m_table->item(row, 2)) {
            kindItem->setText(snapshot.kind);
        }
    }

    QStringList detail;
    detail.push_back(QStringLiteral("MIME Type: %1").arg(snapshot.mimeType));
    detail.push_back(QStringLiteral("Kind: %1").arg(snapshot.kind));
    detail.push_back(
        QStringLiteral("Bytes: %1 (%2)")
            .arg(snapshot.byteSize)
            .arg(QLocale().formattedDataSize(qMax<qint64>(0, snapshot.byteSize))));

    if (currentSourceMode() == SourceMode::StoredEntry) {
        detail.push_back(QStringLiteral("Blob hash: %1").arg(snapshot.blobHash));
        if (snapshot.payloadFetched) {
            detail.push_back(
                QStringLiteral("Loaded payload: %1 bytes")
                    .arg(snapshot.payload.size()));
        }
        if (snapshot.payloadTruncated) {
            detail.push_back(
                QStringLiteral("Payload truncated for inspector (original: %1 bytes)")
                    .arg(snapshot.payloadOriginalSize));
        }
    }

    detail.push_back(QString());

    if (!snapshot.urlPreview.isEmpty()) {
        detail.push_back(QStringLiteral("URLs (%1):").arg(snapshot.urlPreview.size()));
        for (const QString &url : snapshot.urlPreview) {
            detail.push_back(url);
        }
        detail.push_back(QString());
    }

    if (!snapshot.textPreview.isEmpty()) {
        detail.push_back(QStringLiteral("Text preview:"));
        detail.push_back(snapshot.textPreview);
        if (snapshot.textTruncated) {
            detail.push_back(QStringLiteral("\n[truncated to 8000 characters]"));
        }
    } else if (!snapshot.payload.isEmpty()) {
        detail.push_back(QStringLiteral("Hex preview:"));
        detail.push_back(hexPreview(snapshot.payload));
    }

    m_detailView->setPlainText(detail.join('\n'));

    if (!snapshot.imagePreview.isNull()) {
        const QSize maxSize(460, 240);
        const QPixmap pixmap = QPixmap::fromImage(snapshot.imagePreview);
        const QPixmap scaled = pixmap.scaled(maxSize, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
        m_imagePreviewLabel->setPixmap(scaled);
        m_imagePreviewLabel->setToolTip(
            QStringLiteral("%1 x %2")
                .arg(snapshot.imagePreview.width())
                .arg(snapshot.imagePreview.height()));
        return;
    }

    m_imagePreviewLabel->clear();
    m_imagePreviewLabel->setText(QStringLiteral("No image preview"));
    m_imagePreviewLabel->setToolTip(QString());
}

}  // namespace pastetry
