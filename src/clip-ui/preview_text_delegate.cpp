#include "clip-ui/preview_text_delegate.h"

#include "clip-ui/history_model.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTextLayout>

namespace pastetry {

PreviewTextDelegate::PreviewTextDelegate(IpcClient client, QObject *parent)
    : QStyledItemDelegate(parent), m_client(std::move(client)) {}

void PreviewTextDelegate::setMaxLines(int maxLines) {
    m_maxLines = qBound(1, maxLines, 12);
}

int PreviewTextDelegate::maxLines() const {
    return m_maxLines;
}

QSize PreviewTextDelegate::sizeHint(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const QSize base = QStyledItemDelegate::sizeHint(opt, index);
    const QFontMetrics fm(opt.font);
    const int desiredTextHeight = fm.lineSpacing() * m_maxLines;
    const int inferredPadding = qMax(6, base.height() - fm.height());

    return QSize(base.width(), desiredTextHeight + inferredPadding);
}

QStringList PreviewTextDelegate::wrappedLines(const QString &text, const QFont &font,
                                             int maxWidth) const {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar('\r'), QChar('\n'));

    QTextOption textOption;
    textOption.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);

    QTextLayout layout(normalized, font);
    layout.setTextOption(textOption);

    QStringList lines;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
            break;
        }

        line.setLineWidth(maxWidth);
        QString lineText =
            normalized.mid(line.textStart(), line.textLength()).trimmed();
        if (lineText.endsWith(QChar('\n'))) {
            lineText.chop(1);
        }
        lines.push_back(lineText);
    }
    layout.endLayout();

    return lines;
}

QPixmap PreviewTextDelegate::imageForHash(const QString &hash, int targetSide) const {
    if (hash.isEmpty() || targetSide <= 0) {
        return {};
    }

    const QString cacheKey = QStringLiteral("%1:%2").arg(hash).arg(targetSide);
    auto it = m_imageCache.constFind(cacheKey);
    if (it != m_imageCache.constEnd()) {
        return it.value();
    }

    if (m_failedImageHashes.contains(cacheKey)) {
        return {};
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("blob_hash"), hash);
    params.insert(QStringLiteral("max_edge"), targetSide);

    const QCborMap result =
        m_client.request(QStringLiteral("GetImagePreview"), params, 1200, &error);
    if (!error.isEmpty()) {
        m_failedImageHashes.insert(cacheKey, true);
        return {};
    }

    const QByteArray bytes = result.value(QStringLiteral("bytes")).toByteArray();
    if (bytes.isEmpty()) {
        m_failedImageHashes.insert(cacheKey, true);
        return {};
    }

    const QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        m_failedImageHashes.insert(cacheKey, true);
        return {};
    }

    const QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.isNull()) {
        m_failedImageHashes.insert(cacheKey, true);
        return {};
    }

    m_imageCache.insert(cacheKey, pixmap);
    return pixmap;
}

void PreviewTextDelegate::paint(QPainter *painter,
                                const QStyleOptionViewItem &option,
                                const QModelIndex &index) const {
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);

    const QString rawText = opt.text;
    opt.text.clear();

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    QRect textRect =
        style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
    if (textRect.width() <= 0 || textRect.height() <= 0) {
        return;
    }

    const QString imageBlobHash = index.data(HistoryModel::ImageBlobHashRole).toString();
    if (!imageBlobHash.isEmpty()) {
        const int thumbSide = qMax(16, textRect.height() - 4);
        const QRect thumbRect(textRect.left(),
                              textRect.top() + (textRect.height() - thumbSide) / 2,
                              thumbSide, thumbSide);

        const QPixmap pixmap = imageForHash(imageBlobHash, thumbSide);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(opt.palette.color(QPalette::Mid));
        painter->setBrush(opt.palette.color(QPalette::Base));
        painter->drawRoundedRect(thumbRect.adjusted(0, 0, -1, -1), 3, 3);

        if (!pixmap.isNull()) {
            const QPixmap fitted =
                pixmap.scaled(thumbRect.size() - QSize(2, 2), Qt::KeepAspectRatio,
                              Qt::SmoothTransformation);
            const QRect imageRect(
                thumbRect.center().x() - fitted.width() / 2,
                thumbRect.center().y() - fitted.height() / 2,
                fitted.width(), fitted.height());
            painter->drawPixmap(imageRect, fitted);
        } else {
            painter->setPen(opt.palette.color(QPalette::Text));
            painter->drawText(thumbRect, Qt::AlignCenter, QStringLiteral("IMG"));
        }

        painter->restore();

        textRect.adjust(thumbRect.width() + 8, 0, 0, 0);
        if (textRect.width() <= 0) {
            return;
        }
    }

    QStringList lines = wrappedLines(rawText, opt.font, textRect.width());
    if (lines.isEmpty()) {
        return;
    }

    const QFontMetrics fm(opt.font);
    if (lines.size() > m_maxLines) {
        lines = lines.mid(0, m_maxLines);
        lines.last() = fm.elidedText(lines.last(), Qt::ElideRight, textRect.width());
    }

    const int lineHeight = fm.lineSpacing();
    const int drawLineCount = qMin(lines.size(), m_maxLines);
    const int totalHeight = lineHeight * drawLineCount;
    int y = textRect.top();
    if (opt.displayAlignment & Qt::AlignVCenter) {
        y = textRect.top() + qMax(0, (textRect.height() - totalHeight) / 2);
    }

    painter->save();
    const auto role = (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText
                                                            : QPalette::Text;
    painter->setPen(opt.palette.color(role));

    for (int i = 0; i < drawLineCount; ++i) {
        const QRect lineRect(textRect.left(), y + i * lineHeight, textRect.width(),
                             lineHeight);
        painter->drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter, lines.at(i));
    }

    painter->restore();
}

}  // namespace pastetry
