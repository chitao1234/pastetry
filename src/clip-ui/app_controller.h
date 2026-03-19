#pragma once

#include "clip-ui/global_shortcut_service.h"
#include "clip-ui/ipc_async_runner.h"
#include "clip-ui/main_window.h"
#include "clip-ui/quick_paste_dialog.h"
#include "clip-ui/shortcut_config.h"
#include "common/app_paths.h"

#include <QObject>
#include <QSettings>
#include <QPoint>
#include <QSize>
#include <QTimer>
#include <QVector>

class QAction;
class QLocalServer;
class QSystemTrayIcon;

namespace pastetry {

class ClipboardInspectorDialog;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(AppPaths paths, QObject *parent = nullptr);
    bool initialize(QString *error);

private slots:
    void showMainWindow();
    void showQuickPastePopup();
    void openClipboardInspector();
    void openSettings();
    void handleQuitRequested();

private:
    enum class InstanceTakeoverDecision {
        Exit,
        RetryHandoff,
        TakeOver,
    };

    bool notifyExistingInstance(int timeoutMs, QString *error = nullptr);
    bool hasLikelyPeerUiProcess(QString *detail = nullptr) const;
    bool startSingleInstanceServer(QString *error);
    InstanceTakeoverDecision promptSingleInstanceTakeover(const QString &detail);
    void handleSingleInstanceNewConnection();
    void handleSingleInstanceCommand(const QString &command);

    void setupTray();
    void loadSettings();
    void saveSettings();
    void applyShortcutSettings(bool notifyFailures = true);
    void clearShortcutRegistrations();
    void clearChordCaptureRegistrations();
    void beginChordCapture(const QString &firstKeyPortable);
    void endChordCapture();
    void handleShortcutAction(const QString &actionId);
    void executeSlotShortcut(const ShortcutActionSpec &spec);
    bool sendSyntheticPaste(QString *error) const;
    void notifyShortcutWarning(const QString &title, const QString &message);

    void applyViewSettings();
    void checkDaemonConnectivity(bool notifyIfUnavailable);
    void notifyDaemonUnavailable(const QString &reason);
    void notifyDaemonRecovered();
    QVector<bool> parseColumns(const QString &text,
                               const QVector<bool> &fallback) const;
    QString serializeColumns(const QVector<bool> &columns) const;
    QVector<bool> normalizedColumns(const QVector<bool> &columns) const;

    AppPaths m_paths;
    IpcAsyncRunner m_ipcRunner;
    MainWindow m_mainWindow;
    QuickPasteDialog m_quickPasteDialog;

    QSettings m_settings;
    QHash<QString, ShortcutBindingConfig> m_shortcutBindings;
    QHash<QString, ShortcutRegistrationState> m_shortcutStates;
    QHash<QString, QString> m_shortcutErrors;
    QKeySequence m_autoPasteKey = QKeySequence(Qt::CTRL | Qt::Key_V);

    QVector<GlobalShortcutService *> m_baseShortcutServices;
    QVector<GlobalShortcutService *> m_chordSecondShortcutServices;
    QHash<GlobalShortcutService *, QString> m_serviceToDirectAction;
    QHash<GlobalShortcutService *, QString> m_serviceToChordFirstKey;
    QHash<GlobalShortcutService *, QString> m_serviceToChordSecondAction;
    QHash<QString, QVector<QString>> m_chordFirstToActions;
    QVector<QString> m_pendingChordActions;
    bool m_chordCaptureActive = false;
    int m_nextHotkeyId = 1;

    bool m_startToTray = true;
    QByteArray m_popupGeometry;
    QSize m_quickPasteSize;
    QString m_popupPositionMode = QStringLiteral("cursor");
    QPoint m_quickPastePosition;
    bool m_hasQuickPastePosition = false;
    QVector<bool> m_historyColumns = {true, true, true, true};
    QVector<bool> m_quickPasteColumns = {true, true, true, true};
    int m_previewLineCount = 2;
    SearchMode m_searchMode = SearchMode::Plain;
    bool m_regexStrictFullScan = false;
    CapturePolicy m_capturePolicy;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QAction *m_openHistoryAction = nullptr;
    QAction *m_openQuickPasteAction = nullptr;
    QAction *m_openClipboardInspectorAction = nullptr;
    QAction *m_openSettingsAction = nullptr;
    QAction *m_quitAction = nullptr;
    ClipboardInspectorDialog *m_clipboardInspectorDialog = nullptr;

    QLocalServer *m_singleInstanceServer = nullptr;
    QString m_singleInstanceName;
    bool m_isQuitting = false;
    bool m_daemonStatusKnown = false;
    bool m_daemonReachable = false;
    bool m_pingInFlight = false;
    bool m_pingNotifyIfUnavailable = false;
    QTimer m_daemonHealthTimer;
    QTimer m_chordTimeoutTimer;
};

}  // namespace pastetry
