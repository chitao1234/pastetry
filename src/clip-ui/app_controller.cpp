#include "clip-ui/app_controller.h"

#include "clip-ui/clipboard_inspector_dialog.h"
#include "clip-ui/settings_dialog.h"

#include <QAction>
#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QHash>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>

namespace pastetry {
namespace {

constexpr const char *kSettingsShortcut = "hotkey/sequence";
constexpr const char *kSettingsQuickPasteShortcut = "hotkey/quick_paste";
constexpr const char *kSettingsOpenHistoryShortcut = "hotkey/open_history";
constexpr const char *kSettingsOpenInspectorShortcut = "hotkey/open_inspector";
constexpr const char *kSettingsStartToTray = "ui/start_to_tray";
constexpr const char *kSettingsPopupGeometry = "popup/last_geometry";
constexpr const char *kSettingsHistoryColumns = "ui/history_columns";
constexpr const char *kSettingsQuickColumns = "ui/quick_paste_columns";
constexpr const char *kSettingsPreviewLines = "ui/preview_lines";
constexpr const char *kSettingsSearchMode = "search/mode";
constexpr const char *kSettingsRegexStrict = "search/regex_strict_full_scan";

QString shortcutStatusTextForState(const QKeySequence &shortcut,
                                   ShortcutRegistrationState state,
                                   const QString &errorText, bool isCurrentShortcut) {
    const QString detail =
        errorText.trimmed().isEmpty() ? QStringLiteral("Unknown error") : errorText;
    const QString sequenceText = shortcut.toString();

    switch (state) {
        case ShortcutRegistrationState::Registered:
            return isCurrentShortcut
                       ? QStringLiteral("Active: %1").arg(sequenceText)
                       : QStringLiteral("Available: %1").arg(sequenceText);
        case ShortcutRegistrationState::Disabled:
            return QStringLiteral("Disabled (no shortcut configured)");
        case ShortcutRegistrationState::Unavailable:
            return QStringLiteral("Unavailable: %1").arg(detail);
        case ShortcutRegistrationState::InvalidBinding:
            return QStringLiteral("Invalid binding: %1").arg(detail);
        case ShortcutRegistrationState::Failed:
            return QStringLiteral("Registration failed: %1").arg(detail);
        default:
            return QStringLiteral("Unknown shortcut state");
    }
}

QString shortcutConflictStatusText(const QString &otherActionLabel) {
    return QStringLiteral("Conflict: also assigned to %1").arg(otherActionLabel);
}

QString shortcutKey(const QKeySequence &shortcut) {
    return shortcut.toString(QKeySequence::PortableText).trimmed();
}

}  // namespace

AppController::AppController(AppPaths paths, QObject *parent)
    : QObject(parent),
      m_paths(std::move(paths)),
      m_client(m_paths.socketName),
      m_mainWindow(m_client),
      m_quickPasteDialog(m_client),
      m_quickPasteShortcutService(this, 1),
      m_openHistoryShortcutService(this, 2),
      m_openInspectorShortcutService(this, 3),
      m_settings(QStringLiteral("pastetry"), QStringLiteral("pastetry")),
      m_singleInstanceName(QStringLiteral("pastetry.clip-ui.instance.v1")) {
    m_mainWindow.setCloseToTrayEnabled(true);

    connect(&m_quickPasteShortcutService, &GlobalShortcutService::activated, this,
            &AppController::showQuickPastePopup);
    connect(&m_openHistoryShortcutService, &GlobalShortcutService::activated, this,
            &AppController::showMainWindow);
    connect(&m_openInspectorShortcutService, &GlobalShortcutService::activated, this,
            &AppController::openClipboardInspector);

    connect(&m_quickPasteDialog, &QuickPasteDialog::entryActivated, this, [this] {
        if (m_trayIcon && m_trayIcon->isVisible()) {
            m_trayIcon->showMessage(QStringLiteral("Pastetry"),
                                    QStringLiteral("Clipboard entry activated"),
                                    QSystemTrayIcon::Information, 1500);
        }
    });

    connect(&m_quickPasteDialog, &QuickPasteDialog::errorOccurred, this,
            [this](const QString &error) {
                QMessageBox::warning(&m_mainWindow, QStringLiteral("Quick paste error"), error);
            });

    connect(&m_quickPasteDialog, &QuickPasteDialog::popupHidden, this, [this] {
        m_popupGeometry = m_quickPasteDialog.saveGeometry();
        saveSettings();
    });

    connect(&m_mainWindow, &MainWindow::visibleColumnsChanged, this,
            [this](const QVector<bool> &visibleColumns) {
                m_historyColumns = normalizedColumns(visibleColumns);
                saveSettings();
            });

    connect(&m_quickPasteDialog, &QuickPasteDialog::visibleColumnsChanged, this,
            [this](const QVector<bool> &visibleColumns) {
                m_quickPasteColumns = normalizedColumns(visibleColumns);
                saveSettings();
            });
    connect(&m_mainWindow, &MainWindow::searchModeChanged, this,
            [this](const QString &modeText) {
                m_searchMode = searchModeFromString(modeText);
                m_quickPasteDialog.setSearchMode(m_searchMode);
                saveSettings();
            });
    connect(&m_quickPasteDialog, &QuickPasteDialog::searchModeChanged, this,
            [this](const QString &modeText) {
                m_searchMode = searchModeFromString(modeText);
                m_mainWindow.setSearchMode(m_searchMode);
                saveSettings();
            });

    connect(&m_mainWindow, &MainWindow::closeToTrayRequested, this, [this] {
        if (m_trayIcon && m_trayIcon->isVisible()) {
            m_trayIcon->showMessage(QStringLiteral("Pastetry"),
                                    QStringLiteral("Still running in tray"),
                                    QSystemTrayIcon::Information, 1500);
        }
    });

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        m_isQuitting = true;
        m_mainWindow.setCloseToTrayEnabled(false);
        m_quickPasteShortcutService.unregisterShortcut();
        m_openHistoryShortcutService.unregisterShortcut();
        m_openInspectorShortcutService.unregisterShortcut();
        saveSettings();
    });

    m_daemonHealthTimer.setInterval(4000);
    connect(&m_daemonHealthTimer, &QTimer::timeout, this,
            [this] { checkDaemonConnectivity(false); });
}

