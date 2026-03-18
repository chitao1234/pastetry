#include "clip-ui/main_window.h"

#include "clip-ui/preview_text_delegate.h"

#include <QCborArray>
#include <QCborMap>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace pastetry {
namespace {

QVector<EntrySummary> parseSummaries(const QCborArray &items) {
    QVector<EntrySummary> entries;
    entries.reserve(items.size());

    for (const auto &item : items) {
        const QCborMap map = item.toMap();
        EntrySummary entry;
        entry.id = map.value("id").toInteger();
        entry.createdAtMs = map.value("created_at_ms").toInteger();
        entry.preview = map.value("preview").toString();
        entry.sourceApp = map.value("source_app").toString();
        entry.pinned = map.value("pinned").toBool();
        entry.formatCount = map.value("format_count").toInteger();
        entry.imageBlobHash = map.value("image_blob_hash").toString();
        entries.push_back(entry);
    }

    return entries;
}

}  // namespace

MainWindow::MainWindow(IpcClient client, QWidget *parent)
    : QMainWindow(parent), m_client(std::move(client)) {
    setWindowTitle("Pastetry");
    resize(980, 620);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *toolbar = new QHBoxLayout();

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search clipboard history...");

    m_loadMoreButton = new QPushButton("Load More", this);
    m_activateButton = new QPushButton("Activate", this);
    m_pinButton = new QPushButton("Pin/Unpin", this);
    m_deleteButton = new QPushButton("Delete", this);
    m_clearButton = new QPushButton("Clear Unpinned", this);

    toolbar->addWidget(m_searchEdit, 1);
    toolbar->addWidget(m_loadMoreButton);
    toolbar->addWidget(m_activateButton);
    toolbar->addWidget(m_pinButton);
    toolbar->addWidget(m_deleteButton);
    toolbar->addWidget(m_clearButton);

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
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::TimeColumn,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::PreviewColumn,
                                                      QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::FormatsColumn,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(HistoryModel::PinnedColumn,
                                                      QHeaderView::ResizeToContents);

    layout->addLayout(toolbar);
    layout->addWidget(m_table);
    setCentralWidget(central);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(180);

    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this] { m_searchTimer->start(); });
    connect(m_searchTimer, &QTimer::timeout, this, [this] { loadInitial(); });

    connect(m_loadMoreButton, &QPushButton::clicked, this, &MainWindow::loadMore);
    connect(m_activateButton, &QPushButton::clicked, this, &MainWindow::activateSelected);
    connect(m_pinButton, &QPushButton::clicked, this, &MainWindow::pinSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearHistory);
    connect(m_table, &QTableView::doubleClicked, this, [this] { activateSelected(); });

    applyTableLayout();
    loadInitial();
}

void MainWindow::showAndActivate() {
    show();
    raise();
    activateWindow();
}

void MainWindow::setCloseToTrayEnabled(bool enabled) {
    m_closeToTrayEnabled = enabled;
}

void MainWindow::setVisibleColumns(const QVector<bool> &visibleColumns) {
    if (visibleColumns.size() != HistoryModel::ColumnCount) {
        return;
    }

    m_visibleColumns = visibleColumns;
    applyTableLayout();
}

void MainWindow::setPreviewLineCount(int lineCount) {
    m_previewLineCount = qBound(1, lineCount, 12);
    applyTableLayout();
}

qint64 MainWindow::selectedEntryId() const {
    const auto indexes = m_table->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        return -1;
    }
    return m_model->idAt(indexes.first().row());
}

void MainWindow::refresh(bool resetCursor) {
    QString error;
    QCborMap params;
    params.insert(QStringLiteral("query"), m_searchEdit->text());
    params.insert(QStringLiteral("cursor"), resetCursor ? 0 : m_cursor);
    params.insert(QStringLiteral("limit"), 120);

    const QCborMap result = m_client.request("SearchEntries", params, 2500, &error);
    if (!error.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Search failed: %1").arg(error), 4000);
        return;
    }

    QVector<EntrySummary> entries = parseSummaries(result.value("entries").toArray());
    const int nextCursor = result.value("next_cursor").toInteger();

    if (resetCursor) {
        m_model->resetData(std::move(entries), nextCursor);
    } else {
        m_model->appendData(std::move(entries), nextCursor);
    }

    m_cursor = m_model->nextCursor();
    m_loadMoreButton->setEnabled(m_cursor >= 0);
    statusBar()->showMessage(QStringLiteral("Loaded %1 entries").arg(m_model->rowCount()),
                             2000);
}

void MainWindow::applyTableLayout() {
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

void MainWindow::loadInitial() {
    m_cursor = 0;
    refresh(true);
}

void MainWindow::loadMore() {
    if (m_cursor < 0) {
        return;
    }
    refresh(false);
}

void MainWindow::activateSelected() {
    const qint64 id = selectedEntryId();
    if (id < 0) {
        return;
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), id);
    params.insert(QStringLiteral("preferred_format"), QString());

    m_client.request("ActivateEntry", params, 2500, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Activate failed", error);
        return;
    }

    statusBar()->showMessage("Clipboard updated", 2000);
}

void MainWindow::pinSelected() {
    const auto indexes = m_table->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        return;
    }

    const int row = indexes.first().row();
    const qint64 id = m_model->idAt(row);
    const bool currentlyPinned = m_model->pinnedAt(row);

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), id);
    params.insert(QStringLiteral("pinned"), !currentlyPinned);

    m_client.request("PinEntry", params, 2500, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Pin failed", error);
        return;
    }

    refresh(true);
}

void MainWindow::deleteSelected() {
    const qint64 id = selectedEntryId();
    if (id < 0) {
        return;
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), id);

    m_client.request("DeleteEntry", params, 2500, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Delete failed", error);
        return;
    }

    refresh(true);
}

void MainWindow::clearHistory() {
    if (QMessageBox::question(
            this, "Clear unpinned",
            "Delete all unpinned clipboard entries?") != QMessageBox::Yes) {
        return;
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("keep_pinned"), true);

    m_client.request("ClearHistory", params, 2500, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Clear failed", error);
        return;
    }

    refresh(true);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_closeToTrayEnabled) {
        event->ignore();
        hide();
        emit closeToTrayRequested();
        return;
    }

    QMainWindow::closeEvent(event);
}

}  // namespace pastetry
