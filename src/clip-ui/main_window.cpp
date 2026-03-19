#include "clip-ui/main_window.h"

#include "clip-ui/clipboard_inspector_dialog.h"
#include "clip-ui/preview_text_delegate.h"

#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QComboBox>
#include <QDropEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

namespace pastetry {
namespace {

class PinnedReorderTableView : public QTableView {
public:
    explicit PinnedReorderTableView(QWidget *parent = nullptr) : QTableView(parent) {}

    std::function<void(int fromRow, int toRow)> onPinnedReorderDrop;

protected:
    void dropEvent(QDropEvent *event) override {
        if (!onPinnedReorderDrop) {
            QTableView::dropEvent(event);
            return;
        }

        const QModelIndexList selectedRows =
            selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
        if (selectedRows.isEmpty()) {
            event->ignore();
            return;
        }
        const int fromRow = selectedRows.first().row();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        int toRow = indexAt(event->position().toPoint()).row();
#else
        int toRow = indexAt(event->pos()).row();
#endif
        if (toRow < 0 && model()) {
            toRow = model()->rowCount();
        }
        onPinnedReorderDrop(fromRow, toRow);
        event->acceptProposedAction();
    }
};

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

MainWindow::MainWindow(IpcAsyncRunner *ipcRunner, QWidget *parent)
    : QMainWindow(parent), m_ipcRunner(ipcRunner) {
    setWindowTitle("Pastetry");
    resize(980, 620);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *toolbar = new QHBoxLayout();

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search clipboard history...");
    m_searchModeCombo = new QComboBox(this);
    m_searchModeCombo->addItem(QStringLiteral("Plain"), searchModeToString(SearchMode::Plain));
    m_searchModeCombo->addItem(QStringLiteral("Regex"), searchModeToString(SearchMode::Regex));
    m_searchModeCombo->addItem(QStringLiteral("Advanced"),
                               searchModeToString(SearchMode::Advanced));
    m_searchModeCombo->setToolTip(QStringLiteral("Search mode"));

    m_searchErrorLabel = new QLabel(this);
    m_searchErrorLabel->setStyleSheet(QStringLiteral("color: #b3412f;"));
    m_searchErrorLabel->setVisible(false);

    m_loadMoreButton = new QPushButton("Load More", this);
    m_activateButton = new QPushButton("Activate", this);
    m_pinButton = new QPushButton("Pin/Unpin", this);
    m_deleteButton = new QPushButton("Delete", this);
    m_clearButton = new QPushButton("Clear Unpinned", this);

    toolbar->addWidget(m_searchEdit, 1);
    toolbar->addWidget(m_searchModeCombo);
    toolbar->addWidget(m_loadMoreButton);
    toolbar->addWidget(m_activateButton);
    toolbar->addWidget(m_pinButton);
    toolbar->addWidget(m_deleteButton);
    toolbar->addWidget(m_clearButton);

    auto *reorderTable = new PinnedReorderTableView(this);
    m_table = reorderTable;
    m_model = new HistoryModel(this);
    m_previewDelegate = new PreviewTextDelegate(m_ipcRunner, m_table);
    m_table->setModel(m_model);
    m_table->setItemDelegate(m_previewDelegate);
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
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    reorderTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    reorderTable->setDragEnabled(false);
    reorderTable->setAcceptDrops(false);
    reorderTable->setDropIndicatorShown(false);
    reorderTable->setDefaultDropAction(Qt::MoveAction);
    reorderTable->onPinnedReorderDrop = [this](int fromRow, int toRow) {
        if (!m_pinnedReorderEnabled || fromRow < 0 || fromRow >= m_model->rowCount()) {
            return;
        }
        if (!m_model->pinnedAt(fromRow)) {
            return;
        }

        int pinnedCount = 0;
        while (pinnedCount < m_model->rowCount() && m_model->pinnedAt(pinnedCount)) {
            ++pinnedCount;
        }
        if (pinnedCount <= 1) {
            return;
        }

        int normalizedTargetRow = toRow;
        if (normalizedTargetRow < 0 || normalizedTargetRow > pinnedCount) {
            normalizedTargetRow = pinnedCount;
        }
        if (normalizedTargetRow >= pinnedCount) {
            normalizedTargetRow = pinnedCount - 1;
        }
        if (normalizedTargetRow > fromRow) {
            --normalizedTargetRow;
        }
        normalizedTargetRow = qBound(0, normalizedTargetRow, pinnedCount - 1);
        if (normalizedTargetRow == fromRow) {
            return;
        }

        movePinnedEntry(m_model->idAt(fromRow), normalizedTargetRow);
    };

    layout->addLayout(toolbar);
    layout->addWidget(m_searchErrorLabel);
    layout->addWidget(m_table);
    setCentralWidget(central);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(180);
    m_newHighlightTimer = new QTimer(this);
    m_newHighlightTimer->setInterval(1000);
    m_newHighlightTimer->start();

    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this] {
                m_searchTimer->start();
                updatePinnedReorderEnabled();
            });
    connect(m_searchTimer, &QTimer::timeout, this, [this] { loadInitial(); });
    connect(m_newHighlightTimer, &QTimer::timeout, this,
            [this] { m_table->viewport()->update(); });
    connect(m_searchModeCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        setSearchMode(searchModeFromComboIndex(index));
        emit searchModeChanged(searchModeToString(m_searchMode));
    });

    connect(m_loadMoreButton, &QPushButton::clicked, this, &MainWindow::loadMore);
    connect(m_activateButton, &QPushButton::clicked, this, &MainWindow::activateSelected);
    connect(m_pinButton, &QPushButton::clicked, this, &MainWindow::pinSelected);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::deleteSelected);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearHistory);
    connect(m_table, &QTableView::doubleClicked, this, [this] { activateSelected(); });
    connect(m_table, &QTableView::customContextMenuRequested, this,
            &MainWindow::showEntryContextMenu);
    connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested,
            this, &MainWindow::showHeaderContextMenu);

    auto *focusSearchShortcut =
        new QShortcut(QKeySequence::StandardKey::Find, this);
    connect(focusSearchShortcut, &QShortcut::activated, this, [this] {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });
    auto *refreshShortcut = new QShortcut(QKeySequence(Qt::Key_F5), this);
    connect(refreshShortcut, &QShortcut::activated, this, [this] { loadInitial(); });
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
    auto *loadMoreShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_N), this);
    connect(loadMoreShortcut, &QShortcut::activated, this, &MainWindow::loadMore);
    auto *closeWindowShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(closeWindowShortcut, &QShortcut::activated, this, [this] { close(); });

    auto *activateReturnShortcut =
        new QShortcut(QKeySequence(Qt::Key_Return), m_table);
    connect(activateReturnShortcut, &QShortcut::activated, this,
            &MainWindow::activateSelected);
    auto *activateEnterShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), m_table);
    connect(activateEnterShortcut, &QShortcut::activated, this,
            &MainWindow::activateSelected);
    auto *pinShortcut =
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_P), m_table);
    connect(pinShortcut, &QShortcut::activated, this, &MainWindow::pinSelected);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_table);
    connect(deleteShortcut, &QShortcut::activated, this, &MainWindow::deleteSelected);

    syncSearchModeCombo();
    applyTableLayout();
    updatePinnedReorderEnabled();
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