bool AppController::initialize(QString *error) {
    if (notifyExistingInstance()) {
        return false;
    }

    if (!startSingleInstanceServer(error)) {
        return false;
    }

    loadSettings();
    if (!m_popupGeometry.isEmpty()) {
        m_quickPasteDialog.restoreGeometry(m_popupGeometry);
    }
    applyViewSettings();

    setupTray();
    m_mainWindow.setCloseToTrayEnabled(m_startToTray && m_trayIcon &&
                                       m_trayIcon->isVisible());
    applyShortcutSettings();
    checkDaemonConnectivity(true);
    QString capturePolicyError;
    if (!loadCapturePolicyFromDaemon(&capturePolicyError) &&
        !capturePolicyError.trimmed().isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("Failed to load capture policy from daemon: %1")
                   .arg(capturePolicyError);
    }
    m_daemonHealthTimer.start();

    if (!m_startToTray || !m_trayIcon || !m_trayIcon->isVisible()) {
        showMainWindow();
    }

    return true;
}

void AppController::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        m_mainWindow.setCloseToTrayEnabled(false);
        return;
    }

    auto *menu = new QMenu(&m_mainWindow);

    m_openHistoryAction = menu->addAction(QStringLiteral("Open History"));
    m_openQuickPasteAction = menu->addAction(QStringLiteral("Open Quick Paste"));
    m_openClipboardInspectorAction =
        menu->addAction(QStringLiteral("Inspector"));
    menu->addSeparator();
    m_openSettingsAction = menu->addAction(QStringLiteral("Settings"));
    menu->addSeparator();
    m_quitAction = menu->addAction(QStringLiteral("Quit"));

    connect(m_openHistoryAction, &QAction::triggered, this,
            &AppController::showMainWindow);
    connect(m_openQuickPasteAction, &QAction::triggered, this,
            &AppController::showQuickPastePopup);
    connect(m_openClipboardInspectorAction, &QAction::triggered, this,
            &AppController::openClipboardInspector);
    connect(m_openSettingsAction, &QAction::triggered, this,
            &AppController::openSettings);
    connect(m_quitAction, &QAction::triggered, this,
            &AppController::handleQuitRequested);

    m_trayIcon = new QSystemTrayIcon(&m_mainWindow);
    QIcon icon = QIcon::fromTheme(QStringLiteral("edit-paste"));
    if (icon.isNull()) {
        icon = m_mainWindow.style()->standardIcon(QStyle::SP_DialogOpenButton);
    }

    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(QStringLiteral("Pastetry"));
    m_trayIcon->setContextMenu(menu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger ||
                    reason == QSystemTrayIcon::DoubleClick) {
                    showQuickPastePopup();
                }
            });
}

