#pragma once

#include "common/models.h"

#include <QAbstractTableModel>

namespace pastetry {

class HistoryModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        TimeColumn = 0,
        PreviewColumn = 1,
        FormatsColumn = 2,
        PinnedColumn = 3,
        ColumnCount = 4,
    };

    enum Role {
        ImageBlobHashRole = Qt::UserRole + 1,
    };

    explicit HistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;

    void resetData(QVector<EntrySummary> entries, int nextCursor);
    void appendData(QVector<EntrySummary> entries, int nextCursor);

    qint64 idAt(int row) const;
    bool pinnedAt(int row) const;
    int nextCursor() const;

private:
    QVector<EntrySummary> m_entries;
    int m_nextCursor = -1;
};

}  // namespace pastetry
