#pragma once

#include "clip-ui/history_model.h"
#include "clip-ui/ipc_async_runner.h"

#include <QCborMap>
#include <QCloseEvent>
#include <QMainWindow>
#include <QVector>

class QLineEdit;
class QPoint;
class QPushButton;
class QTableView;
class QTimer;
class QLabel;
class QComboBox;

namespace pastetry {

class PreviewTextDelegate;
class ClipboardInspectorDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(IpcAsyncRunner *ipcRunner, QWidget *parent = nullptr);
    void showAndActivate();
    void setCloseToTrayEnabled(bool enabled);
    void setVisibleColumns(const QVector<bool> &visibleColumns);
    void setPreviewLineCount(int lineCount);
    void setSearchMode(SearchMode mode);
    SearchMode searchMode() const;
    void setRegexStrict(bool enabled);

signals:
    void closeToTrayRequested();
    void visibleColumnsChanged(const QVector<bool> &visibleColumns);
    void searchModeChanged(const QString &mode);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void loadInitial();
    void loadMore();
    void activateSelected();
    void activateEntry(qint64 entryId, const QString &preferredFormat);
    void pinSelected();
    void deleteSelected();
    void clearHistory();

    void refresh(bool resetCursor);
    void startPendingSearch();
    void setMutationBusy(bool busy);
    qint64 selectedEntryId() const;
    void movePinnedEntry(qint64 entryId, int targetPinnedIndex);
    void updatePinnedReorderEnabled();
    void applyTableLayout();
    void showHeaderContextMenu(const QPoint &position);
    void showEntryContextMenu(const QPoint &position);
    void inspectEntry(qint64 entryId);
    void setSearchError(const QString &message);
    void syncSearchModeCombo();

    IpcAsyncRunner *m_ipcRunner = nullptr;
    HistoryModel *m_model = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_searchModeCombo = nullptr;
    QLabel *m_searchErrorLabel = nullptr;
    QTableView *m_table = nullptr;
    PreviewTextDelegate *m_previewDelegate = nullptr;
    QPushButton *m_loadMoreButton = nullptr;
    QPushButton *m_activateButton = nullptr;
    QPushButton *m_pinButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QTimer *m_searchTimer = nullptr;
    QTimer *m_newHighlightTimer = nullptr;
    ClipboardInspectorDialog *m_clipboardInspectorDialog = nullptr;
    int m_cursor = 0;
    bool m_closeToTrayEnabled = true;
    bool m_pinnedReorderEnabled = false;
    QVector<bool> m_visibleColumns = {true, true, true, true};
    int m_previewLineCount = 2;
    SearchMode m_searchMode = SearchMode::Plain;
    bool m_regexStrict = false;
    bool m_searchInFlight = false;
    bool m_searchPending = false;
    bool m_pendingSearchResetCursor = true;
    QCborMap m_pendingSearchParams;
    bool m_mutationBusy = false;
};

}  // namespace pastetry