void AppController::showMainWindow() {
    m_mainWindow.showAndActivate();
}

void AppController::showQuickPastePopup() {
    m_quickPasteDialog.togglePopup();
}

void AppController::openClipboardInspector() {
    if (!m_clipboardInspectorDialog) {
        m_clipboardInspectorDialog =
            new ClipboardInspectorDialog(m_client, &m_mainWindow);
    }
    m_clipboardInspectorDialog->inspectClipboard();
}

void AppController::openSettings() {
    QString policyLoadError;
    if (!loadCapturePolicyFromDaemon(&policyLoadError) &&
        !policyLoadError.trimmed().isEmpty()) {
        QMessageBox::warning(
            &m_mainWindow, QStringLiteral("Capture policy unavailable"),
            QStringLiteral("Could not load rich-format capture policy from daemon.\n\n"
                           "Reason: %1")
                .arg(policyLoadError));
    }

    SettingsDialog dialog(&m_mainWindow);
    auto currentQuickPasteStatus = [&]() {
        return shortcutStatusTextForState(
            m_quickPasteShortcut, m_quickPasteShortcutState,
            m_quickPasteShortcutService.lastError(), true);
    };
    auto currentOpenHistoryStatus = [&]() {
        return shortcutStatusTextForState(
            m_openHistoryShortcut, m_openHistoryShortcutState,
            m_openHistoryShortcutService.lastError(), true);
    };
    auto currentOpenInspectorStatus = [&]() {
        return shortcutStatusTextForState(
            m_openInspectorShortcut, m_openInspectorShortcutState,
            m_openInspectorShortcutService.lastError(), true);
    };

    dialog.setValues(
        m_quickPasteShortcut, m_openHistoryShortcut, m_openInspectorShortcut,
        m_startToTray, currentQuickPasteStatus(), currentOpenHistoryStatus(),
        currentOpenInspectorStatus(), m_historyColumns, m_quickPasteColumns,
        m_previewLineCount, m_regexStrictFullScan, m_capturePolicy);

    GlobalShortcutService probeShortcutService(&dialog, 99);
    auto updateShortcutAvailability = [&]() {
        struct ShortcutAvailabilityInput {
            QString actionLabel;
            QKeySequence candidateSequence;
            QKeySequence activeSequence;
            ShortcutRegistrationState activeState = ShortcutRegistrationState::Disabled;
            QString activeError;
            QString statusText;
            int conflictWith = -1;
        };

        QVector<ShortcutAvailabilityInput> inputs = {
            {QStringLiteral("Quick paste popup"), dialog.quickPasteShortcut(),
             m_quickPasteShortcut, m_quickPasteShortcutState,
             m_quickPasteShortcutService.lastError()},
            {QStringLiteral("Open history window"), dialog.openHistoryShortcut(),
             m_openHistoryShortcut, m_openHistoryShortcutState,
             m_openHistoryShortcutService.lastError()},
            {QStringLiteral("Open clipboard inspector"), dialog.openInspectorShortcut(),
             m_openInspectorShortcut, m_openInspectorShortcutState,
             m_openInspectorShortcutService.lastError()},
        };

        QHash<QString, QVector<int>> shortcutUsage;
        for (int i = 0; i < inputs.size(); ++i) {
            const QString key = shortcutKey(inputs[i].candidateSequence);
            if (!key.isEmpty()) {
                shortcutUsage[key].push_back(i);
            }
        }

        bool hasConflict = false;
        for (auto it = shortcutUsage.cbegin(); it != shortcutUsage.cend(); ++it) {
            const QVector<int> usage = it.value();
            if (usage.size() < 2) {
                continue;
            }
            hasConflict = true;
            for (int index : usage) {
                int peerIndex = -1;
                for (int candidatePeer : usage) {
                    if (candidatePeer != index) {
                        peerIndex = candidatePeer;
                        break;
                    }
                }
                inputs[index].conflictWith = peerIndex;
            }
        }

        for (int i = 0; i < inputs.size(); ++i) {
            ShortcutAvailabilityInput &input = inputs[i];
            if (input.conflictWith >= 0) {
                input.statusText = shortcutConflictStatusText(
                    inputs[input.conflictWith].actionLabel);
                continue;
            }

            ShortcutRegistrationState state = ShortcutRegistrationState::Disabled;
            QString detail;
            const bool currentShortcut =
                (input.candidateSequence == input.activeSequence);
            if (currentShortcut) {
                state = input.activeState;
                detail = input.activeError;
            } else {
                state = probeShortcutService.registerShortcut(input.candidateSequence);
                detail = probeShortcutService.lastError();
                probeShortcutService.unregisterShortcut();
            }

            input.statusText = shortcutStatusTextForState(
                input.candidateSequence, state, detail, currentShortcut);
        }

        dialog.setShortcutStatusTexts(inputs[0].statusText, inputs[1].statusText,
                                      inputs[2].statusText);
        dialog.setShortcutConflictState(
            hasConflict,
            hasConflict
                ? QStringLiteral(
                      "Duplicate shortcuts are not allowed for Pastetry actions.")
                : QString());
    };

    auto applyFromDialog = [&]() {
        const CapturePolicy requestedPolicy = dialog.capturePolicy();
        QString policyApplyError;
        if (!applyCapturePolicyToDaemon(requestedPolicy, &policyApplyError)) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Capture policy apply failed"),
                QStringLiteral("Failed to update daemon capture policy.\n\nReason: %1")
                    .arg(policyApplyError.isEmpty()
                             ? QStringLiteral("Unknown error")
                             : policyApplyError));
            return;
        }

        m_quickPasteShortcut = dialog.quickPasteShortcut();
        m_openHistoryShortcut = dialog.openHistoryShortcut();
        m_openInspectorShortcut = dialog.openInspectorShortcut();
        m_startToTray = dialog.startToTray();
        m_historyColumns = normalizedColumns(dialog.historyColumns());
        m_quickPasteColumns = normalizedColumns(dialog.quickPasteColumns());
        m_previewLineCount = qBound(1, dialog.previewLineCount(), 12);
        m_regexStrictFullScan = dialog.regexStrictFullScanEnabled();

        m_mainWindow.setCloseToTrayEnabled(m_startToTray && m_trayIcon &&
                                           m_trayIcon->isVisible());

        applyViewSettings();
        applyShortcutSettings();
        saveSettings();
        dialog.setValues(
            m_quickPasteShortcut, m_openHistoryShortcut, m_openInspectorShortcut,
            m_startToTray, currentQuickPasteStatus(), currentOpenHistoryStatus(),
            currentOpenInspectorStatus(), m_historyColumns, m_quickPasteColumns,
            m_previewLineCount, m_regexStrictFullScan, m_capturePolicy);
        updateShortcutAvailability();
    };

    connect(&dialog, &SettingsDialog::applyRequested, &dialog, applyFromDialog);
    connect(&dialog, &SettingsDialog::shortcutsEdited, &dialog,
            updateShortcutAvailability);
    updateShortcutAvailability();

    if (dialog.exec() == QDialog::Accepted) {
        applyFromDialog();
    }
}

