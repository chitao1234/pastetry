#include "clip-ui/settings_dialog.h"

#include "clip-ui/history_model.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pastetry {
namespace {

QString columnLabel(int column) {
    switch (column) {
        case HistoryModel::TimeColumn:
            return QStringLiteral("Time");
        case HistoryModel::PreviewColumn:
            return QStringLiteral("Preview");
        case HistoryModel::FormatsColumn:
            return QStringLiteral("Formats");
        case HistoryModel::PinnedColumn:
            return QStringLiteral("Pinned");
        default:
            return QStringLiteral("Unknown");
    }
}

int captureProfileComboIndex(CaptureProfile profile) {
    switch (profile) {
        case CaptureProfile::Strict:
            return 0;
        case CaptureProfile::Broad:
            return 2;
        case CaptureProfile::Balanced:
        default:
            return 1;
    }
}

CaptureProfile captureProfileFromComboIndex(int index) {
    switch (index) {
        case 0:
            return CaptureProfile::Strict;
        case 2:
            return CaptureProfile::Broad;
        case 1:
        default:
            return CaptureProfile::Balanced;
    }
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Settings"));
    resize(560, 360);

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

    auto *columnsWidget = new QWidget(this);
    auto *columnsLayout = new QGridLayout(columnsWidget);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    columnsLayout->setColumnStretch(0, 1);
    columnsLayout->setColumnStretch(1, 1);

    auto *historyLabel = new QLabel(QStringLiteral("History view"), columnsWidget);
    auto *quickLabel = new QLabel(QStringLiteral("Quick paste view"), columnsWidget);
    columnsLayout->addWidget(historyLabel, 0, 0);
    columnsLayout->addWidget(quickLabel, 0, 1);

    auto *historyContainer = new QWidget(columnsWidget);
    auto *historyColumnLayout = new QVBoxLayout(historyContainer);
    historyColumnLayout->setContentsMargins(0, 0, 0, 0);
    auto *quickContainer = new QWidget(columnsWidget);
    auto *quickColumnLayout = new QVBoxLayout(quickContainer);
    quickColumnLayout->setContentsMargins(0, 0, 0, 0);

    m_historyColumnChecks.resize(HistoryModel::ColumnCount);
    m_quickPasteColumnChecks.resize(HistoryModel::ColumnCount);
    for (int column = 0; column < HistoryModel::ColumnCount; ++column) {
        auto *historyCheck = new QCheckBox(columnLabel(column), historyContainer);
        auto *quickCheck = new QCheckBox(columnLabel(column), quickContainer);
        m_historyColumnChecks[column] = historyCheck;
        m_quickPasteColumnChecks[column] = quickCheck;
        historyColumnLayout->addWidget(historyCheck);
        quickColumnLayout->addWidget(quickCheck);
    }

    columnsLayout->addWidget(historyContainer, 1, 0);
    columnsLayout->addWidget(quickContainer, 1, 1);

    m_previewLines = new QSpinBox(this);
    m_previewLines->setMinimum(1);
    m_previewLines->setMaximum(12);
    m_regexStrictFullScan =
        new QCheckBox(QStringLiteral("Regex strict mode scans full history"), this);

    auto *capturePolicyWidget = new QWidget(this);
    auto *capturePolicyLayout = new QGridLayout(capturePolicyWidget);
    capturePolicyLayout->setContentsMargins(0, 0, 0, 0);

    auto *profileLabel = new QLabel(QStringLiteral("Profile"), capturePolicyWidget);
    m_captureProfile = new QComboBox(capturePolicyWidget);
    m_captureProfile->addItem(QStringLiteral("Strict"));
    m_captureProfile->addItem(QStringLiteral("Balanced"));
    m_captureProfile->addItem(QStringLiteral("Broad"));
    m_captureProfile->setToolTip(
        QStringLiteral("Strict: common formats only\n"
                       "Balanced: strict + common app formats\n"
                       "Broad: almost all MIME formats"));

    auto *maxFormatLabel = new QLabel(QStringLiteral("Max per format (MB)"),
                                      capturePolicyWidget);
    m_maxFormatMb = new QSpinBox(capturePolicyWidget);
    m_maxFormatMb->setRange(1, 1024);

    auto *maxEntryLabel = new QLabel(QStringLiteral("Max per entry (MB)"),
                                     capturePolicyWidget);
    m_maxEntryMb = new QSpinBox(capturePolicyWidget);
    m_maxEntryMb->setRange(1, 1024);

    auto *customLabel = new QLabel(QStringLiteral("Custom allowlist patterns"),
                                   capturePolicyWidget);
    m_customAllowlist = new QPlainTextEdit(capturePolicyWidget);
    m_customAllowlist->setPlaceholderText(
        QStringLiteral("One MIME pattern per line, e.g.\n"
                       "application/x-special\n"
                       "application/vnd.*\n"
                       "# lines starting with # are ignored"));
    m_customAllowlist->setFixedHeight(110);

    capturePolicyLayout->addWidget(profileLabel, 0, 0);
    capturePolicyLayout->addWidget(m_captureProfile, 0, 1);
    capturePolicyLayout->addWidget(maxFormatLabel, 1, 0);
    capturePolicyLayout->addWidget(m_maxFormatMb, 1, 1);
    capturePolicyLayout->addWidget(maxEntryLabel, 2, 0);
    capturePolicyLayout->addWidget(m_maxEntryMb, 2, 1);
    capturePolicyLayout->addWidget(customLabel, 3, 0, Qt::AlignTop);
    capturePolicyLayout->addWidget(m_customAllowlist, 3, 1);

    form->addRow(QStringLiteral("Global shortcut"), shortcutRow);
    form->addRow(QStringLiteral("Shortcut status"), m_shortcutStatus);
    form->addRow(QStringLiteral("Visible columns"), columnsWidget);
    form->addRow(QStringLiteral("Preview lines"), m_previewLines);
    form->addRow(QStringLiteral("Search"), m_regexStrictFullScan);
    form->addRow(QStringLiteral("Rich format capture"), capturePolicyWidget);
    form->addRow(QString(), m_startToTray);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);

