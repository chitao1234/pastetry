#include "clip-ui/preview_text_delegate.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTextLayout>

namespace pastetry {

PreviewTextDelegate::PreviewTextDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

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
