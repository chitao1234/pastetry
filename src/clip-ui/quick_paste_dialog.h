#pragma once

#include "clip-ui/history_model.h"
#include "common/ipc_client.h"

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

class QuickPasteDialog : public QDialog {
    Q_OBJECT

public:
    explicit QuickPasteDialog(IpcClient client, QWidget *parent = nullptr);
    void setVisibleColumns(const QVector<bool> &visibleColumns);
    void setPreviewLineCount(int lineCount);
    void setSearchMode(SearchMode mode);
    SearchMode searchMode() const;
    void setRegexStrict(bool enabled);

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
    void activateCurrent();
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
    QTimer *m_searchTimer = nullptr;
    QTimer *m_newHighlightTimer = nullptr;
    QVector<bool> m_visibleColumns = {true, true, true, true};
    int m_previewLineCount = 2;
    SearchMode m_searchMode = SearchMode::Plain;
    bool m_regexStrict = false;
};

}  // namespace pastetry
