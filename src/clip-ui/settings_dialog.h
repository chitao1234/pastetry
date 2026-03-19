#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QVector>

class QCheckBox;
class QAbstractButton;
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
                   const QVector<bool> &quickPasteColumns, int previewLineCount,
                   bool regexStrictFullScan);
    void setShortcutStatusText(const QString &shortcutStatusText);

    QKeySequence shortcut() const;
    bool startToTray() const;
    QVector<bool> historyColumns() const;
    QVector<bool> quickPasteColumns() const;
    int previewLineCount() const;
    bool regexStrictFullScanEnabled() const;

signals:
    void applyRequested();
    void shortcutEdited(const QKeySequence &shortcut);

private:
    QVector<bool> columnsFromChecks(const QVector<QCheckBox *> &checks) const;
    bool hasUnsavedChanges() const;
    void refreshApplyButtonState();

    QKeySequenceEdit *m_shortcutEdit = nullptr;
    QCheckBox *m_startToTray = nullptr;
    QLabel *m_shortcutStatus = nullptr;
    QSpinBox *m_previewLines = nullptr;
    QCheckBox *m_regexStrictFullScan = nullptr;
    QAbstractButton *m_applyButton = nullptr;
    QVector<QCheckBox *> m_historyColumnChecks;
    QVector<QCheckBox *> m_quickPasteColumnChecks;

    QKeySequence m_savedShortcut;
    bool m_savedStartToTray = true;
    QVector<bool> m_savedHistoryColumns;
    QVector<bool> m_savedQuickPasteColumns;
    int m_savedPreviewLines = 2;
    bool m_savedRegexStrictFullScan = false;
    bool m_loadingValues = false;
};

}  // namespace pastetry
