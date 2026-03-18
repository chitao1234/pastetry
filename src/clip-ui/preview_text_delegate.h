#pragma once

#include "common/ipc_client.h"

#include <QHash>
#include <QPixmap>
#include <QStyledItemDelegate>

namespace pastetry {

class PreviewTextDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit PreviewTextDelegate(IpcClient client, QObject *parent = nullptr);

    void setMaxLines(int maxLines);
    int maxLines() const;

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    QStringList wrappedLines(const QString &text, const QFont &font,
                             int maxWidth) const;
    QPixmap imageForHash(const QString &hash, int targetSide) const;

    IpcClient m_client;
    int m_maxLines = 2;
    mutable QHash<QString, QPixmap> m_imageCache;
    mutable QHash<QString, bool> m_failedImageHashes;
};

}  // namespace pastetry