void MainWindow::setSearchMode(SearchMode mode) {
    if (m_searchMode == mode) {
        return;
    }
    m_searchMode = mode;
    syncSearchModeCombo();
    loadInitial();
}

SearchMode MainWindow::searchMode() const {
    return m_searchMode;
}

void MainWindow::setRegexStrict(bool enabled) {
    if (m_regexStrict == enabled) {
        return;
    }
    m_regexStrict = enabled;
    if (m_searchMode == SearchMode::Regex) {
        loadInitial();
    }
}

qint64 MainWindow::selectedEntryId() const {
    const auto indexes = m_table->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        return -1;
    }
    return m_model->idAt(indexes.first().row());
}

void MainWindow::refresh(bool resetCursor) {
    if (!m_ipcRunner) {
        statusBar()->showMessage(QStringLiteral("Search failed: IPC unavailable"), 4000);
        return;
    }

    QCborMap params;
    params.insert(QStringLiteral("query"), m_searchEdit->text());
    params.insert(QStringLiteral("cursor"), resetCursor ? 0 : m_cursor);
    params.insert(QStringLiteral("limit"), 120);
    params.insert(QStringLiteral("mode"), searchModeToString(m_searchMode));
    params.insert(QStringLiteral("regex_strict"), m_regexStrict);

    m_pendingSearchParams = params;
    m_pendingSearchResetCursor = resetCursor;
    m_searchPending = true;
    startPendingSearch();
}

