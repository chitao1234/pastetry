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
#include <QScrollArea>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QSpinBox>
#include <QTabWidget>
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

QString modeLabel(ShortcutBindingMode mode) {
    switch (mode) {
        case ShortcutBindingMode::Direct:
            return QStringLiteral("Direct");
        case ShortcutBindingMode::Chord:
            return QStringLiteral("Chord");
        case ShortcutBindingMode::Disabled:
        default:
            return QStringLiteral("Disabled");
    }
}

int modeComboIndex(ShortcutBindingMode mode) {
    switch (mode) {
        case ShortcutBindingMode::Direct:
            return 1;
        case ShortcutBindingMode::Chord:
            return 2;
        case ShortcutBindingMode::Disabled:
        default:
            return 0;
    }
}

ShortcutBindingMode modeFromComboIndex(int index) {
    switch (index) {
        case 1:
            return ShortcutBindingMode::Direct;
        case 2:
            return ShortcutBindingMode::Chord;
        case 0:
        default:
            return ShortcutBindingMode::Disabled;
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
    resize(980, 560);

    auto *mainLayout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    mainLayout->addWidget(tabs, 1);

    auto *shortcutsTab = new QWidget(tabs);
    auto *shortcutsLayout = new QVBoxLayout(shortcutsTab);

    auto *autoPasteRow = new QWidget(shortcutsTab);
    auto *autoPasteLayout = new QHBoxLayout(autoPasteRow);
    autoPasteLayout->setContentsMargins(0, 0, 0, 0);
    auto *autoPasteLabel = new QLabel(QStringLiteral("Auto-paste key"), autoPasteRow);
    m_autoPasteKeyEdit = new QKeySequenceEdit(autoPasteRow);
    m_autoPasteKeyEdit->setClearButtonEnabled(true);
    auto *autoPasteClear = new QPushButton(QStringLiteral("Disable"), autoPasteRow);
    autoPasteLayout->addWidget(autoPasteLabel);
    autoPasteLayout->addWidget(m_autoPasteKeyEdit, 1);
    autoPasteLayout->addWidget(autoPasteClear);
    shortcutsLayout->addWidget(autoPasteRow);

    auto *hintLabel = new QLabel(
        QStringLiteral("Chord mode uses two global shortcuts (step 1 then step 2). "
                       "Direct mode uses a single global shortcut."),
        shortcutsTab);
    hintLabel->setWordWrap(true);
    shortcutsLayout->addWidget(hintLabel);

    auto *scroll = new QScrollArea(shortcutsTab);
    scroll->setWidgetResizable(true);
    auto *scrollContent = new QWidget(scroll);
    auto *grid = new QGridLayout(scrollContent);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 0);
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    grid->setColumnStretch(4, 1);
    grid->setColumnStretch(5, 2);

    grid->addWidget(new QLabel(QStringLiteral("Action"), scrollContent), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Mode"), scrollContent), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Direct"), scrollContent), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("Chord Step 1"), scrollContent), 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("Chord Step 2"), scrollContent), 0, 4);
    grid->addWidget(new QLabel(QStringLiteral("Status"), scrollContent), 0, 5);

    int row = 1;
    QString currentGroup;
    for (const auto &spec : allShortcutActionSpecs()) {
        if (spec.groupLabel != currentGroup) {
            currentGroup = spec.groupLabel;
            auto *groupLabel = new QLabel(currentGroup, scrollContent);
            QFont boldFont = groupLabel->font();
            boldFont.setBold(true);
            groupLabel->setFont(boldFont);
            grid->addWidget(groupLabel, row, 0, 1, 6);
            ++row;
        }

        ShortcutRowWidgets widgets;
        widgets.mode = new QComboBox(scrollContent);
        widgets.mode->addItem(modeLabel(ShortcutBindingMode::Disabled));
        widgets.mode->addItem(modeLabel(ShortcutBindingMode::Direct));
        widgets.mode->addItem(modeLabel(ShortcutBindingMode::Chord));

        widgets.directEdit = new QKeySequenceEdit(scrollContent);
        widgets.directEdit->setClearButtonEnabled(true);

        widgets.chordFirstEdit = new QKeySequenceEdit(scrollContent);
        widgets.chordFirstEdit->setClearButtonEnabled(true);

        widgets.chordSecondEdit = new QKeySequenceEdit(scrollContent);
        widgets.chordSecondEdit->setClearButtonEnabled(true);

        widgets.statusLabel = new QLabel(scrollContent);
        widgets.statusLabel->setWordWrap(false);
        widgets.statusLabel->setTextFormat(Qt::PlainText);
        widgets.statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto *nameLabel = new QLabel(spec.label, scrollContent);
        grid->addWidget(nameLabel, row, 0);
        grid->addWidget(widgets.mode, row, 1);
        grid->addWidget(widgets.directEdit, row, 2);
        grid->addWidget(widgets.chordFirstEdit, row, 3);
        grid->addWidget(widgets.chordSecondEdit, row, 4);
        grid->addWidget(widgets.statusLabel, row, 5);

        m_shortcutRows.insert(spec.id, widgets);

        connect(widgets.mode, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, actionId = spec.id](int) {
                    emit shortcutsEdited();
                    refreshShortcutRowState(actionId);
                    refreshApplyButtonState();
                });

        auto onEditChanged = [this](const QKeySequence &) {
            emit shortcutsEdited();
            refreshApplyButtonState();
        };
        connect(widgets.directEdit, &QKeySequenceEdit::keySequenceChanged, this,
                onEditChanged);
        connect(widgets.chordFirstEdit, &QKeySequenceEdit::keySequenceChanged, this,
                onEditChanged);
        connect(widgets.chordSecondEdit, &QKeySequenceEdit::keySequenceChanged, this,
                onEditChanged);

        ++row;
    }

    scrollContent->setLayout(grid);
    scroll->setWidget(scrollContent);
    shortcutsLayout->addWidget(scroll, 1);

    m_shortcutConflictLabel = new QLabel(shortcutsTab);
    m_shortcutConflictLabel->setWordWrap(true);
    m_shortcutConflictLabel->setStyleSheet(QStringLiteral("color: #b3412f;"));
    m_shortcutConflictLabel->setVisible(false);
    shortcutsLayout->addWidget(m_shortcutConflictLabel);
    tabs->addTab(shortcutsTab, QStringLiteral("Shortcuts"));

    auto *generalTab = new QWidget(tabs);
    auto *generalForm = new QFormLayout(generalTab);
    generalForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_startToTray = new QCheckBox(QStringLiteral("Start minimized to tray"), generalTab);
    generalForm->addRow(QString(), m_startToTray);
    generalForm->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum,
                                         QSizePolicy::Expanding));
    tabs->addTab(generalTab, QStringLiteral("General"));

    auto *viewsTab = new QWidget(tabs);
    auto *viewsForm = new QFormLayout(viewsTab);
    viewsForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto *columnsWidget = new QWidget(viewsTab);
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

    m_previewLines = new QSpinBox(viewsTab);
    m_previewLines->setMinimum(1);
    m_previewLines->setMaximum(12);
    m_regexStrictFullScan =
        new QCheckBox(QStringLiteral("Regex strict mode scans full history"), viewsTab);

    viewsForm->addRow(QStringLiteral("Visible columns"), columnsWidget);
    viewsForm->addRow(QStringLiteral("Preview lines"), m_previewLines);
    viewsForm->addRow(QStringLiteral("Search"), m_regexStrictFullScan);
    viewsForm->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum,
                                       QSizePolicy::Expanding));
    tabs->addTab(viewsTab, QStringLiteral("Views"));

    auto *richFormatTab = new QWidget(tabs);
    auto *richFormatForm = new QFormLayout(richFormatTab);
    richFormatForm->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    auto *capturePolicyWidget = new QWidget(richFormatTab);
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

    richFormatForm->addRow(QStringLiteral("Rich format capture"), capturePolicyWidget);
    richFormatForm->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum,
                                            QSizePolicy::Expanding));
    tabs->addTab(richFormatTab, QStringLiteral("Rich Formats"));

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttons);

    connect(autoPasteClear, &QPushButton::clicked, this,
            [this] { m_autoPasteKeyEdit->clear(); });
    connect(m_autoPasteKeyEdit, &QKeySequenceEdit::keySequenceChanged, this,
            [this](const QKeySequence &) { refreshApplyButtonState(); });

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
    m_okButton = buttons->button(QDialogButtonBox::Ok);
    if (m_applyButton) {
        connect(m_applyButton, &QAbstractButton::clicked, this,
                &SettingsDialog::applyRequested);
    }

    refreshApplyButtonState();
}

