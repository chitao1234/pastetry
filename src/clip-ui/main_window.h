#pragma once

#include "clip-ui/history_model.h"
#include "common/ipc_client.h"

#include <QMainWindow>

class QLineEdit;
class QPushButton;
class QTableView;
class QTimer;

namespace pastetry {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(IpcClient client, QWidget *parent = nullptr);

private:
    void loadInitial();
    void loadMore();
    void activateSelected();
    void pinSelected();
    void deleteSelected();
    void clearHistory();

    void refresh(bool resetCursor);
    qint64 selectedEntryId() const;

    IpcClient m_client;
    HistoryModel *m_model = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QTableView *m_table = nullptr;
    QPushButton *m_loadMoreButton = nullptr;
    QPushButton *m_activateButton = nullptr;
    QPushButton *m_pinButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QTimer *m_searchTimer = nullptr;
    int m_cursor = 0;
};

}  // namespace pastetry
