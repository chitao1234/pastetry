#pragma once

#include "clip-ui/shortcut_config.h"
#include "common/models.h"

#include <QDialog>
#include <QHash>
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

    void setValues(const QHash<QString, ShortcutBindingConfig> &shortcutBindings,
                   const QHash<QString, QString> &shortcutStatusTextByAction,
                   const QKeySequence &autoPasteKey,
                   bool startToTray,
                   bool hasShortcutConflict,
                   const QString &shortcutConflictMessage,
                   const QVector<bool> &historyColumns,
                   const QVector<bool> &quickPasteColumns, int previewLineCount,
                   bool regexStrictFullScan,
                   const CapturePolicy &capturePolicy);
    void setShortcutStatusTexts(
        const QHash<QString, QString> &shortcutStatusTextByAction);
    void setShortcutConflictState(bool hasConflict, const QString &message);
    QHash<QString, ShortcutBindingConfig> shortcutBindings() const;
    QKeySequence autoPasteKey() const;
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
    struct ShortcutRowWidgets {
        QComboBox *mode = nullptr;
        QKeySequenceEdit *directEdit = nullptr;
        QKeySequenceEdit *chordFirstEdit = nullptr;
        QKeySequenceEdit *chordSecondEdit = nullptr;
        QLabel *statusLabel = nullptr;
    };

    ShortcutBindingConfig bindingForAction(const QString &actionId) const;
    void setBindingForAction(const QString &actionId,
                             const ShortcutBindingConfig &binding);
    void refreshShortcutRowState(const QString &actionId);
    QVector<bool> columnsFromChecks(const QVector<QCheckBox *> &checks) const;
    bool hasUnsavedChanges() const;
    void refreshApplyButtonState();
    QHash<QString, ShortcutBindingConfig> allShortcutBindings() const;

    QHash<QString, ShortcutRowWidgets> m_shortcutRows;
    QLabel *m_shortcutConflictLabel = nullptr;
    QKeySequenceEdit *m_autoPasteKeyEdit = nullptr;
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

    QHash<QString, ShortcutBindingConfig> m_savedShortcutBindings;
    QKeySequence m_savedAutoPasteKey;
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