void MainWindow::startPendingSearch() {
    if (m_searchInFlight || !m_searchPending || !m_ipcRunner) {
        return;
    }

    const QCborMap params = m_pendingSearchParams;
    const bool resetCursor = m_pendingSearchResetCursor;
    m_searchPending = false;
    m_searchInFlight = true;
    m_loadMoreButton->setEnabled(false);

    m_ipcRunner->request(
        QStringLiteral("SearchEntries"), params, 2500, this,
        [this, resetCursor](const QCborMap &result, const QString &error) {
            m_searchInFlight = false;
            const bool superseded = m_searchPending;
            if (!superseded) {
                if (!error.isEmpty()) {
                    statusBar()->showMessage(QStringLiteral("Search failed: %1").arg(error),
                                             4000);
                } else {
                    const bool queryValid =
                        !result.contains(QStringLiteral("query_valid")) ||
                        result.value(QStringLiteral("query_valid")).toBool();
                    const QString queryError =
                        result.value(QStringLiteral("query_error")).toString();
                    if (!queryValid) {
                        setSearchError(queryError);
                        statusBar()->showMessage(QStringLiteral("Invalid query"), 2500);
                    } else {
                        setSearchError(QString());

                        QVector<EntrySummary> entries =
                            parseSummaries(result.value(QStringLiteral("entries")).toArray());
                        const int nextCursor =
                            result.value(QStringLiteral("next_cursor")).toInteger();

                        if (resetCursor) {
                            m_model->resetData(std::move(entries), nextCursor);
                        } else {
                            m_model->appendData(std::move(entries), nextCursor);
                        }

                        m_cursor = m_model->nextCursor();
                        updatePinnedReorderEnabled();
                        statusBar()->showMessage(
                            QStringLiteral("Loaded %1 entries").arg(m_model->rowCount()), 2000);
                    }
                }
            }

            if (m_searchPending) {
                startPendingSearch();
            } else {
                m_loadMoreButton->setEnabled(m_cursor >= 0);
            }
        });
}

void MainWindow::setMutationBusy(bool busy) {
    m_mutationBusy = busy;
    if (m_activateButton) {
        m_activateButton->setEnabled(!busy);
    }
    if (m_pinButton) {
        m_pinButton->setEnabled(!busy);
    }
    if (m_deleteButton) {
        m_deleteButton->setEnabled(!busy);
    }
    if (m_clearButton) {
        m_clearButton->setEnabled(!busy);
    }
    if (m_loadMoreButton && !m_searchInFlight && !m_searchPending) {
        m_loadMoreButton->setEnabled(!busy && m_cursor >= 0);
    }
}

void MainWindow::movePinnedEntry(qint64 entryId, int targetPinnedIndex) {
    if (entryId <= 0 || targetPinnedIndex < 0 || m_mutationBusy || !m_ipcRunner) {
        return;
    }

    QCborMap params;
    params.insert(QStringLiteral("entry_id"), entryId);
    params.insert(QStringLiteral("target_index"), targetPinnedIndex);
    setMutationBusy(true);
    m_ipcRunner->request(
        QStringLiteral("MovePinnedEntry"), params, 2500, this,
        [this](const QCborMap &, const QString &error) {
            setMutationBusy(false);
            if (!error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Pinned reorder failed"), error);
                return;
            }

            refresh(true);
        });
}

