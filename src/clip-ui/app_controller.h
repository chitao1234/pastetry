#pragma once

#include "clip-ui/global_shortcut_service.h"
#include "clip-ui/main_window.h"
#include "clip-ui/quick_paste_dialog.h"
#include "common/app_paths.h"
#include "common/ipc_client.h"

#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVector>

class QAction;
class QLocalServer;
class QSystemTrayIcon;

namespace pastetry {

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(AppPaths paths, QObject *parent = nullptr);
    bool initialize(QString *error);

private slots:
    void showMainWindow();
    void showQuickPastePopup();
    void openSettings();
    void handleQuitRequested();

private:
    bool notifyExistingInstance();
    bool startSingleInstanceServer(QString *error);
    void handleSingleInstanceCommand(const QString &command);

    void setupTray();
    void loadSettings();
    void saveSettings();
    void applyShortcutSetting();
    void applyViewSettings();
    void checkDaemonConnectivity(bool notifyIfUnavailable);
    void notifyDaemonUnavailable(const QString &reason);
    void notifyDaemonRecovered();
    QVector<bool> parseColumns(const QString &text,
                               const QVector<bool> &fallback) const;
    QString serializeColumns(const QVector<bool> &columns) const;
    QVector<bool> normalizedColumns(const QVector<bool> &columns) const;
    QString shortcutStatusText() const;

    AppPaths m_paths;
    IpcClient m_client;
    MainWindow m_mainWindow;
    QuickPasteDialog m_quickPasteDialog;
    GlobalShortcutService m_shortcutService;

    QSettings m_settings;
    QKeySequence m_shortcut;
    bool m_startToTray = true;
    QByteArray m_popupGeometry;
    QVector<bool> m_historyColumns = {true, true, true, true};
    QVector<bool> m_quickPasteColumns = {true, true, true, true};
    int m_previewLineCount = 2;
    SearchMode m_searchMode = SearchMode::Plain;
    bool m_regexStrictFullScan = false;
    ShortcutRegistrationState m_shortcutState = ShortcutRegistrationState::Disabled;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QAction *m_openHistoryAction = nullptr;
    QAction *m_openQuickPasteAction = nullptr;
    QAction *m_openSettingsAction = nullptr;
    QAction *m_quitAction = nullptr;

    QLocalServer *m_singleInstanceServer = nullptr;
    QString m_singleInstanceName;
    bool m_isQuitting = false;
    bool m_daemonStatusKnown = false;
    bool m_daemonReachable = false;
    QTimer m_daemonHealthTimer;
};

}  // namespace pastetry