void SettingsDialog::setValues(
    const QHash<QString, ShortcutBindingConfig> &shortcutBindings,
    const QHash<QString, QString> &shortcutStatusTextByAction,
    const QKeySequence &autoPasteKey, bool startToTray, bool hasShortcutConflict,
    const QString &shortcutConflictMessage, const QVector<bool> &historyColumns,
    const QVector<bool> &quickPasteColumns, int previewLineCount,
    bool regexStrictFullScan, const CapturePolicy &capturePolicy) {
    m_loadingValues = true;

    for (const auto &spec : allShortcutActionSpecs()) {
        const ShortcutBindingConfig binding = shortcutBindings.contains(spec.id)
                                                  ? shortcutBindings.value(spec.id)
                                                  : ShortcutBindingConfig{};
        setBindingForAction(spec.id, binding);
    }

    m_autoPasteKeyEdit->setKeySequence(autoPasteKey);
    m_startToTray->setChecked(startToTray);
    setShortcutStatusTexts(shortcutStatusTextByAction);
    setShortcutConflictState(hasShortcutConflict, shortcutConflictMessage);

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

    m_savedShortcutBindings = allShortcutBindings();
    m_savedAutoPasteKey = m_autoPasteKeyEdit->keySequence();
    m_savedStartToTray = startToTray;
    m_savedHistoryColumns = columnsFromChecks(m_historyColumnChecks);
    m_savedQuickPasteColumns = columnsFromChecks(m_quickPasteColumnChecks);
    m_savedPreviewLines = m_previewLines->value();
    m_savedRegexStrictFullScan = regexStrictFullScan;
    m_savedCapturePolicy = this->capturePolicy();

    m_loadingValues = false;
    refreshApplyButtonState();
}