    mainLayout->addLayout(form);
    mainLayout->addStretch(1);
    mainLayout->addWidget(buttons);

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_shortcutEdit->clear();
    });
    connect(m_shortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            &SettingsDialog::shortcutEdited);
    connect(m_shortcutEdit, &QKeySequenceEdit::keySequenceChanged, this,
            [this] { refreshApplyButtonState(); });
    connect(m_startToTray, &QCheckBox::toggled, this,
            [this] { refreshApplyButtonState(); });
    connect(m_previewLines, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { refreshApplyButtonState(); });
    connect(m_regexStrictFullScan, &QCheckBox::toggled, this,
            [this] { refreshApplyButtonState(); });
    connect(m_captureProfile, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { refreshApplyButtonState(); });
    connect(m_maxFormatMb, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { refreshApplyButtonState(); });
    connect(m_maxEntryMb, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { refreshApplyButtonState(); });
    connect(m_customAllowlist, &QPlainTextEdit::textChanged, this,
            [this] { refreshApplyButtonState(); });
    for (auto *check : m_historyColumnChecks) {
        connect(check, &QCheckBox::toggled, this,
                [this] { refreshApplyButtonState(); });
    }
    for (auto *check : m_quickPasteColumnChecks) {
        connect(check, &QCheckBox::toggled, this,
                [this] { refreshApplyButtonState(); });
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_applyButton = buttons->button(QDialogButtonBox::Apply);
    if (m_applyButton) {
        connect(m_applyButton, &QAbstractButton::clicked, this,
                &SettingsDialog::applyRequested);
    }
    refreshApplyButtonState();
}

