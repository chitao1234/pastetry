#include "clip-ui/quick_paste_dialog.h"

#include "clip-ui/preview_text_delegate.h"

#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QCursor>
#include <QEvent>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace pastetry {
namespace {

QVector<EntrySummary> parseSummaries(const QCborArray &items) {
    QVector<EntrySummary> entries;
    entries.reserve(items.size());

    for (const auto &item : items) {
        const QCborMap map = item.toMap();
        EntrySummary entry;
        entry.id = map.value(QStringLiteral("id")).toInteger();
        entry.createdAtMs = map.value(QStringLiteral("created_at_ms")).toInteger();
        entry.preview = map.value(QStringLiteral("preview")).toString();
        entry.sourceApp = map.value(QStringLiteral("source_app")).toString();
        entry.pinned = map.value(QStringLiteral("pinned")).toBool();
        entry.formatCount = map.value(QStringLiteral("format_count")).toInteger();
        entry.imageBlobHash = map.value(QStringLiteral("image_blob_hash")).toString();
        entries.push_back(entry);
    }

    return entries;
}

}  // namespace

QuickPasteDialog::QuickPasteDialog(IpcClient client, QWidget *parent)
    : QDialog(parent), m_client(std::move(client)) {
    setWindowTitle(QStringLiteral("Quick Paste"));
    setWindowFlag(Qt::Tool, true);
    resize(760, 420);

    auto *layout = new QVBoxLayout(this);
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Type to search clipboard history..."));

    m_table = new QTableView(this);
    m_model = new HistoryModel(this);
    m_previewDelegate = new PreviewTextDelegate(m_client, m_table);
    m_table->setModel(m_model);
    m_table->setItemDelegateForColumn(HistoryModel::PreviewColumn, m_previewDelegate);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(true);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::TimeColumn,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::PreviewColumn,
                                                      QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::FormatsColumn,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::PinnedColumn,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

    layout->addWidget(m_searchEdit);
    layout->addWidget(m_table);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(120);

    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this] { m_searchTimer->start(); });
    connect(m_searchTimer, &QTimer::timeout, this, &QuickPasteDialog::refreshResults);
    connect(m_searchEdit, &QLineEdit::returnPressed, this,
            &QuickPasteDialog::activateCurrent);
    connect(m_table, &QTableView::doubleClicked, this,
            [this] { activateCurrent(); });
    connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &QuickPasteDialog::showHeaderContextMenu);

    applyTableLayout();
}

void QuickPasteDialog::setVisibleColumns(const QVector<bool> &visibleColumns) {
    if (visibleColumns.size() != HistoryModel::ColumnCount) {
        return;
    }

    m_visibleColumns = visibleColumns;
    applyTableLayout();
}

void QuickPasteDialog::setPreviewLineCount(int lineCount) {
    m_previewLineCount = qBound(1, lineCount, 12);
    applyTableLayout();
}

void QuickPasteDialog::openPopup() {
    if (!isVisible()) {
        const QPoint cursorPos = QCursor::pos();
        move(cursorPos.x() - width() / 2, cursorPos.y() - 40);
        show();
    }

    raise();
    activateWindow();

    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
    refreshResults();
}

void QuickPasteDialog::togglePopup() {
    if (isVisible()) {
        hide();
        return;
    }

    openPopup();
}

void QuickPasteDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        activateCurrent();
        event->accept();
        return;
    }

    QDialog::keyPressEvent(event);
}

void QuickPasteDialog::hideEvent(QHideEvent *event) {
    QDialog::hideEvent(event);
    emit popupHidden();
}

bool QuickPasteDialog::event(QEvent *event) {
    if (event->type() == QEvent::WindowDeactivate && isVisible()) {
        hide();
        return true;
    }

    return QDialog::event(event);
}

qint64 QuickPasteDialog::selectedEntryId() const {
    const auto indexes = m_table->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        return -1;
    }
    return m_model->idAt(indexes.first().row());
}

void QuickPasteDialog::refreshResults() {
    QString error;
    QCborMap params;
    params.insert(QStringLiteral("query"), m_searchEdit->text());
    params.insert(QStringLiteral("cursor"), 0);
    params.insert(QStringLiteral("limit"), 60);

    const QCborMap result = m_client.request(QStringLiteral("SearchEntries"), params,
                                             2500, &error);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return;
    }

    QVector<EntrySummary> entries =
        parseSummaries(result.value(QStringLiteral("entries")).toArray());
    const int nextCursor = result.value(QStringLiteral("next_cursor")).toInteger();
    m_model->resetData(std::move(entries), nextCursor);

    if (m_model->rowCount() > 0) {
        m_table->selectRow(0);
    }
}

void QuickPasteDialog::activateCurrent() {
    qint64 entryId = selectedEntryId();
    if (entryId < 0 && m_model->rowCount() > 0) {
        entryId = m_model->idAt(0);
    }

    if (entryId < 0) {
        return;
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), entryId);
    params.insert(QStringLiteral("preferred_format"), QString());

    m_client.request(QStringLiteral("ActivateEntry"), params, 2500, &error);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return;
    }

    emit entryActivated();
    hide();
}

void QuickPasteDialog::applyTableLayout() {
    for (int column = 0; column < HistoryModel::ColumnCount; ++column) {
        const bool visible = column < m_visibleColumns.size() ? m_visibleColumns.at(column) : true;
        m_table->setColumnHidden(column, !visible);
    }

    m_previewDelegate->setMaxLines(m_previewLineCount);
    QModelIndex previewIndex;
    if (m_model->rowCount() > 0) {
        previewIndex = m_model->index(0, HistoryModel::PreviewColumn);
    }
    QStyleOptionViewItem option;
    option.initFrom(m_table);
    option.font = m_table->font();
    option.rect = QRect(0, 0, m_table->viewport()->width(), m_table->fontMetrics().height());
    const int rowHeight =
        m_previewDelegate->sizeHint(option, previewIndex).height();
    m_table->verticalHeader()->setDefaultSectionSize(rowHeight);
}

void QuickPasteDialog::showHeaderContextMenu(const QPoint &position) {
    auto *header = m_table->horizontalHeader();
    QMenu menu(this);

    for (int column = 0; column < HistoryModel::ColumnCount; ++column) {
        const QString label =
            m_model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
        QAction *action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(!m_table->isColumnHidden(column));

        connect(action, &QAction::toggled, &menu, [this, action, column](bool checked) {
            QVector<bool> updated = m_visibleColumns;
            if (column < 0 || column >= updated.size()) {
                return;
            }
            updated[column] = checked;

            bool anyVisible = false;
            for (const bool visible : updated) {
                anyVisible = anyVisible || visible;
            }
            if (!anyVisible) {
                action->blockSignals(true);
                action->setChecked(true);
                action->blockSignals(false);
                return;
            }

            setVisibleColumns(updated);
            emit visibleColumnsChanged(m_visibleColumns);
        });
    }

    menu.exec(header->mapToGlobal(position));
}

}  // namespace pastetry
