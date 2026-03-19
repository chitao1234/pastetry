#pragma once

#include "common/models.h"

#include <QDialog>
#include <QKeySequence>
#include <QVector>

class QCheckBox;
class QAbstractButton;
class QKeySequenceEdit;
class QLabel;
class QSpinBox;
class QComboBox;
class QPlainTextEdit;

namespace pastetry {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setValues(const QKeySequence &quickPasteShortcut,
                   const QKeySequence &openHistoryShortcut,
                   const QKeySequence &openInspectorShortcut,
                   bool startToTray,
                   const QString &quickPasteShortcutStatusText,
                   const QString &openHistoryShortcutStatusText,
                   const QString &openInspectorShortcutStatusText,
                   const QVector<bool> &historyColumns,
                   const QVector<bool> &quickPasteColumns, int previewLineCount,
                   bool regexStrictFullScan,
                   const CapturePolicy &capturePolicy);
    void setShortcutStatusTexts(const QString &quickPasteShortcutStatusText,
                                const QString &openHistoryShortcutStatusText,
                                const QString &openInspectorShortcutStatusText);
    void setShortcutConflictState(bool hasConflict, const QString &message);
    QKeySequence quickPasteShortcut() const;
    QKeySequence openHistoryShortcut() const;
    QKeySequence openInspectorShortcut() const;
    bool startToTray() const;
    QVector<bool> historyColumns() const;
    QVector<bool> quickPasteColumns() const;
    int previewLineCount() const;
    bool regexStrictFullScanEnabled() const;
    CapturePolicy capturePolicy() const;

signals:
    void applyRequested();
    void shortcutsEdited();

private:
    QVector<bool> columnsFromChecks(const QVector<QCheckBox *> &checks) const;
    bool hasUnsavedChanges() const;
    void refreshApplyButtonState();
    QVector<QKeySequence> globalShortcuts() const;
    static constexpr int kShortcutActionCount = 3;
    QVector<QKeySequenceEdit *> m_shortcutEdits;
    QVector<QLabel *> m_shortcutStatusLabels;
    QLabel *m_shortcutConflictLabel = nullptr;
    QCheckBox *m_startToTray = nullptr;
    QSpinBox *m_previewLines = nullptr;
    QCheckBox *m_regexStrictFullScan = nullptr;
    QComboBox *m_captureProfile = nullptr;
    QSpinBox *m_maxFormatMb = nullptr;
    QSpinBox *m_maxEntryMb = nullptr;
    QPlainTextEdit *m_customAllowlist = nullptr;
    QAbstractButton *m_applyButton = nullptr;
    QAbstractButton *m_okButton = nullptr;
    QVector<QCheckBox *> m_historyColumnChecks;
    QVector<QCheckBox *> m_quickPasteColumnChecks;

    QVector<QKeySequence> m_savedShortcuts;
    bool m_savedStartToTray = true;
    QVector<bool> m_savedHistoryColumns;
    QVector<bool> m_savedQuickPasteColumns;
    int m_savedPreviewLines = 2;
    bool m_savedRegexStrictFullScan = false;
    CapturePolicy m_savedCapturePolicy;
    bool m_hasShortcutConflict = false;
    bool m_loadingValues = false;
};

}  // namespace pastetry
