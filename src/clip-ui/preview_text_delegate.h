#pragma once

#include <QStyledItemDelegate>

namespace pastetry {

class PreviewTextDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit PreviewTextDelegate(QObject *parent = nullptr);

    void setMaxLines(int maxLines);
    int maxLines() const;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    QStringList wrappedLines(const QString &text, const QFont &font,
                             int maxWidth) const;

    int m_maxLines = 2;
};

}  // namespace pastetry