void AppController::handleQuitRequested() {
    m_isQuitting = true;
    m_mainWindow.setCloseToTrayEnabled(false);
    qApp->quit();
}

bool AppController::notifyExistingInstance() {
    QLocalSocket socket;
    socket.connectToServer(m_singleInstanceName);
    if (!socket.waitForConnected(80)) {
        return false;
    }

    socket.write("toggle-popup\n");
    socket.waitForBytesWritten(80);
    socket.disconnectFromServer();
    return true;
}

bool AppController::startSingleInstanceServer(QString *error) {
    m_singleInstanceServer = new QLocalServer(this);
    QLocalServer::removeServer(m_singleInstanceName);

    if (!m_singleInstanceServer->listen(m_singleInstanceName)) {
        if (error) {
            *error = m_singleInstanceServer->errorString();
        }
        return false;
    }

    connect(m_singleInstanceServer, &QLocalServer::newConnection, this, [this] {
        while (m_singleInstanceServer->hasPendingConnections()) {
            QLocalSocket *socket = m_singleInstanceServer->nextPendingConnection();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                const QString command =
                    QString::fromUtf8(socket->readAll()).trimmed();
                if (!command.isEmpty()) {
                    handleSingleInstanceCommand(command);
                }
            });
            connect(socket, &QLocalSocket::disconnected, socket,
                    &QLocalSocket::deleteLater);
        }
    });

    return true;
}

