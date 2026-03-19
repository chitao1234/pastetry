#include "clip-ui/quick_paste_dialog.h"

#include "clip-ui/clipboard_inspector_dialog.h"
#include "clip-ui/preview_text_delegate.h"

#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QHeaderView>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QShortcut>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTimer>
#include <QHBoxLayout>
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

int searchModeComboIndex(SearchMode mode) {
    switch (mode) {
        case SearchMode::Regex:
            return 1;
        case SearchMode::Advanced:
            return 2;
        case SearchMode::Plain:
        default:
            return 0;
    }
}

SearchMode searchModeFromComboIndex(int index) {
    switch (index) {
        case 1:
            return SearchMode::Regex;
        case 2:
            return SearchMode::Advanced;
        case 0:
        default:
            return SearchMode::Plain;
    }
}

struct FormatMenuItem {
    QString mimeType;
    qint64 byteSize = 0;
};

QVector<FormatMenuItem> parseFormatMenuItems(const QCborArray &items) {
    QVector<FormatMenuItem> formats;
    formats.reserve(items.size());
    for (const auto &item : items) {
        const QCborMap map = item.toMap();
        const QString mimeType = map.value(QStringLiteral("mime_type")).toString().trimmed();
        if (mimeType.isEmpty()) {
            continue;
        }
        FormatMenuItem format;
        format.mimeType = mimeType;
        format.byteSize = map.value(QStringLiteral("byte_size")).toInteger();
        formats.push_back(format);
    }
    return formats;
}

QString formatMenuLabel(const FormatMenuItem &format) {
    return QStringLiteral("%1 (%2)")
        .arg(format.mimeType,
             QLocale().formattedDataSize(qMax<qint64>(0, format.byteSize)));
}

}  // namespace

QuickPasteDialog::QuickPasteDialog(IpcClient client, QWidget *parent)
    : QDialog(parent), m_client(std::move(client)) {
    setWindowTitle(QStringLiteral("Quick Paste"));
    setWindowFlag(Qt::Tool, true);
    resize(760, 420);

    auto *layout = new QVBoxLayout(this);
    auto *searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Type to search clipboard history..."));
    m_searchModeCombo = new QComboBox(this);
    m_searchModeCombo->addItem(QStringLiteral("Plain"), searchModeToString(SearchMode::Plain));
    m_searchModeCombo->addItem(QStringLiteral("Regex"), searchModeToString(SearchMode::Regex));
    m_searchModeCombo->addItem(QStringLiteral("Advanced"),
                               searchModeToString(SearchMode::Advanced));
    m_searchModeCombo->setToolTip(QStringLiteral("Search mode"));

    m_searchErrorLabel = new QLabel(this);
    m_searchErrorLabel->setStyleSheet(QStringLiteral("color: #b3412f;"));
    m_searchErrorLabel->setVisible(false);

    m_table = new QTableView(this);
    m_model = new HistoryModel(this);
    m_previewDelegate = new PreviewTextDelegate(m_client, m_table);
    m_table->setModel(m_model);
    m_table->setItemDelegate(m_previewDelegate);
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
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchModeCombo);
    layout->addLayout(searchRow);
    layout->addWidget(m_searchErrorLabel);
    layout->addWidget(m_table);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(120);
    m_newHighlightTimer = new QTimer(this);
    m_newHighlightTimer->setInterval(1000);
    m_newHighlightTimer->start();

    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this] { m_searchTimer->start(); });
    connect(m_searchTimer, &QTimer::timeout, this, &QuickPasteDialog::refreshResults);
    connect(m_newHighlightTimer, &QTimer::timeout, this,
            [this] {
                if (isVisible()) {
                    m_table->viewport()->update();
                }
            });
    connect(m_searchEdit, &QLineEdit::returnPressed, this,
            &QuickPasteDialog::activateCurrent);
    connect(m_table, &QTableView::doubleClicked, this,
            [this] { activateCurrent(); });
    connect(m_table, &QTableView::customContextMenuRequested, this,
            &QuickPasteDialog::showEntryContextMenu);
    connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &QuickPasteDialog::showHeaderContextMenu);
    connect(m_searchModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        setSearchMode(searchModeFromComboIndex(index));
        emit searchModeChanged(searchModeToString(m_searchMode));
    });

    auto *focusSearchShortcut =
        new QShortcut(QKeySequence::StandardKey::Find, this);
    connect(focusSearchShortcut, &QShortcut::activated, this, [this] {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });
    auto *refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refreshShortcut, &QShortcut::activated, this,
            &QuickPasteDialog::refreshResults);
    auto *clearSearchShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(clearSearchShortcut, &QShortcut::activated, this, [this] {
        m_searchEdit->clear();
        m_searchEdit->setFocus();
    });
    auto *inspectShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_I), this);
    connect(inspectShortcut, &QShortcut::activated, this, [this] {
        const qint64 entryId = selectedEntryId();
        if (entryId > 0) {
            inspectEntry(entryId);
        }
    });
    auto *refreshShortcutSecondary =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), this);
    connect(refreshShortcutSecondary, &QShortcut::activated, this,
            &QuickPasteDialog::refreshResults);
    auto *hideShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(hideShortcut, &QShortcut::activated, this, [this] { hide(); });

    auto *activateReturnShortcut =
        new QShortcut(QKeySequence(Qt::Key_Return), m_table);
    connect(activateReturnShortcut, &QShortcut::activated, this,
            &QuickPasteDialog::activateCurrent);
    auto *activateEnterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), m_table);
    connect(activateEnterShortcut, &QShortcut::activated, this,
            &QuickPasteDialog::activateCurrent);
    auto *pinShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_P), m_table);
    connect(pinShortcut, &QShortcut::activated, this, &QuickPasteDialog::pinSelected);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_table);
    connect(deleteShortcut, &QShortcut::activated, this,
            &QuickPasteDialog::deleteSelected);

    syncSearchModeCombo();
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

