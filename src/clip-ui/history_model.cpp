#include "clip-ui/history_model.h"

#include <QDateTime>

namespace pastetry {

HistoryModel::HistoryModel(QObject *parent) : QAbstractTableModel(parent) {}

int HistoryModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

int HistoryModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return ColumnCount;
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const EntrySummary &entry = m_entries.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0:
                return QDateTime::fromMSecsSinceEpoch(entry.createdAtMs)
                    .toString("yyyy-MM-dd hh:mm:ss");
            case 1:
                return entry.preview;
            case 2:
                return entry.formatCount;
            case 3:
                return entry.pinned ? "Yes" : "No";
            default:
                return {};
        }
    }

    if (role == Qt::ToolTipRole && index.column() == 1) {
        return entry.preview;
    }

    if (role == ImageBlobHashRole) {
        return entry.imageBlobHash;
    }

    if (role == CreatedAtMsRole) {
        return entry.createdAtMs;
    }

    if (role == PinnedRole) {
        return entry.pinned;
    }

    if (role == Qt::TextAlignmentRole && index.column() >= FormatsColumn) {
        return Qt::AlignCenter;
    }

    return {};
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation,
                                  int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
        case TimeColumn:
            return "Time";
        case PreviewColumn:
            return "Preview";
        case FormatsColumn:
            return "Formats";
        case PinnedColumn:
            return "Pinned";
        default:
            return {};
    }
}

void HistoryModel::resetData(QVector<EntrySummary> entries, int nextCursor) {
    beginResetModel();
    m_entries = std::move(entries);
    m_nextCursor = nextCursor;
    endResetModel();
}

void HistoryModel::appendData(QVector<EntrySummary> entries, int nextCursor) {
    if (entries.isEmpty()) {
        m_nextCursor = nextCursor;
        return;
    }

    const int start = m_entries.size();
    const int end = start + entries.size() - 1;
    beginInsertRows(QModelIndex(), start, end);
    for (auto &entry : entries) {
        m_entries.push_back(std::move(entry));
    }
    m_nextCursor = nextCursor;
    endInsertRows();
}

qint64 HistoryModel::idAt(int row) const {
    if (row < 0 || row >= m_entries.size()) {
        return -1;
    }
    return m_entries.at(row).id;
}

bool HistoryModel::pinnedAt(int row) const {
    if (row < 0 || row >= m_entries.size()) {
        return false;
    }
    return m_entries.at(row).pinned;
}

int HistoryModel::nextCursor() const {
    return m_nextCursor;
}

}  // namespace pastetry
