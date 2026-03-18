#include "clip-ui/app_controller.h"

#include "clip-ui/settings_dialog.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QSystemTrayIcon>

namespace pastetry {
namespace {

constexpr const char *kSettingsShortcut = "hotkey/sequence";
constexpr const char *kSettingsStartToTray = "ui/start_to_tray";
constexpr const char *kSettingsPopupGeometry = "popup/last_geometry";

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

    setupTray();
    applyShortcutSetting();

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
    dialog.setValues(m_shortcut, m_startToTray, shortcutStatusText());

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    m_shortcut = dialog.shortcut();
    m_startToTray = dialog.startToTray();

    m_mainWindow.setCloseToTrayEnabled(m_startToTray && m_trayIcon &&
                                       m_trayIcon->isVisible());

    applyShortcutSetting();
    saveSettings();
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
}

void AppController::saveSettings() {
    m_settings.setValue(kSettingsStartToTray, m_startToTray);
    m_settings.setValue(kSettingsShortcut, m_shortcut.toString());
    m_settings.setValue(kSettingsPopupGeometry, m_popupGeometry);
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
