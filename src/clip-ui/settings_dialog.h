#pragma once

#include <QDialog>
#include <QKeySequence>

class QCheckBox;
class QKeySequenceEdit;
class QLabel;

namespace pastetry {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    void setValues(const QKeySequence &shortcut, bool startToTray,
                   const QString &shortcutStatusText);

    QKeySequence shortcut() const;
    bool startToTray() const;

private:
    QKeySequenceEdit *m_shortcutEdit = nullptr;
    QCheckBox *m_startToTray = nullptr;
    QLabel *m_shortcutStatus = nullptr;
};

}  // namespace pastetry