void MainWindow::updatePinnedReorderEnabled() {
    const bool enabled = m_searchEdit && m_searchEdit->text().trimmed().isEmpty();
    if (m_pinnedReorderEnabled == enabled) {
        return;
    }
    m_pinnedReorderEnabled = enabled;

    auto *reorderTable = dynamic_cast<PinnedReorderTableView *>(m_table);
    if (!reorderTable) {
        return;
    }
    reorderTable->setDragDropMode(enabled ? QAbstractItemView::DragDrop
                                          : QAbstractItemView::NoDragDrop);
    reorderTable->setDragEnabled(enabled);
    reorderTable->setAcceptDrops(enabled);
    reorderTable->setDropIndicatorShown(enabled);
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

void MainWindow::setSearchError(const QString &message) {
    const QString trimmed = message.trimmed();
    m_searchErrorLabel->setVisible(!trimmed.isEmpty());
    m_searchErrorLabel->setText(trimmed);
}

void MainWindow::syncSearchModeCombo() {
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

void MainWindow::showHeaderContextMenu(const QPoint &position) {
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

void MainWindow::showEntryContextMenu(const QPoint &position) {
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

    QAction *loadingAction = activateAsMenu->addAction(QStringLiteral("Loading formats..."));
    loadingAction->setEnabled(false);

    if (m_ipcRunner) {
        QPointer<QMenu> menuGuard(&menu);
        QPointer<QMenu> activateAsGuard(activateAsMenu);
        QCborMap detailParams;
        detailParams.insert(QStringLiteral("entry_id"), entryId);
        m_ipcRunner->request(
            QStringLiteral("GetEntryDetail"), detailParams, 2500, this,
            [menuGuard, activateAsGuard](const QCborMap &detailResult, const QString &detailError) {
                if (!menuGuard || !activateAsGuard) {
                    return;
                }

                activateAsGuard->clear();
                if (!detailError.isEmpty()) {
                    QAction *unavailable = activateAsGuard->addAction(
                        QStringLiteral("Unavailable: %1").arg(detailError));
                    unavailable->setEnabled(false);
                    return;
                }

                const QVector<FormatMenuItem> formats =
                    parseFormatMenuItems(detailResult.value(QStringLiteral("formats")).toArray());
                if (formats.isEmpty()) {
                    QAction *none = activateAsGuard->addAction(QStringLiteral("No formats"));
                    none->setEnabled(false);
                    return;
                }

                for (const auto &format : formats) {
                    QAction *action = activateAsGuard->addAction(formatMenuLabel(format));
                    action->setData(format.mimeType);
                }
            });
    }

    QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(position));
    if (!chosen) {
        return;
    }

    if (chosen == activateAction) {
        activateSelected();
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
        activateEntry(entryId, preferredFormat);
    }
}

void MainWindow::inspectEntry(qint64 entryId) {
    if (entryId <= 0) {
        return;
    }

    if (!m_clipboardInspectorDialog) {
        m_clipboardInspectorDialog =
            new ClipboardInspectorDialog(m_ipcRunner, nullptr);
    }
    m_clipboardInspectorDialog->inspectEntry(entryId);
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

    activateEntry(id, QString());
}

void MainWindow::activateEntry(qint64 entryId, const QString &preferredFormat) {
    if (m_mutationBusy || !m_ipcRunner) {
        return;
    }

    QCborMap params;
    params.insert(QStringLiteral("entry_id"), entryId);
    params.insert(QStringLiteral("preferred_format"), preferredFormat);

    setMutationBusy(true);
    m_ipcRunner->request(
        QStringLiteral("ActivateEntry"), params, 2500, this,
        [this](const QCborMap &, const QString &error) {
            setMutationBusy(false);
            if (!error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Activate failed"), error);
                return;
            }

            statusBar()->showMessage(QStringLiteral("Clipboard updated"), 2000);
        });
}

void MainWindow::pinSelected() {
    if (m_mutationBusy || !m_ipcRunner) {
        return;
    }

    const auto indexes = m_table->selectionModel()->selectedRows();
    if (indexes.isEmpty()) {
        return;
    }

    const int row = indexes.first().row();
    const qint64 id = m_model->idAt(row);
    const bool currentlyPinned = m_model->pinnedAt(row);

    QCborMap params;
    params.insert(QStringLiteral("entry_id"), id);
    params.insert(QStringLiteral("pinned"), !currentlyPinned);
    setMutationBusy(true);
    m_ipcRunner->request(
        QStringLiteral("PinEntry"), params, 2500, this,
        [this](const QCborMap &, const QString &error) {
            setMutationBusy(false);
            if (!error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Pin failed"), error);
                return;
            }

            refresh(true);
        });
}

void MainWindow::deleteSelected() {
    if (m_mutationBusy || !m_ipcRunner) {
        return;
    }

    const qint64 id = selectedEntryId();
    if (id < 0) {
        return;
    }

    QCborMap params;
    params.insert(QStringLiteral("entry_id"), id);
    setMutationBusy(true);
    m_ipcRunner->request(
        QStringLiteral("DeleteEntry"), params, 2500, this,
        [this](const QCborMap &, const QString &error) {
            setMutationBusy(false);
            if (!error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Delete failed"), error);
                return;
            }

            refresh(true);
        });
}

void MainWindow::clearHistory() {
    if (m_mutationBusy || !m_ipcRunner) {
        return;
    }

    if (QMessageBox::question(
            this, "Clear unpinned",
            "Delete all unpinned clipboard entries?") != QMessageBox::Yes) {
        return;
    }

    QCborMap params;
    params.insert(QStringLiteral("keep_pinned"), true);
    setMutationBusy(true);
    m_ipcRunner->request(
        QStringLiteral("ClearHistory"), params, 2500, this,
        [this](const QCborMap &, const QString &error) {
            setMutationBusy(false);
            if (!error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Clear failed"), error);
                return;
            }

            refresh(true);
        });
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