void AppController::handleSingleInstanceCommand(const QString &command) {
    if (command == QStringLiteral("show-main")) {
        showMainWindow();
        return;
    }

    showQuickPastePopup();
}

void AppController::loadSettings() {
    m_startToTray = m_settings.value(kSettingsStartToTray, true).toBool();

    const QString quickPasteSequence = m_settings.contains(kSettingsQuickPasteShortcut)
                                           ? m_settings.value(kSettingsQuickPasteShortcut)
                                                 .toString()
                                           : m_settings.value(kSettingsShortcut, QString())
                                                 .toString();
    m_quickPasteShortcut = QKeySequence::fromString(quickPasteSequence);
    m_openHistoryShortcut = QKeySequence::fromString(
        m_settings.value(kSettingsOpenHistoryShortcut, QString()).toString());
    m_openInspectorShortcut = QKeySequence::fromString(
        m_settings.value(kSettingsOpenInspectorShortcut, QString()).toString());

    m_popupGeometry = m_settings.value(kSettingsPopupGeometry).toByteArray();
    m_historyColumns = parseColumns(m_settings.value(kSettingsHistoryColumns).toString(),
                                    m_historyColumns);
    m_quickPasteColumns = parseColumns(m_settings.value(kSettingsQuickColumns).toString(),
                                       m_quickPasteColumns);
    m_previewLineCount = qBound(1, m_settings.value(kSettingsPreviewLines, 2).toInt(), 12);
    m_searchMode = searchModeFromString(
        m_settings.value(kSettingsSearchMode, QStringLiteral("plain")).toString());
    m_regexStrictFullScan = m_settings.value(kSettingsRegexStrict, false).toBool();

    m_historyColumns = normalizedColumns(m_historyColumns);
    m_quickPasteColumns = normalizedColumns(m_quickPasteColumns);
}

void AppController::saveSettings() {
    m_settings.setValue(kSettingsStartToTray, m_startToTray);
    m_settings.setValue(kSettingsShortcut, m_quickPasteShortcut.toString());
    m_settings.setValue(kSettingsQuickPasteShortcut, m_quickPasteShortcut.toString());
    m_settings.setValue(kSettingsOpenHistoryShortcut, m_openHistoryShortcut.toString());
    m_settings.setValue(kSettingsOpenInspectorShortcut, m_openInspectorShortcut.toString());
    m_settings.setValue(kSettingsPopupGeometry, m_popupGeometry);
    m_settings.setValue(kSettingsHistoryColumns, serializeColumns(m_historyColumns));
    m_settings.setValue(kSettingsQuickColumns, serializeColumns(m_quickPasteColumns));
    m_settings.setValue(kSettingsPreviewLines, m_previewLineCount);
    m_settings.setValue(kSettingsSearchMode, searchModeToString(m_searchMode));
    m_settings.setValue(kSettingsRegexStrict, m_regexStrictFullScan);
    m_settings.sync();
}