void SettingsDialog::setValues(const QKeySequence &shortcut, bool startToTray,
                               const QString &shortcutStatusText,
                               const QVector<bool> &historyColumns,
                               const QVector<bool> &quickPasteColumns,
                               int previewLineCount,
                               bool regexStrictFullScan,
                               const CapturePolicy &capturePolicy) {
    m_loadingValues = true;
    m_shortcutEdit->setKeySequence(shortcut);
    m_startToTray->setChecked(startToTray);
    setShortcutStatusText(shortcutStatusText);

    for (int column = 0; column < HistoryModel::ColumnCount; ++column) {
        const bool historyVisible =
            column < historyColumns.size() ? historyColumns.at(column) : true;
        const bool quickVisible =
            column < quickPasteColumns.size() ? quickPasteColumns.at(column) : true;

        m_historyColumnChecks[column]->setChecked(historyVisible);
        m_quickPasteColumnChecks[column]->setChecked(quickVisible);
    }

    m_previewLines->setValue(qBound(1, previewLineCount, 12));
    m_regexStrictFullScan->setChecked(regexStrictFullScan);
    m_captureProfile->setCurrentIndex(captureProfileComboIndex(capturePolicy.profile));
    m_maxFormatMb->setValue(
        qBound(1, static_cast<int>(capturePolicy.maxFormatBytes / (1024 * 1024)), 1024));
    m_maxEntryMb->setValue(
        qBound(1, static_cast<int>(capturePolicy.maxEntryBytes / (1024 * 1024)), 1024));
    m_customAllowlist->setPlainText(capturePolicy.customAllowlistPatterns.join('\n'));

    m_savedShortcut = shortcut;
    m_savedStartToTray = startToTray;
    m_savedHistoryColumns = columnsFromChecks(m_historyColumnChecks);
    m_savedQuickPasteColumns = columnsFromChecks(m_quickPasteColumnChecks);
    m_savedPreviewLines = m_previewLines->value();
    m_savedRegexStrictFullScan = regexStrictFullScan;
    m_savedCapturePolicy = this->capturePolicy();
    m_loadingValues = false;
    refreshApplyButtonState();
}

void SettingsDialog::setShortcutStatusText(const QString &shortcutStatusText) {
    m_shortcutStatus->setText(shortcutStatusText);
}

QKeySequence SettingsDialog::shortcut() const {
    return m_shortcutEdit->keySequence();
}

bool SettingsDialog::startToTray() const {
    return m_startToTray->isChecked();
}

QVector<bool> SettingsDialog::columnsFromChecks(const QVector<QCheckBox *> &checks) const {
    QVector<bool> visible;
    visible.reserve(checks.size());
    for (auto *check : checks) {
        visible.push_back(check && check->isChecked());
    }
    return visible;
}

QVector<bool> SettingsDialog::historyColumns() const {
    return columnsFromChecks(m_historyColumnChecks);
}

QVector<bool> SettingsDialog::quickPasteColumns() const {
    return columnsFromChecks(m_quickPasteColumnChecks);
}

int SettingsDialog::previewLineCount() const {
    return m_previewLines->value();
}

bool SettingsDialog::regexStrictFullScanEnabled() const {
    return m_regexStrictFullScan->isChecked();
}

CapturePolicy SettingsDialog::capturePolicy() const {
    CapturePolicy policy;
    policy.profile = captureProfileFromComboIndex(m_captureProfile->currentIndex());
    policy.maxFormatBytes = static_cast<qint64>(m_maxFormatMb->value()) * 1024 * 1024;
    policy.maxEntryBytes = static_cast<qint64>(m_maxEntryMb->value()) * 1024 * 1024;
    policy.customAllowlistPatterns.clear();
    for (const QString &line :
         m_customAllowlist->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            policy.customAllowlistPatterns.push_back(trimmed);
        }
    }
    return policy;
}

bool SettingsDialog::hasUnsavedChanges() const {
    const CapturePolicy currentPolicy = capturePolicy();
    const bool capturePolicyChanged =
        currentPolicy.profile != m_savedCapturePolicy.profile ||
        currentPolicy.maxFormatBytes != m_savedCapturePolicy.maxFormatBytes ||
        currentPolicy.maxEntryBytes != m_savedCapturePolicy.maxEntryBytes ||
        currentPolicy.customAllowlistPatterns != m_savedCapturePolicy.customAllowlistPatterns;

    return shortcut() != m_savedShortcut || startToTray() != m_savedStartToTray ||
           historyColumns() != m_savedHistoryColumns ||
           quickPasteColumns() != m_savedQuickPasteColumns ||
           previewLineCount() != m_savedPreviewLines ||
           regexStrictFullScanEnabled() != m_savedRegexStrictFullScan ||
           capturePolicyChanged;
}

void SettingsDialog::refreshApplyButtonState() {
    if (!m_applyButton) {
        return;
    }

    const bool enabled = !m_loadingValues && hasUnsavedChanges();
    m_applyButton->setEnabled(enabled);
}

}  // namespace pastetry