void QuickPasteDialog::setSearchMode(SearchMode mode) {
    if (m_searchMode == mode) {
        return;
    }
    m_searchMode = mode;
    syncSearchModeCombo();
    if (isVisible()) {
        refreshResults();
    }
}

SearchMode QuickPasteDialog::searchMode() const {
    return m_searchMode;
}

void QuickPasteDialog::setRegexStrict(bool enabled) {
    if (m_regexStrict == enabled) {
        return;
    }
    m_regexStrict = enabled;
    if (isVisible() && m_searchMode == SearchMode::Regex) {
        refreshResults();
    }
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
    params.insert(QStringLiteral("mode"), searchModeToString(m_searchMode));
    params.insert(QStringLiteral("regex_strict"), m_regexStrict);

    const QCborMap result = m_client.request(QStringLiteral("SearchEntries"), params,
                                             2500, &error);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return;
    }

    const bool queryValid =
        !result.contains(QStringLiteral("query_valid")) ||
        result.value(QStringLiteral("query_valid")).toBool();
    const QString queryError = result.value(QStringLiteral("query_error")).toString();
    if (!queryValid) {
        setSearchError(queryError);
        return;
    }
    setSearchError(QString());

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

    activateEntryById(entryId, QString());
}

bool QuickPasteDialog::activateEntryById(qint64 entryId, const QString &preferredFormat) {
    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), entryId);
    params.insert(QStringLiteral("preferred_format"), preferredFormat);

    m_client.request(QStringLiteral("ActivateEntry"), params, 2500, &error);
    if (!error.isEmpty()) {
        emit errorOccurred(error);
        return false;
    }

    emit entryActivated();
    hide();
    return true;
}

void QuickPasteDialog::pinSelected() {
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
    m_client.request(QStringLiteral("PinEntry"), params, 2500, &error);
    if (!error.isEmpty()) {
        emit errorOccurred(QStringLiteral("Pin failed: %1").arg(error));
        return;
    }

    refreshResults();
}