void AppController::applyShortcutSettings() {
    struct ShortcutRegistration {
        QString actionLabel;
        QKeySequence sequence;
        GlobalShortcutService *service = nullptr;
        ShortcutRegistrationState *state = nullptr;
    };

    QVector<ShortcutRegistration> registrations = {
        {QStringLiteral("Quick paste popup"), m_quickPasteShortcut,
         &m_quickPasteShortcutService, &m_quickPasteShortcutState},
        {QStringLiteral("Open history window"), m_openHistoryShortcut,
         &m_openHistoryShortcutService, &m_openHistoryShortcutState},
        {QStringLiteral("Open clipboard inspector"), m_openInspectorShortcut,
         &m_openInspectorShortcutService, &m_openInspectorShortcutState},
    };

    for (auto &registration : registrations) {
        if (!registration.service || !registration.state) {
            continue;
        }

        *registration.state = registration.service->registerShortcut(
            registration.sequence);
        if (*registration.state == ShortcutRegistrationState::Failed ||
            *registration.state == ShortcutRegistrationState::Unavailable) {
            if (m_trayIcon && m_trayIcon->isVisible()) {
                m_trayIcon->showMessage(
                    QStringLiteral("Pastetry shortcut"),
                    QStringLiteral("%1: %2")
                        .arg(registration.actionLabel,
                             shortcutStatusTextForState(
                                 registration.sequence, *registration.state,
                                 registration.service->lastError(), true)),
                    QSystemTrayIcon::Warning, 3000);
            }
        }
    }
}

void AppController::checkDaemonConnectivity(bool notifyIfUnavailable) {
    QString error;
    QCborMap params;
    m_client.request(QStringLiteral("Ping"), params, 800, &error);

    const bool reachable = error.isEmpty();
    if (!m_daemonStatusKnown) {
        m_daemonStatusKnown = true;
        m_daemonReachable = reachable;
        if (!reachable && notifyIfUnavailable) {
            notifyDaemonUnavailable(error);
        }
        return;
    }

    if (reachable == m_daemonReachable) {
        return;
    }

    m_daemonReachable = reachable;
    if (reachable) {
        notifyDaemonRecovered();
    } else {
        notifyDaemonUnavailable(error);
    }
}

void AppController::notifyDaemonUnavailable(const QString &reason) {
    const QString detail = reason.isEmpty() ? QStringLiteral("Unknown error") : reason;
    const QString message = QStringLiteral(
        "Cannot connect to daemon (pastetry-clipd).\n"
        "Clipboard capture and activation may not work until the daemon is running.\n\n"
        "Reason: %1")
                                .arg(detail);

    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(QStringLiteral("Pastetry daemon unavailable"), message,
                                QSystemTrayIcon::Warning, 5000);
        return;
    }

    QMessageBox::warning(&m_mainWindow, QStringLiteral("Daemon unavailable"), message);
}

void AppController::notifyDaemonRecovered() {
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(QStringLiteral("Pastetry daemon reachable"),
                                QStringLiteral("Connection to pastetry-clipd restored."),
                                QSystemTrayIcon::Information, 2500);
    }

    QString policyError;
    if (!loadCapturePolicyFromDaemon(&policyError) && !policyError.trimmed().isEmpty()) {
        qWarning().noquote()
            << QStringLiteral("Failed to refresh capture policy after daemon recovery: %1")
                   .arg(policyError);
    }
}

bool AppController::loadCapturePolicyFromDaemon(QString *error) {
    QCborMap params;
    QString requestError;
    const QCborMap result =
        m_client.request(QStringLiteral("GetCapturePolicy"), params, 1800, &requestError);
    if (!requestError.isEmpty()) {
        if (error) {
            *error = requestError;
        }
        return false;
    }

    const QString profileText = result.value(QStringLiteral("profile")).toString();
    const qint64 maxFormatBytes = result.value(QStringLiteral("max_format_bytes")).toInteger();
    const qint64 maxEntryBytes = result.value(QStringLiteral("max_entry_bytes")).toInteger();
    if (profileText.trimmed().isEmpty() || maxFormatBytes <= 0 || maxEntryBytes <= 0) {
        if (error) {
            *error = QStringLiteral("Invalid capture policy payload from daemon");
        }
        return false;
    }

    CapturePolicy loaded;
    loaded.profile = captureProfileFromString(profileText);
    loaded.maxFormatBytes = maxFormatBytes;
    loaded.maxEntryBytes = maxEntryBytes;

    const QCborValue allowlistValue = result.value(QStringLiteral("custom_allowlist"));
    if (allowlistValue.isArray()) {
        for (const QCborValue &item : allowlistValue.toArray()) {
            const QString pattern = item.toString().trimmed();
            if (!pattern.isEmpty()) {
                loaded.customAllowlistPatterns.push_back(pattern);
            }
        }
    }

    m_capturePolicy = loaded;
    return true;
}

