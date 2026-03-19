#pragma once

#include "clip-ui/history_model.h"
#include "common/ipc_client.h"

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

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(IpcClient client, QWidget *parent = nullptr);
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
    void pinSelected();
    void deleteSelected();
    void clearHistory();

    void refresh(bool resetCursor);
    qint64 selectedEntryId() const;
    void applyTableLayout();
    void showHeaderContextMenu(const QPoint &position);
    void setSearchError(const QString &message);
    void syncSearchModeCombo();

    IpcClient m_client;
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
    int m_cursor = 0;
    bool m_closeToTrayEnabled = true;
    QVector<bool> m_visibleColumns = {true, true, true, true};
    int m_previewLineCount = 2;
    SearchMode m_searchMode = SearchMode::Plain;
    bool m_regexStrict = false;
};

}  // namespace pastetry
