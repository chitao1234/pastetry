#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QVector>

class QCheckBox;
class QKeySequenceEdit;
class QLabel;
class QSpinBox;

namespace pastetry {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setValues(const QKeySequence &shortcut, bool startToTray,
                   const QString &shortcutStatusText,
                   const QVector<bool> &historyColumns,
                   const QVector<bool> &quickPasteColumns, int previewLineCount);
    void setShortcutStatusText(const QString &shortcutStatusText);

    QKeySequence shortcut() const;
    bool startToTray() const;
    QVector<bool> historyColumns() const;
    QVector<bool> quickPasteColumns() const;
    int previewLineCount() const;

signals:
    void applyRequested();

private:
    QVector<bool> columnsFromChecks(const QVector<QCheckBox *> &checks) const;

    QKeySequenceEdit *m_shortcutEdit = nullptr;
    QCheckBox *m_startToTray = nullptr;
    QLabel *m_shortcutStatus = nullptr;
    QSpinBox *m_previewLines = nullptr;
    QVector<QCheckBox *> m_historyColumnChecks;
    QVector<QCheckBox *> m_quickPasteColumnChecks;
};

}  // namespace pastetry