void SettingsDialog::setShortcutStatusTexts(
    const QHash<QString, QString> &shortcutStatusTextByAction) {
    for (const auto &spec : allShortcutActionSpecs()) {
        const auto it = m_shortcutRows.find(spec.id);
        if (it == m_shortcutRows.end() || !it->statusLabel) {
            continue;
        }
        const QString original = shortcutStatusTextByAction.value(spec.id);
        const QString simplified = original.simplified();
        it->statusLabel->setText(simplified);
        it->statusLabel->setToolTip(original);
    }
}

void SettingsDialog::setShortcutConflictState(bool hasConflict, const QString &message) {
    m_hasShortcutConflict = hasConflict;

    if (!m_shortcutConflictLabel) {
        refreshApplyButtonState();
        return;
    }

    const QString detail = message.trimmed().isEmpty()
                               ? QStringLiteral("Shortcut conflict detected")
                               : message.trimmed();
    m_shortcutConflictLabel->setVisible(hasConflict);
    m_shortcutConflictLabel->setText(hasConflict ? detail : QString());

    refreshApplyButtonState();
}

ShortcutBindingConfig SettingsDialog::bindingForAction(const QString &actionId) const {
    const auto it = m_shortcutRows.find(actionId);
    if (it == m_shortcutRows.end()) {
        return {};
    }

    ShortcutBindingConfig binding;
    binding.mode = modeFromComboIndex(it->mode ? it->mode->currentIndex() : 0);
    if (it->directEdit) {
        binding.directSequence = it->directEdit->keySequence();
    }
    if (it->chordFirstEdit) {
        binding.chordFirstSequence = it->chordFirstEdit->keySequence();
    }
    if (it->chordSecondEdit) {
        binding.chordSecondSequence = it->chordSecondEdit->keySequence();
    }
    return binding;
}

void SettingsDialog::setBindingForAction(const QString &actionId,
                                         const ShortcutBindingConfig &binding) {
    const auto it = m_shortcutRows.find(actionId);
    if (it == m_shortcutRows.end()) {
        return;
    }

    if (it->mode) {
        it->mode->setCurrentIndex(modeComboIndex(binding.mode));
    }
    if (it->directEdit) {
        it->directEdit->setKeySequence(binding.directSequence);
    }
    if (it->chordFirstEdit) {
        it->chordFirstEdit->setKeySequence(binding.chordFirstSequence);
    }
    if (it->chordSecondEdit) {
        it->chordSecondEdit->setKeySequence(binding.chordSecondSequence);
    }

    refreshShortcutRowState(actionId);
}

void SettingsDialog::refreshShortcutRowState(const QString &actionId) {
    const auto it = m_shortcutRows.find(actionId);
    if (it == m_shortcutRows.end()) {
        return;
    }

    const ShortcutBindingMode mode =
        modeFromComboIndex(it->mode ? it->mode->currentIndex() : 0);
    const bool directEnabled = mode == ShortcutBindingMode::Direct;
    const bool chordEnabled = mode == ShortcutBindingMode::Chord;

    if (it->directEdit) {
        it->directEdit->setEnabled(directEnabled);
    }
    if (it->chordFirstEdit) {
        it->chordFirstEdit->setEnabled(chordEnabled);
    }
    if (it->chordSecondEdit) {
        it->chordSecondEdit->setEnabled(chordEnabled);
    }
}

QHash<QString, ShortcutBindingConfig> SettingsDialog::allShortcutBindings() const {
    QHash<QString, ShortcutBindingConfig> bindings;
    for (const auto &spec : allShortcutActionSpecs()) {
        bindings.insert(spec.id, bindingForAction(spec.id));
    }
    return bindings;
}

QHash<QString, ShortcutBindingConfig> SettingsDialog::shortcutBindings() const {
    return allShortcutBindings();
}

QKeySequence SettingsDialog::autoPasteKey() const {
    if (!m_autoPasteKeyEdit) {
        return {};
    }
    return m_autoPasteKeyEdit->keySequence();
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

    return allShortcutBindings() != m_savedShortcutBindings ||
           autoPasteKey() != m_savedAutoPasteKey ||
           startToTray() != m_savedStartToTray ||
           historyColumns() != m_savedHistoryColumns ||
           quickPasteColumns() != m_savedQuickPasteColumns ||
           previewLineCount() != m_savedPreviewLines ||
           regexStrictFullScanEnabled() != m_savedRegexStrictFullScan ||
           capturePolicyChanged;
}

void SettingsDialog::refreshApplyButtonState() {
    if (m_applyButton) {
        const bool applyEnabled =
            !m_loadingValues && !m_hasShortcutConflict && hasUnsavedChanges();
        m_applyButton->setEnabled(applyEnabled);
    }

    if (m_okButton) {
        m_okButton->setEnabled(!m_hasShortcutConflict);
    }
}

}  // namespace pastetry
