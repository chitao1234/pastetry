#include "clip-ui/app_controller.h"

#include "clip-ui/settings_dialog.h"

#include <QAction>
#include <QApplication>
#include <QCborMap>
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
constexpr const char *kSettingsStartToTray = "ui/start_to_tray";
constexpr const char *kSettingsPopupGeometry = "popup/last_geometry";
constexpr const char *kSettingsHistoryColumns = "ui/history_columns";
constexpr const char *kSettingsQuickColumns = "ui/quick_paste_columns";
constexpr const char *kSettingsPreviewLines = "ui/preview_lines";

}  // namespace

AppController::AppController(AppPaths paths, QObject *parent)
    : QObject(parent),
      m_paths(std::move(paths)),
      m_client(m_paths.socketName),
      m_mainWindow(m_client),
      m_quickPasteDialog(m_client),
      m_settings(QStringLiteral("pastetry"), QStringLiteral("pastetry")),
      m_singleInstanceName(QStringLiteral("pastetry.clip-ui.instance.v1")) {
    m_mainWindow.setCloseToTrayEnabled(true);

    connect(&m_shortcutService, &GlobalShortcutService::activated, this,
            &AppController::showQuickPastePopup);

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
        m_shortcutService.unregisterShortcut();
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
    applyShortcutSetting();
    checkDaemonConnectivity(true);
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
    menu->addSeparator();
    m_openSettingsAction = menu->addAction(QStringLiteral("Settings"));
    menu->addSeparator();
    m_quitAction = menu->addAction(QStringLiteral("Quit"));

    connect(m_openHistoryAction, &QAction::triggered, this,
            &AppController::showMainWindow);
    connect(m_openQuickPasteAction, &QAction::triggered, this,
            &AppController::showQuickPastePopup);
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

void AppController::openSettings() {
    SettingsDialog dialog(&m_mainWindow);
    dialog.setValues(m_shortcut, m_startToTray, shortcutStatusText(),
                     m_historyColumns, m_quickPasteColumns, m_previewLineCount);

    auto applyFromDialog = [&]() {
        m_shortcut = dialog.shortcut();
        m_startToTray = dialog.startToTray();
        m_historyColumns = normalizedColumns(dialog.historyColumns());
        m_quickPasteColumns = normalizedColumns(dialog.quickPasteColumns());
        m_previewLineCount = qBound(1, dialog.previewLineCount(), 12);

        m_mainWindow.setCloseToTrayEnabled(m_startToTray && m_trayIcon &&
                                           m_trayIcon->isVisible());

        applyViewSettings();
        applyShortcutSetting();
        saveSettings();

        dialog.setShortcutStatusText(shortcutStatusText());
    };

    connect(&dialog, &SettingsDialog::applyRequested, &dialog, applyFromDialog);

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

    const QString sequence =
        m_settings.value(kSettingsShortcut, QString()).toString();
    m_shortcut = QKeySequence::fromString(sequence);

    m_popupGeometry = m_settings.value(kSettingsPopupGeometry).toByteArray();
    m_historyColumns = parseColumns(m_settings.value(kSettingsHistoryColumns).toString(),
                                    m_historyColumns);
    m_quickPasteColumns = parseColumns(m_settings.value(kSettingsQuickColumns).toString(),
                                       m_quickPasteColumns);
    m_previewLineCount = qBound(1, m_settings.value(kSettingsPreviewLines, 2).toInt(), 12);

    m_historyColumns = normalizedColumns(m_historyColumns);
    m_quickPasteColumns = normalizedColumns(m_quickPasteColumns);
}

void AppController::saveSettings() {
    m_settings.setValue(kSettingsStartToTray, m_startToTray);
    m_settings.setValue(kSettingsShortcut, m_shortcut.toString());
    m_settings.setValue(kSettingsPopupGeometry, m_popupGeometry);
    m_settings.setValue(kSettingsHistoryColumns, serializeColumns(m_historyColumns));
    m_settings.setValue(kSettingsQuickColumns, serializeColumns(m_quickPasteColumns));
    m_settings.setValue(kSettingsPreviewLines, m_previewLineCount);
    m_settings.sync();
}

void AppController::applyShortcutSetting() {
    m_shortcutState = m_shortcutService.registerShortcut(m_shortcut);

    if (m_shortcutState == ShortcutRegistrationState::Failed ||
        m_shortcutState == ShortcutRegistrationState::Unavailable) {
        if (m_trayIcon && m_trayIcon->isVisible()) {
            m_trayIcon->showMessage(QStringLiteral("Pastetry shortcut"),
                                    shortcutStatusText(),
                                    QSystemTrayIcon::Warning, 2500);
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
}

void AppController::applyViewSettings() {
    m_mainWindow.setVisibleColumns(m_historyColumns);
    m_quickPasteDialog.setVisibleColumns(m_quickPasteColumns);
    m_mainWindow.setPreviewLineCount(m_previewLineCount);
    m_quickPasteDialog.setPreviewLineCount(m_previewLineCount);
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

QString AppController::shortcutStatusText() const {
    switch (m_shortcutState) {
        case ShortcutRegistrationState::Registered:
            return QStringLiteral("Active: %1").arg(m_shortcut.toString());
        case ShortcutRegistrationState::Disabled:
            return QStringLiteral("Disabled (no shortcut configured)");
        case ShortcutRegistrationState::Unavailable:
            return QStringLiteral("Unavailable: %1")
                .arg(m_shortcutService.lastError());
        case ShortcutRegistrationState::InvalidBinding:
            return QStringLiteral("Invalid binding: %1")
                .arg(m_shortcutService.lastError());
        case ShortcutRegistrationState::Failed:
            return QStringLiteral("Registration failed: %1")
                .arg(m_shortcutService.lastError());
        default:
            return QStringLiteral("Unknown shortcut state");
    }
}

}  // namespace pastetry
