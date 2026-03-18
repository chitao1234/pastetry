#include "clip-ui/quick_paste_dialog.h"

#include <QCborArray>
#include <QCborMap>
#include <QCursor>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
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
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

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

}  // namespace pastetry
