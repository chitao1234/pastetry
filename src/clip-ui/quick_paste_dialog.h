#pragma once

#include "clip-ui/history_model.h"
#include "common/ipc_client.h"

#include <QDialog>

class QLineEdit;
class QTableView;
class QTimer;
class QHideEvent;

namespace pastetry {

class QuickPasteDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickPasteDialog(IpcClient client, QWidget *parent = nullptr);

public slots:
    void openPopup();
    void togglePopup();

signals:
    void entryActivated();
    void errorOccurred(const QString &error);
    void popupHidden();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refreshResults();
    void activateCurrent();
    qint64 selectedEntryId() const;

    IpcClient m_client;
    HistoryModel *m_model = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QTableView *m_table = nullptr;
    QTimer *m_searchTimer = nullptr;
};

}  // namespace pastetry