bool AppController::applyCapturePolicyToDaemon(const CapturePolicy &policy, QString *error) {
    QCborArray customAllowlist;
    for (const QString &pattern : policy.customAllowlistPatterns) {
        const QString trimmed = pattern.trimmed();
        if (!trimmed.isEmpty()) {
            customAllowlist.append(trimmed);
        }
    }

    QCborMap params;
    params.insert(QStringLiteral("profile"), captureProfileToString(policy.profile));
    params.insert(QStringLiteral("custom_allowlist"), customAllowlist);
    params.insert(QStringLiteral("max_format_bytes"), policy.maxFormatBytes);
    params.insert(QStringLiteral("max_entry_bytes"), policy.maxEntryBytes);

    QString requestError;
    const QCborMap result =
        m_client.request(QStringLiteral("SetCapturePolicy"), params, 2500, &requestError);
    if (!requestError.isEmpty()) {
        if (error) {
            *error = requestError;
        }
        return false;
    }

    Q_UNUSED(result);
    return loadCapturePolicyFromDaemon(error);
}

void AppController::applyViewSettings() {
    m_mainWindow.setVisibleColumns(m_historyColumns);
    m_quickPasteDialog.setVisibleColumns(m_quickPasteColumns);
    m_mainWindow.setPreviewLineCount(m_previewLineCount);
    m_quickPasteDialog.setPreviewLineCount(m_previewLineCount);
    m_mainWindow.setSearchMode(m_searchMode);
    m_quickPasteDialog.setSearchMode(m_searchMode);
    m_mainWindow.setRegexStrict(m_regexStrictFullScan);
    m_quickPasteDialog.setRegexStrict(m_regexStrictFullScan);
}

QVector<bool> AppController::parseColumns(const QString &text,
                                          const QVector<bool> &fallback) const {
    if (text.trimmed().isEmpty()) {
        return fallback;
    }

    const QStringList parts = text.split(',', Qt::SkipEmptyParts);
    if (parts.size() != HistoryModel::ColumnCount) {
        return fallback;
    }

    QVector<bool> columns;
    columns.reserve(HistoryModel::ColumnCount);
    for (const QString &part : parts) {
        columns.push_back(part.trimmed() == QLatin1String("1"));
    }

    return columns;
}

QString AppController::serializeColumns(const QVector<bool> &columns) const {
    QStringList parts;
    for (int i = 0; i < HistoryModel::ColumnCount; ++i) {
        const bool visible = i < columns.size() ? columns.at(i) : true;
        parts.push_back(visible ? QStringLiteral("1") : QStringLiteral("0"));
    }
    return parts.join(',');
}

QVector<bool> AppController::normalizedColumns(const QVector<bool> &columns) const {
    QVector<bool> normalized = columns;
    if (normalized.size() < HistoryModel::ColumnCount) {
        normalized.resize(HistoryModel::ColumnCount);
    }
    for (int i = 0; i < HistoryModel::ColumnCount; ++i) {
        if (i >= columns.size()) {
            normalized[i] = true;
        }
    }

    bool anyVisible = false;
    for (int i = 0; i < HistoryModel::ColumnCount; ++i) {
        anyVisible = anyVisible || normalized.at(i);
    }
    if (!anyVisible) {
        normalized[HistoryModel::PreviewColumn] = true;
    }

    return normalized;
}

}  // namespace pastetry
