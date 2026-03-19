#pragma once

#include "clip-ui/history_model.h"
#include "clip-ui/ipc_async_runner.h"

#include <QCborMap>
#include <QDialog>
#include <QVector>

class QLineEdit;
class QPoint;
class QTableView;
class QTimer;
class QHideEvent;
class QLabel;
class QComboBox;

namespace pastetry {

class PreviewTextDelegate;
class ClipboardInspectorDialog;

class QuickPasteDialog : public QDialog {
    Q_OBJECT

public:
    enum class PopupPositionMode {
        Caret,
        Cursor,
        LastLocation,
    };

    explicit QuickPasteDialog(IpcAsyncRunner *ipcRunner, QWidget *parent = nullptr);
    void setVisibleColumns(const QVector<bool> &visibleColumns);
    void setPreviewLineCount(int lineCount);
    void setSearchMode(SearchMode mode);
    SearchMode searchMode() const;
    void setRegexStrict(bool enabled);
    void setPopupPositionMode(PopupPositionMode mode);
    PopupPositionMode popupPositionMode() const;
    void setLastPopupPosition(const QPoint &position, bool hasPosition);
    QPoint lastPopupPosition() const;
    bool hasLastPopupPosition() const;

public slots:
    void openPopup();
    void togglePopup();

signals:
    void entryActivated();
    void errorOccurred(const QString &error);
    void popupHidden();
    void visibleColumnsChanged(const QVector<bool> &visibleColumns);
    void searchModeChanged(const QString &mode);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool event(QEvent *event) override;

private:
    void refreshResults();
    void startPendingSearch();
    void setMutationBusy(bool busy);
    void activateCurrent();
    void activateEntryById(qint64 entryId, const QString &preferredFormat);
    void pinSelected();
    void deleteSelected();
    qint64 selectedEntryId() const;
    void applyTableLayout();
    void showHeaderContextMenu(const QPoint &position);
    void showEntryContextMenu(const QPoint &position);
    void inspectEntry(qint64 entryId);
    void setSearchError(const QString &message);
    void syncSearchModeCombo();
    void scheduleDeferredHide();
    void cancelDeferredHide();

    IpcAsyncRunner *m_ipcRunner = nullptr;
    HistoryModel *m_model = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_searchModeCombo = nullptr;
    QLabel *m_searchErrorLabel = nullptr;
    QTableView *m_table = nullptr;
    PreviewTextDelegate *m_previewDelegate = nullptr;
    QTimer *m_searchTimer = nullptr;
    QTimer *m_newHighlightTimer = nullptr;
    QTimer *m_deferredHideTimer = nullptr;
    ClipboardInspectorDialog *m_clipboardInspectorDialog = nullptr;
    QVector<bool> m_visibleColumns = {true, true, true, true};
    int m_previewLineCount = 2;
    SearchMode m_searchMode = SearchMode::Plain;
    bool m_regexStrict = false;
    PopupPositionMode m_popupPositionMode = PopupPositionMode::Cursor;
    QPoint m_lastPopupPosition;
    bool m_hasLastPopupPosition = false;
    bool m_searchInFlight = false;
    bool m_searchPending = false;
    QCborMap m_pendingSearchParams;
    bool m_mutationBusy = false;
};

}  // namespace pastetry
