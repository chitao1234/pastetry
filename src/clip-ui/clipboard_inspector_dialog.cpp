#include "clip-ui/clipboard_inspector_dialog.h"

#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
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

QString yesNo(bool value) {
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

QString modeLabel(QClipboard::Mode mode) {
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

QString hexPreview(const QByteArray &payload, int maxBytes = 96) {
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

QString mimeKind(const QString &mimeType, const QByteArray &payload) {
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

}  // namespace

ClipboardInspectorDialog::ClipboardInspectorDialog(QWidget *parent)
    : QDialog(parent), m_clipboard(QGuiApplication::clipboard()) {
    setWindowTitle(QStringLiteral("Clipboard Inspector"));
    resize(980, 620);

    auto *mainLayout = new QVBoxLayout(this);

    auto *modeRow = new QHBoxLayout();
    auto *modeLabelWidget = new QLabel(QStringLiteral("Clipboard mode"), this);
    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(QStringLiteral("Clipboard"),
                         static_cast<int>(QClipboard::Clipboard));
    if (m_clipboard && m_clipboard->supportsSelection()) {
        m_modeCombo->addItem(QStringLiteral("Selection"),
                             static_cast<int>(QClipboard::Selection));
    }
    if (m_clipboard && m_clipboard->supportsFindBuffer()) {
        m_modeCombo->addItem(QStringLiteral("Find Buffer"),
                             static_cast<int>(QClipboard::FindBuffer));
    }
    modeRow->addWidget(modeLabelWidget);
    modeRow->addWidget(m_modeCombo);
    modeRow->addStretch(1);
    mainLayout->addLayout(modeRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    mainLayout->addWidget(m_summaryLabel);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("MIME Type"), QStringLiteral("Size"), QStringLiteral("Kind")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

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

    connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { refresh(); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &ClipboardInspectorDialog::updateSelectedDetails);
    if (m_clipboard) {
        connect(m_clipboard, &QClipboard::changed, this,
                [this](QClipboard::Mode changedMode) {
                    if (isVisible() && changedMode == currentMode()) {
                        refresh();
                    }
                });
    }

    refresh();
}

QClipboard::Mode ClipboardInspectorDialog::currentMode() const {
    if (!m_modeCombo) {
        return QClipboard::Clipboard;
    }
    const QVariant modeValue = m_modeCombo->currentData();
    if (!modeValue.isValid()) {
        return QClipboard::Clipboard;
    }
    return static_cast<QClipboard::Mode>(modeValue.toInt());
}

void ClipboardInspectorDialog::refresh() {
    m_snapshots.clear();
    m_table->setRowCount(0);
    clearDetails();

    if (!m_clipboard) {
        m_summaryLabel->setText(
            QStringLiteral("Clipboard backend is unavailable in this session."));
        return;
    }

    const QClipboard::Mode mode = currentMode();
    const QMimeData *mimeData = m_clipboard->mimeData(mode);
    updateSummaryLabel(mimeData, mode);

    if (!mimeData) {
        return;
    }

    const QStringList formats = mimeData->formats();
    m_table->setRowCount(formats.size());
    m_snapshots.reserve(formats.size());
    for (int row = 0; row < formats.size(); ++row) {
        const FormatSnapshot snapshot = buildSnapshot(formats.at(row), mimeData);
        appendSnapshotRow(row, snapshot);
        m_snapshots.push_back(snapshot);
    }

    if (!m_snapshots.isEmpty()) {
        m_table->selectRow(0);
    }
}

void ClipboardInspectorDialog::updateSummaryLabel(const QMimeData *mimeData,
                                                  QClipboard::Mode mode) {
    if (!mimeData) {
        m_summaryLabel->setText(
            QStringLiteral("Mode: %1 | No data in clipboard")
                .arg(modeLabel(mode)));
        return;
    }

    m_summaryLabel->setText(
        QStringLiteral("Mode: %1 | formats: %2 | text: %3 | html: %4 | image: %5 | urls: %6 | color: %7")
            .arg(modeLabel(mode))
            .arg(mimeData->formats().size())
            .arg(yesNo(mimeData->hasText()))
            .arg(yesNo(mimeData->hasHtml()))
            .arg(yesNo(mimeData->hasImage()))
            .arg(yesNo(mimeData->hasUrls()))
            .arg(yesNo(mimeData->hasColor())));
}

void ClipboardInspectorDialog::clearDetails() {
    m_detailView->clear();
    m_imagePreviewLabel->clear();
    m_imagePreviewLabel->setText(QStringLiteral("No image preview"));
}

ClipboardInspectorDialog::FormatSnapshot ClipboardInspectorDialog::buildSnapshot(
    const QString &mimeType, const QMimeData *mimeData) const {
    FormatSnapshot snapshot;
    snapshot.mimeType = mimeType;
    if (mimeData) {
        snapshot.payload = mimeData->data(mimeType);
    }
    snapshot.kind = mimeKind(mimeType, snapshot.payload);

    if (mimeType.compare(QStringLiteral("text/uri-list"), Qt::CaseInsensitive) == 0 &&
        mimeData && mimeData->hasUrls()) {
        const auto urls = mimeData->urls();
        snapshot.urlPreview.reserve(urls.size());
        for (const QUrl &url : urls) {
            snapshot.urlPreview.push_back(url.toString());
        }
    }

    if (snapshot.kind == QLatin1String("Text") ||
        snapshot.kind == QLatin1String("Text-like") ||
        snapshot.kind == QLatin1String("HTML")) {
        QString text = QString::fromUtf8(snapshot.payload.constData(), snapshot.payload.size());
        if (text.isEmpty() && !snapshot.payload.isEmpty()) {
            text = QString::fromLocal8Bit(snapshot.payload.constData(),
                                          snapshot.payload.size());
        }
        if (text.size() > 8000) {
            snapshot.textPreview = text.left(8000);
            snapshot.textTruncated = true;
        } else {
            snapshot.textPreview = text;
        }
    }

    snapshot.imagePreview = decodeImagePayload(mimeType, snapshot.payload, mimeData);
    return snapshot;
}

void ClipboardInspectorDialog::appendSnapshotRow(int row,
                                                 const FormatSnapshot &snapshot) {
    auto *mimeItem = new QTableWidgetItem(snapshot.mimeType);
    auto *sizeItem = new QTableWidgetItem(
        QLocale().formattedDataSize(qMax<qint64>(0, snapshot.payload.size())));
    auto *kindItem = new QTableWidgetItem(snapshot.kind);

    mimeItem->setToolTip(snapshot.mimeType);
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_table->setItem(row, 0, mimeItem);
    m_table->setItem(row, 1, sizeItem);
    m_table->setItem(row, 2, kindItem);
}

void ClipboardInspectorDialog::updateSelectedDetails() {
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_snapshots.size()) {
        clearDetails();
        return;
    }

    const FormatSnapshot &snapshot = m_snapshots.at(row);
    QStringList detail;
    detail.push_back(QStringLiteral("MIME Type: %1").arg(snapshot.mimeType));
    detail.push_back(QStringLiteral("Kind: %1").arg(snapshot.kind));
    detail.push_back(
        QStringLiteral("Bytes: %1 (%2)")
            .arg(snapshot.payload.size())
            .arg(QLocale().formattedDataSize(qMax<qint64>(0, snapshot.payload.size()))));
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
        const QSize maxSize(420, 220);
        const QPixmap pixmap = QPixmap::fromImage(snapshot.imagePreview);
        const QPixmap scaled = pixmap.scaled(maxSize, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
        m_imagePreviewLabel->setPixmap(scaled);
        m_imagePreviewLabel->setToolTip(
            QStringLiteral("%1 x %2")
                .arg(snapshot.imagePreview.width())
                .arg(snapshot.imagePreview.height()));
    } else {
        m_imagePreviewLabel->clear();
        m_imagePreviewLabel->setText(QStringLiteral("No image preview"));
        m_imagePreviewLabel->setToolTip(QString());
    }
}

}  // namespace pastetry
