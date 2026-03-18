#include "clip-ui/settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace pastetry {

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Settings"));
    resize(460, 220);

    auto *mainLayout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    auto *shortcutRow = new QWidget(this);
    auto *shortcutLayout = new QHBoxLayout(shortcutRow);
    shortcutLayout->setContentsMargins(0, 0, 0, 0);

    m_shortcutEdit = new QKeySequenceEdit(shortcutRow);
    m_shortcutEdit->setClearButtonEnabled(true);
    auto *clearButton = new QPushButton(QStringLiteral("Disable"), shortcutRow);

    shortcutLayout->addWidget(m_shortcutEdit, 1);
    shortcutLayout->addWidget(clearButton);

    m_shortcutStatus = new QLabel(this);
    m_shortcutStatus->setWordWrap(true);

    m_startToTray = new QCheckBox(QStringLiteral("Start minimized to tray"), this);

    form->addRow(QStringLiteral("Global shortcut"), shortcutRow);
    form->addRow(QStringLiteral("Shortcut status"), m_shortcutStatus);
    form->addRow(QString(), m_startToTray);

    auto *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    mainLayout->addLayout(form);
    mainLayout->addStretch(1);
    mainLayout->addWidget(buttons);

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_shortcutEdit->clear();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::setValues(const QKeySequence &shortcut, bool startToTray,
                               const QString &shortcutStatusText) {
    m_shortcutEdit->setKeySequence(shortcut);
    m_startToTray->setChecked(startToTray);
    m_shortcutStatus->setText(shortcutStatusText);
}

QKeySequence SettingsDialog::shortcut() const {
    return m_shortcutEdit->keySequence();
}

bool SettingsDialog::startToTray() const {
    return m_startToTray->isChecked();
}

}  // namespace pastetry