void QuickPasteDialog::deleteSelected() {
    const qint64 id = selectedEntryId();
    if (id < 0) {
        return;
    }

    QString error;
    QCborMap params;
    params.insert(QStringLiteral("entry_id"), id);
    m_client.request(QStringLiteral("DeleteEntry"), params, 2500, &error);
    if (!error.isEmpty()) {
        emit errorOccurred(QStringLiteral("Delete failed: %1").arg(error));
        return;
    }

    refreshResults();
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

void QuickPasteDialog::setSearchError(const QString &message) {
    const QString trimmed = message.trimmed();
    m_searchErrorLabel->setVisible(!trimmed.isEmpty());
    m_searchErrorLabel->setText(trimmed);
}

void QuickPasteDialog::syncSearchModeCombo() {
    if (!m_searchModeCombo) {
        return;
    }

    const int targetIndex = searchModeComboIndex(m_searchMode);
    if (m_searchModeCombo->currentIndex() == targetIndex) {
        return;
    }

    m_searchModeCombo->blockSignals(true);
    m_searchModeCombo->setCurrentIndex(targetIndex);
    m_searchModeCombo->blockSignals(false);
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

void QuickPasteDialog::showEntryContextMenu(const QPoint &position) {
    const QModelIndex clicked = m_table->indexAt(position);
    if (!clicked.isValid()) {
        return;
    }

    m_table->selectRow(clicked.row());
    const qint64 entryId = m_model->idAt(clicked.row());
    if (entryId < 0) {
        return;
    }
    const bool pinned = m_model->pinnedAt(clicked.row());

    QMenu menu(this);
    QAction *activateAction = menu.addAction(QStringLiteral("Activate"));
    QMenu *activateAsMenu = menu.addMenu(QStringLiteral("Activate As"));
    QAction *inspectAction = menu.addAction(QStringLiteral("Inspect Item"));
    QAction *pinAction = menu.addAction(pinned ? QStringLiteral("Unpin")
                                               : QStringLiteral("Pin"));
    QAction *deleteAction = menu.addAction(QStringLiteral("Delete"));

    QString detailError;
    QCborMap detailParams;
    detailParams.insert(QStringLiteral("entry_id"), entryId);
    const QCborMap detailResult =
        m_client.request(QStringLiteral("GetEntryDetail"), detailParams, 2500, &detailError);
    if (!detailError.isEmpty()) {
        QAction *unavailable =
            activateAsMenu->addAction(QStringLiteral("Unavailable: %1").arg(detailError));
        unavailable->setEnabled(false);
    } else {
        const QVector<FormatMenuItem> formats =
            parseFormatMenuItems(detailResult.value(QStringLiteral("formats")).toArray());
        if (formats.isEmpty()) {
            QAction *none = activateAsMenu->addAction(QStringLiteral("No formats"));
            none->setEnabled(false);
        } else {
            for (const auto &format : formats) {
                QAction *action = activateAsMenu->addAction(formatMenuLabel(format));
                action->setData(format.mimeType);
            }
        }
    }

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(position));
    if (!chosen) {
        return;
    }

    if (chosen == activateAction) {
        activateCurrent();
        return;
    }
    if (chosen == inspectAction) {
        inspectEntry(entryId);
        return;
    }
    if (chosen == pinAction) {
        pinSelected();
        return;
    }
    if (chosen == deleteAction) {
        deleteSelected();
        return;
    }

    const QString preferredFormat = chosen->data().toString().trimmed();
    if (!preferredFormat.isEmpty()) {
        activateEntryById(entryId, preferredFormat);
    }
}

void QuickPasteDialog::inspectEntry(qint64 entryId) {
    if (entryId <= 0) {
        return;
    }

    if (!m_clipboardInspectorDialog) {
        m_clipboardInspectorDialog =
            new ClipboardInspectorDialog(m_client, nullptr);
    }
    m_clipboardInspectorDialog->inspectEntry(entryId);
}

}  // namespace pastetry
