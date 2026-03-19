#include "clip-ui/app_controller.h"

#include "clip-ui/clipboard_inspector_dialog.h"
#include "clip-ui/settings_dialog.h"

#include <QAction>
#include <QAbstractSocket>
#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStyle>
#include <QSystemTrayIcon>

#ifdef Q_OS_WIN
#include <tlhelp32.h>
#include <windows.h>
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
#include <QtCore/qnativeinterface.h>
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#if defined(PASTETRY_HAVE_XTEST)
#include <X11/extensions/XTest.h>
#endif
#endif

namespace pastetry {
namespace {

constexpr const char *kSettingsShortcut = "hotkey/sequence";
constexpr const char *kSettingsQuickPasteShortcut = "hotkey/quick_paste";
constexpr const char *kSettingsOpenHistoryShortcut = "hotkey/open_history";
constexpr const char *kSettingsOpenInspectorShortcut = "hotkey/open_inspector";
constexpr const char *kSettingsActionsPrefix = "hotkey/actions_v2";
constexpr const char *kSettingsAutoPasteKey = "hotkey/auto_paste_key";
constexpr const char *kSettingsStartToTray = "ui/start_to_tray";
constexpr const char *kSettingsPopupGeometry = "popup/last_geometry";
constexpr const char *kSettingsQuickPasteSize = "popup/size";
constexpr const char *kSettingsHistoryColumns = "ui/history_columns";
constexpr const char *kSettingsQuickColumns = "ui/quick_paste_columns";
constexpr const char *kSettingsPreviewLines = "ui/preview_lines";
constexpr const char *kSettingsSearchMode = "search/mode";
constexpr const char *kSettingsRegexStrict = "search/regex_strict_full_scan";
constexpr int kChordTimeoutMs = 1200;

QStringList normalizedAllowlistPatterns(const QStringList &patterns) {
    QStringList normalized;
    normalized.reserve(patterns.size());
    for (const QString &raw : patterns) {
        const QString trimmed = raw.trimmed();
        if (!trimmed.isEmpty()) {
            normalized.push_back(trimmed);
        }
    }
    return normalized;
}

bool capturePolicyEquals(const CapturePolicy &lhs, const CapturePolicy &rhs) {
    return lhs.profile == rhs.profile &&
           lhs.maxFormatBytes == rhs.maxFormatBytes &&
           lhs.maxEntryBytes == rhs.maxEntryBytes &&
           normalizedAllowlistPatterns(lhs.customAllowlistPatterns) ==
               normalizedAllowlistPatterns(rhs.customAllowlistPatterns);
}

bool parseCapturePolicyFromCborResult(const QCborMap &result, CapturePolicy *policy,
                                      QString *error) {
    if (!policy) {
        if (error) {
            *error = QStringLiteral("policy output is required");
        }
        return false;
    }

    const QString profileText = result.value(QStringLiteral("profile")).toString();
    const qint64 maxFormatBytes = result.value(QStringLiteral("max_format_bytes")).toInteger();
    const qint64 maxEntryBytes = result.value(QStringLiteral("max_entry_bytes")).toInteger();
    if (profileText.trimmed().isEmpty() || maxFormatBytes <= 0 || maxEntryBytes <= 0) {
        if (error) {
            *error = QStringLiteral("Invalid capture policy payload");
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
    } else if (allowlistValue.isString()) {
        for (const QString &line :
             allowlistValue.toString().split('\n', Qt::SkipEmptyParts)) {
            const QString pattern = line.trimmed();
            if (!pattern.isEmpty()) {
                loaded.customAllowlistPatterns.push_back(pattern);
            }
        }
    }

    loaded.customAllowlistPatterns = normalizedAllowlistPatterns(loaded.customAllowlistPatterns);
    *policy = loaded;
    if (error) {
        error->clear();
    }
    return true;
}

QString shortcutKey(const QKeySequence &shortcut) {
    return shortcut.toString(QKeySequence::PortableText).trimmed();
}

QString actionLabel(const QString &actionId) {
    if (const ShortcutActionSpec *spec = findShortcutActionSpec(actionId)) {
        return spec->label;
    }
    return actionId;
}

QString stateDetailText(ShortcutRegistrationState state, const QString &detail) {
    const QString cleaned = detail.trimmed().isEmpty() ? QStringLiteral("Unknown error") : detail;
    switch (state) {
        case ShortcutRegistrationState::Registered:
            return QStringLiteral("Active");
        case ShortcutRegistrationState::Disabled:
            return QStringLiteral("Disabled");
        case ShortcutRegistrationState::Unavailable:
            return QStringLiteral("Unavailable: %1").arg(cleaned);
        case ShortcutRegistrationState::InvalidBinding:
            return QStringLiteral("Invalid binding: %1").arg(cleaned);
        case ShortcutRegistrationState::Failed:
            return QStringLiteral("Registration failed: %1").arg(cleaned);
        default:
            return QStringLiteral("Unknown state");
    }
}

struct ShortcutValidationResult {
    QHash<QString, QString> actionErrors;
    QString conflictMessage;
};

void setActionValidationError(ShortcutValidationResult *result, const QString &actionId,
                              const QString &message) {
    if (!result) {
        return;
    }
    if (!result->actionErrors.contains(actionId)) {
        result->actionErrors.insert(actionId, message);
    }
}

ShortcutValidationResult validateShortcutBindings(
    const QHash<QString, ShortcutBindingConfig> &bindings) {
    ShortcutValidationResult result;

    QHash<QString, QString> directByKey;
    QHash<QString, QVector<QString>> chordByFirst;
    QHash<QString, QString> chordPairByKey;

    for (const auto &spec : allShortcutActionSpecs()) {
        const ShortcutBindingConfig binding = bindings.value(spec.id);
        if (binding.mode == ShortcutBindingMode::Disabled) {
            continue;
        }

        if (binding.mode == ShortcutBindingMode::Direct) {
            const QString directKey = shortcutKey(binding.directSequence);
            if (directKey.isEmpty()) {
                setActionValidationError(&result, spec.id,
                                         QStringLiteral("Direct mode requires a shortcut"));
                continue;
            }

            if (directByKey.contains(directKey)) {
                const QString peerId = directByKey.value(directKey);
                setActionValidationError(
                    &result, spec.id,
                    QStringLiteral("Conflict with %1").arg(actionLabel(peerId)));
                setActionValidationError(
                    &result, peerId,
                    QStringLiteral("Conflict with %1").arg(actionLabel(spec.id)));
                continue;
            }
            directByKey.insert(directKey, spec.id);
            continue;
        }

        const QString firstKey = shortcutKey(binding.chordFirstSequence);
        const QString secondKey = shortcutKey(binding.chordSecondSequence);
        if (firstKey.isEmpty() || secondKey.isEmpty()) {
            setActionValidationError(
                &result, spec.id,
                QStringLiteral("Chord mode requires both step 1 and step 2 shortcuts"));
            continue;
        }

        const QString pairKey = firstKey + QStringLiteral("||") + secondKey;
        if (chordPairByKey.contains(pairKey)) {
            const QString peerId = chordPairByKey.value(pairKey);
            setActionValidationError(
                &result, spec.id,
                QStringLiteral("Chord pair duplicates %1").arg(actionLabel(peerId)));
            setActionValidationError(
                &result, peerId,
                QStringLiteral("Chord pair duplicates %1").arg(actionLabel(spec.id)));
            continue;
        }

        chordPairByKey.insert(pairKey, spec.id);
        chordByFirst[firstKey].push_back(spec.id);
    }

    for (auto it = directByKey.cbegin(); it != directByKey.cend(); ++it) {
        const QString firstKey = it.key();
        if (!chordByFirst.contains(firstKey)) {
            continue;
        }

        const QString directAction = it.value();
        const QVector<QString> chordActions = chordByFirst.value(firstKey);
        for (const QString &chordAction : chordActions) {
            setActionValidationError(
                &result, directAction,
                QStringLiteral("Direct shortcut conflicts with chord step 1 of %1")
                    .arg(actionLabel(chordAction)));
            setActionValidationError(
                &result, chordAction,
                QStringLiteral("Chord step 1 conflicts with direct shortcut of %1")
                    .arg(actionLabel(directAction)));
        }
    }

    if (!result.actionErrors.isEmpty()) {
        result.conflictMessage =
            QStringLiteral("Shortcut conflicts detected. Resolve duplicate direct bindings, "
                           "direct/chord-step-1 collisions, and duplicate chord pairs.");
    }

    return result;
}

QString bindingSummary(const ShortcutBindingConfig &binding) {
    if (binding.mode == ShortcutBindingMode::Disabled) {
        return QStringLiteral("Disabled (no shortcut configured)");
    }
    if (binding.mode == ShortcutBindingMode::Direct) {
        return QStringLiteral("Direct: %1").arg(binding.directSequence.toString());
    }
    return QStringLiteral("Chord: %1 then %2")
        .arg(binding.chordFirstSequence.toString(),
             binding.chordSecondSequence.toString());
}

#ifdef Q_OS_WIN
bool qtKeyToWindowsVk(int qtKey, UINT *outVk) {
    if (!outVk) {
        return false;
    }

    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        *outVk = static_cast<UINT>('A' + (qtKey - Qt::Key_A));
        return true;
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        *outVk = static_cast<UINT>('0' + (qtKey - Qt::Key_0));
        return true;
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        *outVk = static_cast<UINT>(VK_F1 + (qtKey - Qt::Key_F1));
        return true;
    }

    switch (qtKey) {
        case Qt::Key_BracketLeft:
            *outVk = VK_OEM_4;
            return true;
        case Qt::Key_BracketRight:
            *outVk = VK_OEM_6;
            return true;
        case Qt::Key_Backslash:
            *outVk = VK_OEM_5;
            return true;
        case Qt::Key_Slash:
            *outVk = VK_OEM_2;
            return true;
        case Qt::Key_Minus:
            *outVk = VK_OEM_MINUS;
            return true;
        case Qt::Key_Equal:
        case Qt::Key_Plus:
            *outVk = VK_OEM_PLUS;
            return true;
        case Qt::Key_Comma:
            *outVk = VK_OEM_COMMA;
            return true;
        case Qt::Key_Period:
            *outVk = VK_OEM_PERIOD;
            return true;
        case Qt::Key_Semicolon:
        case Qt::Key_Colon:
            *outVk = VK_OEM_1;
            return true;
        case Qt::Key_Apostrophe:
            *outVk = VK_OEM_7;
            return true;
        case Qt::Key_QuoteLeft:
            *outVk = VK_OEM_3;
            return true;
        case Qt::Key_Insert:
            *outVk = VK_INSERT;
            return true;
        case Qt::Key_Delete:
            *outVk = VK_DELETE;
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            *outVk = VK_RETURN;
            return true;
        case Qt::Key_Tab:
            *outVk = VK_TAB;
            return true;
        case Qt::Key_Space:
            *outVk = VK_SPACE;
            return true;
        case Qt::Key_Left:
            *outVk = VK_LEFT;
            return true;
        case Qt::Key_Right:
            *outVk = VK_RIGHT;
            return true;
        case Qt::Key_Up:
            *outVk = VK_UP;
            return true;
        case Qt::Key_Down:
            *outVk = VK_DOWN;
            return true;
        case Qt::Key_Home:
            *outVk = VK_HOME;
            return true;
        case Qt::Key_End:
            *outVk = VK_END;
            return true;
        case Qt::Key_PageUp:
            *outVk = VK_PRIOR;
            return true;
        case Qt::Key_PageDown:
            *outVk = VK_NEXT;
            return true;
        default:
            return false;
    }
}
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
bool qtKeyToX11KeySym(int qtKey, KeySym *outSym) {
    if (!outSym) {
        return false;
    }

    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        *outSym = static_cast<KeySym>(XK_A + (qtKey - Qt::Key_A));
        return true;
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        *outSym = static_cast<KeySym>(XK_0 + (qtKey - Qt::Key_0));
        return true;
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        *outSym = static_cast<KeySym>(XK_F1 + (qtKey - Qt::Key_F1));
        return true;
    }

    switch (qtKey) {
        case Qt::Key_BracketLeft:
            *outSym = XK_bracketleft;
            return true;
        case Qt::Key_BracketRight:
            *outSym = XK_bracketright;
            return true;
        case Qt::Key_Backslash:
            *outSym = XK_backslash;
            return true;
        case Qt::Key_Slash:
            *outSym = XK_slash;
            return true;
        case Qt::Key_Minus:
            *outSym = XK_minus;
            return true;
        case Qt::Key_Equal:
            *outSym = XK_equal;
            return true;
        case Qt::Key_Comma:
            *outSym = XK_comma;
            return true;
        case Qt::Key_Period:
            *outSym = XK_period;
            return true;
        case Qt::Key_Semicolon:
            *outSym = XK_semicolon;
            return true;
        case Qt::Key_Apostrophe:
            *outSym = XK_apostrophe;
            return true;
        case Qt::Key_QuoteLeft:
            *outSym = XK_grave;
            return true;
        case Qt::Key_Plus:
            *outSym = XK_plus;
            return true;
        case Qt::Key_Colon:
            *outSym = XK_colon;
            return true;
        case Qt::Key_Underscore:
            *outSym = XK_underscore;
            return true;
        case Qt::Key_Question:
            *outSym = XK_question;
            return true;
        case Qt::Key_Insert:
            *outSym = XK_Insert;
            return true;
        case Qt::Key_Delete:
            *outSym = XK_Delete;
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            *outSym = XK_Return;
            return true;
        case Qt::Key_Tab:
            *outSym = XK_Tab;
            return true;
        case Qt::Key_Space:
            *outSym = XK_space;
            return true;
        case Qt::Key_Left:
            *outSym = XK_Left;
            return true;
        case Qt::Key_Right:
            *outSym = XK_Right;
            return true;
        case Qt::Key_Up:
            *outSym = XK_Up;
            return true;
        case Qt::Key_Down:
            *outSym = XK_Down;
            return true;
        case Qt::Key_Home:
            *outSym = XK_Home;
            return true;
        case Qt::Key_End:
            *outSym = XK_End;
            return true;
        case Qt::Key_PageUp:
            *outSym = XK_Page_Up;
            return true;
        case Qt::Key_PageDown:
            *outSym = XK_Page_Down;
            return true;
        default:
            return false;
    }
}
#endif

}  // namespace

AppController::AppController(AppPaths paths, QObject *parent)
    : QObject(parent),
      m_paths(std::move(paths)),
      m_ipcRunner(m_paths.socketName, this),
      m_mainWindow(&m_ipcRunner),
      m_quickPasteDialog(&m_ipcRunner),
      m_settings(QStringLiteral("pastetry"), QStringLiteral("pastetry")),
      m_singleInstanceName(QStringLiteral("pastetry.clip-ui.instance.v1")) {
    m_mainWindow.setCloseToTrayEnabled(true);

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
        const QSize popupSize = m_quickPasteDialog.size();
        if (popupSize.isValid() && popupSize != m_quickPasteSize) {
            m_quickPasteSize = popupSize;
            saveSettings();
        }
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
        const QSize popupSize = m_quickPasteDialog.size();
        if (popupSize.isValid()) {
            m_quickPasteSize = popupSize;
        }
        m_mainWindow.setCloseToTrayEnabled(false);
        clearChordCaptureRegistrations();
        clearShortcutRegistrations();
        saveSettings();
    });

    m_daemonHealthTimer.setInterval(4000);
    connect(&m_daemonHealthTimer, &QTimer::timeout, this,
            [this] { checkDaemonConnectivity(false); });

    m_chordTimeoutTimer.setSingleShot(true);
    m_chordTimeoutTimer.setInterval(kChordTimeoutMs);
    connect(&m_chordTimeoutTimer, &QTimer::timeout, this, &AppController::endChordCapture);
}

bool AppController::initialize(QString *error) {
    QString handoffError;
    if (notifyExistingInstance(300, &handoffError)) {
        return false;
    }

    if (!startSingleInstanceServer(error)) {
        if (!m_singleInstanceServer ||
            m_singleInstanceServer->serverError() != QAbstractSocket::AddressInUseError) {
            return false;
        }

        handoffError.clear();
        if (notifyExistingInstance(300, &handoffError)) {
            return false;
        }

        QString processProbeDetail;
        const bool peerRunning = hasLikelyPeerUiProcess(&processProbeDetail);
        if (!peerRunning) {
            qWarning().noquote()
                << QStringLiteral("Single-instance socket looked in-use but no peer UI process "
                                  "was found; taking over stale socket. Handoff detail: %1. "
                                  "Process probe: %2")
                       .arg(handoffError.trimmed().isEmpty() ? QStringLiteral("Unknown")
                                                             : handoffError.trimmed(),
                            processProbeDetail.trimmed().isEmpty()
                                ? QStringLiteral("No peer process detected")
                                : processProbeDetail.trimmed());
            QLocalServer::removeServer(m_singleInstanceName);
            if (!startSingleInstanceServer(error)) {
                return false;
            }
        } else {
            if (!processProbeDetail.trimmed().isEmpty()) {
                handoffError =
                    handoffError.trimmed().isEmpty()
                        ? processProbeDetail
                        : QStringLiteral("%1\n%2").arg(handoffError.trimmed(), processProbeDetail);
            }

            while (true) {
                const InstanceTakeoverDecision decision =
                    promptSingleInstanceTakeover(handoffError);
                if (decision == InstanceTakeoverDecision::Exit) {
                    if (error) {
                        error->clear();
                    }
                    return false;
                }

                if (decision == InstanceTakeoverDecision::RetryHandoff) {
                    handoffError.clear();
                    if (notifyExistingInstance(300, &handoffError)) {
                        return false;
                    }
                    continue;
                }

                QLocalServer::removeServer(m_singleInstanceName);
                if (!startSingleInstanceServer(error)) {
                    return false;
                }
                break;
            }
        }
    }

    loadSettings();
    if (m_quickPasteSize.isValid()) {
        m_quickPasteDialog.resize(m_quickPasteSize);
    } else if (!m_popupGeometry.isEmpty()) {
        m_quickPasteDialog.restoreGeometry(m_popupGeometry);
    }
    m_quickPasteSize = m_quickPasteDialog.size();
    applyViewSettings();

    setupTray();
    m_mainWindow.setCloseToTrayEnabled(m_startToTray && m_trayIcon &&
                                       m_trayIcon->isVisible());
    applyShortcutSettings(true);
    checkDaemonConnectivity(true);
    m_ipcRunner.request(
        QStringLiteral("GetCapturePolicy"), QCborMap{}, 1800, this,
        [this](const QCborMap &result, const QString &requestError) {
            if (!requestError.trimmed().isEmpty()) {
                qWarning().noquote()
                    << QStringLiteral("Failed to load capture policy from daemon: %1")
                           .arg(requestError);
                return;
            }

            CapturePolicy loaded;
            QString parseError;
            if (!parseCapturePolicyFromCborResult(result, &loaded, &parseError)) {
                qWarning().noquote()
                    << QStringLiteral("Failed to load capture policy from daemon: %1")
                           .arg(parseError);
                return;
            }
            m_capturePolicy = loaded;
        });
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
            new ClipboardInspectorDialog(&m_ipcRunner, &m_mainWindow);
    }
    m_clipboardInspectorDialog->inspectClipboard();
}

void AppController::openSettings() {
    SettingsDialog dialog(&m_mainWindow);

    auto currentStatusTexts = [&]() {
        QHash<QString, QString> statusTexts;
        for (const auto &spec : allShortcutActionSpecs()) {
            const ShortcutBindingConfig binding = m_shortcutBindings.value(spec.id);
            if (binding.mode == ShortcutBindingMode::Disabled) {
                statusTexts.insert(spec.id,
                                   QStringLiteral("Disabled (no shortcut configured)"));
                continue;
            }

            const ShortcutRegistrationState state =
                m_shortcutStates.value(spec.id, ShortcutRegistrationState::Disabled);
            const QString detail = m_shortcutErrors.value(spec.id);
            if (state == ShortcutRegistrationState::Registered) {
                statusTexts.insert(spec.id,
                                   QStringLiteral("Active: %1").arg(bindingSummary(binding)));
            } else {
                statusTexts.insert(spec.id, stateDetailText(state, detail));
            }
        }
        return statusTexts;
    };

    dialog.setValues(m_shortcutBindings, currentStatusTexts(), m_autoPasteKey,
                     m_startToTray, false, QString(), m_historyColumns,
                     m_quickPasteColumns, m_previewLineCount, m_regexStrictFullScan,
                     m_capturePolicy);

    m_ipcRunner.request(
        QStringLiteral("GetCapturePolicy"), QCborMap{}, 1800, &dialog,
        [&dialog, this, currentStatusTexts](const QCborMap &result, const QString &error) {
            if (!error.trimmed().isEmpty()) {
                QMessageBox::warning(
                    &dialog, QStringLiteral("Capture policy unavailable"),
                    QStringLiteral("Could not load rich-format capture policy from daemon.\n\n"
                                   "Reason: %1")
                        .arg(error));
                return;
            }

            CapturePolicy loaded;
            QString parseError;
            if (!parseCapturePolicyFromCborResult(result, &loaded, &parseError)) {
                QMessageBox::warning(&dialog, QStringLiteral("Capture policy unavailable"),
                                     parseError);
                return;
            }

            m_capturePolicy = loaded;
            if (!dialog.hasPendingChanges()) {
                dialog.setValues(m_shortcutBindings, currentStatusTexts(), m_autoPasteKey,
                                 m_startToTray, false, QString(), m_historyColumns,
                                 m_quickPasteColumns, m_previewLineCount,
                                 m_regexStrictFullScan, m_capturePolicy);
            }
        });

    auto updateShortcutAvailability = [&]() {
        const QHash<QString, ShortcutBindingConfig> candidateBindings =
            dialog.shortcutBindings();
        const ShortcutValidationResult validation =
            validateShortcutBindings(candidateBindings);
        const QHash<QString, QString> activeStatuses = currentStatusTexts();

        QHash<QString, QString> statuses;
        for (const auto &spec : allShortcutActionSpecs()) {
            const ShortcutBindingConfig binding = candidateBindings.value(spec.id);
            if (validation.actionErrors.contains(spec.id)) {
                statuses.insert(spec.id, validation.actionErrors.value(spec.id));
                continue;
            }

            if (binding.mode == ShortcutBindingMode::Disabled) {
                statuses.insert(spec.id, QStringLiteral("Disabled (no shortcut configured)"));
                continue;
            }

            const ShortcutBindingConfig activeBinding = m_shortcutBindings.value(spec.id);
            if (binding == activeBinding) {
                statuses.insert(spec.id, activeStatuses.value(spec.id));
                continue;
            }

            statuses.insert(spec.id, QStringLiteral("Pending apply: %1").arg(bindingSummary(binding)));
        }

        dialog.setShortcutStatusTexts(statuses);
        dialog.setShortcutConflictState(!validation.actionErrors.isEmpty(),
                                        validation.conflictMessage);
    };

    auto applyFromDialog = [&](bool closeOnSuccess) {
        if (dialog.isModal() && !dialog.isVisible()) {
            return;
        }

        const ShortcutValidationResult validation =
            validateShortcutBindings(dialog.shortcutBindings());
        if (!validation.actionErrors.isEmpty()) {
            QMessageBox::warning(
                &dialog, QStringLiteral("Shortcut conflicts"),
                validation.conflictMessage.isEmpty()
                    ? QStringLiteral("Resolve shortcut conflicts before applying.")
                    : validation.conflictMessage);
            return;
        }

        const QHash<QString, ShortcutBindingConfig> requestedShortcuts = dialog.shortcutBindings();
        const QKeySequence requestedAutoPasteKey = dialog.autoPasteKey();
        const bool requestedStartToTray = dialog.startToTray();
        const QVector<bool> requestedHistoryColumns =
            normalizedColumns(dialog.historyColumns());
        const QVector<bool> requestedQuickPasteColumns =
            normalizedColumns(dialog.quickPasteColumns());
        const int requestedPreviewLineCount = qBound(1, dialog.previewLineCount(), 12);
        const bool requestedRegexStrict = dialog.regexStrictFullScanEnabled();
        CapturePolicy requestedPolicy = dialog.capturePolicy();
        requestedPolicy.customAllowlistPatterns =
            normalizedAllowlistPatterns(requestedPolicy.customAllowlistPatterns);

        const bool shortcutsChanged = requestedShortcuts != m_shortcutBindings;
        const bool autoPasteChanged = requestedAutoPasteKey != m_autoPasteKey;
        const bool startToTrayChanged = requestedStartToTray != m_startToTray;
        const bool historyColumnsChanged = requestedHistoryColumns != m_historyColumns;
        const bool quickColumnsChanged = requestedQuickPasteColumns != m_quickPasteColumns;
        const bool previewLineCountChanged = requestedPreviewLineCount != m_previewLineCount;
        const bool regexStrictChanged = requestedRegexStrict != m_regexStrictFullScan;
        const bool capturePolicyChanged = !capturePolicyEquals(requestedPolicy, m_capturePolicy);

        const bool anyChanged = shortcutsChanged || autoPasteChanged || startToTrayChanged ||
                                historyColumnsChanged || quickColumnsChanged ||
                                previewLineCountChanged || regexStrictChanged ||
                                capturePolicyChanged;
        if (!anyChanged) {
            if (closeOnSuccess) {
                dialog.accept();
            }
            return;
        }

        auto commitLocalChanges = [&, requestedShortcuts, requestedAutoPasteKey,
                                   requestedStartToTray, requestedHistoryColumns,
                                   requestedQuickPasteColumns, requestedPreviewLineCount,
                                   requestedRegexStrict, closeOnSuccess,
                                   shortcutsChanged, autoPasteChanged, startToTrayChanged,
                                   historyColumnsChanged, quickColumnsChanged,
                                   previewLineCountChanged, regexStrictChanged](
                                      const CapturePolicy &effectivePolicy) {
            if (shortcutsChanged) {
                m_shortcutBindings = requestedShortcuts;
            }
            if (autoPasteChanged) {
                m_autoPasteKey = requestedAutoPasteKey;
            }
            if (startToTrayChanged) {
                m_startToTray = requestedStartToTray;
            }
            if (historyColumnsChanged) {
                m_historyColumns = requestedHistoryColumns;
            }
            if (quickColumnsChanged) {
                m_quickPasteColumns = requestedQuickPasteColumns;
            }
            if (previewLineCountChanged) {
                m_previewLineCount = requestedPreviewLineCount;
            }
            if (regexStrictChanged) {
                m_regexStrictFullScan = requestedRegexStrict;
            }
            m_capturePolicy = effectivePolicy;

            if (startToTrayChanged) {
                m_mainWindow.setCloseToTrayEnabled(m_startToTray && m_trayIcon &&
                                                   m_trayIcon->isVisible());
            }

            if (historyColumnsChanged || quickColumnsChanged || previewLineCountChanged ||
                regexStrictChanged) {
                applyViewSettings();
            }
            if (shortcutsChanged || autoPasteChanged) {
                applyShortcutSettings(true);
            }
            saveSettings();

            dialog.setValues(m_shortcutBindings, currentStatusTexts(), m_autoPasteKey,
                             m_startToTray, false, QString(), m_historyColumns,
                             m_quickPasteColumns, m_previewLineCount,
                             m_regexStrictFullScan, m_capturePolicy);
            updateShortcutAvailability();
            if (closeOnSuccess) {
                dialog.accept();
            }
        };

        if (!capturePolicyChanged) {
            commitLocalChanges(m_capturePolicy);
            return;
        }

        QCborArray customAllowlist;
        for (const QString &pattern : requestedPolicy.customAllowlistPatterns) {
            customAllowlist.append(pattern);
        }

        QCborMap params;
        params.insert(QStringLiteral("profile"), captureProfileToString(requestedPolicy.profile));
        params.insert(QStringLiteral("custom_allowlist"), customAllowlist);
        params.insert(QStringLiteral("max_format_bytes"), requestedPolicy.maxFormatBytes);
        params.insert(QStringLiteral("max_entry_bytes"), requestedPolicy.maxEntryBytes);

        dialog.setApplyInProgress(true);
        m_ipcRunner.request(
            QStringLiteral("SetCapturePolicy"), params, 2500, &dialog,
            [this, &dialog, commitLocalChanges](const QCborMap &, const QString &setError) {
                if (!setError.isEmpty()) {
                    dialog.setApplyInProgress(false);
                    QMessageBox::warning(
                        &dialog, QStringLiteral("Capture policy apply failed"),
                        QStringLiteral("Failed to update daemon capture policy.\n\nReason: %1")
                            .arg(setError));
                    return;
                }

                m_ipcRunner.request(
                    QStringLiteral("GetCapturePolicy"), QCborMap{}, 1800, &dialog,
                    [this, &dialog, commitLocalChanges](const QCborMap &result,
                                                        const QString &getError) {
                        dialog.setApplyInProgress(false);
                        if (!getError.isEmpty()) {
                            QMessageBox::warning(
                                &dialog, QStringLiteral("Capture policy apply failed"),
                                QStringLiteral("Policy was set but reload failed.\n\nReason: %1")
                                    .arg(getError));
                            return;
                        }

                        CapturePolicy loaded;
                        QString parseError;
                        if (!parseCapturePolicyFromCborResult(result, &loaded, &parseError)) {
                            QMessageBox::warning(
                                &dialog, QStringLiteral("Capture policy apply failed"),
                                QStringLiteral("Policy reload returned invalid payload.\n\n"
                                               "Reason: %1")
                                    .arg(parseError));
                            return;
                        }

                        commitLocalChanges(loaded);
                    });
            });
    };

    connect(&dialog, &SettingsDialog::applyRequested, &dialog,
            [&]() { applyFromDialog(false); });
    connect(&dialog, &SettingsDialog::acceptRequested, &dialog,
            [&]() { applyFromDialog(true); });
    connect(&dialog, &SettingsDialog::shortcutsEdited, &dialog,
            updateShortcutAvailability);
    updateShortcutAvailability();

    dialog.exec();
}

void AppController::handleQuitRequested() {
    m_isQuitting = true;
    m_mainWindow.setCloseToTrayEnabled(false);
    qApp->quit();
}

bool AppController::notifyExistingInstance(int timeoutMs, QString *error) {
    QLocalSocket socket;
    socket.connectToServer(m_singleInstanceName);
    if (!socket.waitForConnected(timeoutMs)) {
        if (error) {
            *error = socket.errorString();
        }
        return false;
    }

    if (socket.write("toggle-popup\n") < 0 || !socket.waitForBytesWritten(timeoutMs)) {
        if (error) {
            *error = socket.errorString();
        }
        socket.disconnectFromServer();
        return false;
    }

    socket.disconnectFromServer();
    if (error) {
        error->clear();
    }
    return true;
}

bool AppController::hasLikelyPeerUiProcess(QString *detail) const {
    const qint64 selfPid = QCoreApplication::applicationPid();
    const QString executableName =
        QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (executableName.trimmed().isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("Unable to determine current executable name");
        }
        return true;
    }

#ifdef Q_OS_WIN
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        if (detail) {
            *detail = QStringLiteral("Process probe unavailable (snapshot failed)");
        }
        return true;
    }

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    QVector<qint64> peerPids;
    const QString targetLower = executableName.toLower();
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const qint64 pid = static_cast<qint64>(entry.th32ProcessID);
            if (pid == selfPid) {
                continue;
            }

            const QString candidate =
                QString::fromWCharArray(entry.szExeFile).trimmed().toLower();
            if (candidate == targetLower) {
                peerPids.push_back(pid);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (peerPids.isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("No peer UI process found");
        }
        return false;
    }

    if (detail) {
        QStringList pidStrings;
        for (qint64 pid : peerPids) {
            pidStrings.push_back(QString::number(pid));
        }
        *detail = QStringLiteral("Found peer UI process PID(s): %1").arg(pidStrings.join(", "));
    }
    return true;
#else
    QProcess ps;
    ps.start(QStringLiteral("ps"),
             QStringList{QStringLiteral("-eo"), QStringLiteral("pid=,args=")});
    if (!ps.waitForStarted(1000)) {
        if (detail) {
            *detail = QStringLiteral("Process probe unavailable (ps did not start)");
        }
        return true;
    }
    if (!ps.waitForFinished(2000)) {
        ps.kill();
        ps.waitForFinished(500);
        if (detail) {
            *detail = QStringLiteral("Process probe unavailable (ps timed out)");
        }
        return true;
    }

    QVector<qint64> peerPids;
    const QString output = QString::fromUtf8(ps.readAllStandardOutput());
    for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        const int firstSpace = trimmed.indexOf(' ');
        if (firstSpace <= 0) {
            continue;
        }

        bool ok = false;
        const qint64 pid = trimmed.left(firstSpace).toLongLong(&ok);
        if (!ok || pid == selfPid) {
            continue;
        }

        const QString args = trimmed.mid(firstSpace + 1).trimmed();
        if (args.isEmpty()) {
            continue;
        }

        const QString commandToken = args.section(' ', 0, 0);
        const QString commandName = QFileInfo(commandToken).fileName();
        if (commandName == executableName) {
            peerPids.push_back(pid);
        }
    }

    if (peerPids.isEmpty()) {
        if (detail) {
            *detail = QStringLiteral("No peer UI process found");
        }
        return false;
    }

    if (detail) {
        QStringList pidStrings;
        for (qint64 pid : peerPids) {
            pidStrings.push_back(QString::number(pid));
        }
        *detail = QStringLiteral("Found peer UI process PID(s): %1").arg(pidStrings.join(", "));
    }
    return true;
#endif
}

bool AppController::startSingleInstanceServer(QString *error) {
    if (!m_singleInstanceServer) {
        m_singleInstanceServer = new QLocalServer(this);
    }

    if (m_singleInstanceServer->isListening()) {
        return true;
    }

    if (!m_singleInstanceServer->listen(m_singleInstanceName)) {
        if (error) {
            *error = m_singleInstanceServer->errorString();
        }
        return false;
    }

    connect(m_singleInstanceServer, &QLocalServer::newConnection, this,
            &AppController::handleSingleInstanceNewConnection, Qt::UniqueConnection);

    return true;
}

AppController::InstanceTakeoverDecision
AppController::promptSingleInstanceTakeover(const QString &detail) {
    QMessageBox dialog(&m_mainWindow);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(QStringLiteral("Pastetry instance conflict"));
    dialog.setText(QStringLiteral(
        "Pastetry detected an in-use instance socket but could not hand off to the existing UI."));
    dialog.setInformativeText(
        QStringLiteral("Retry handoff, take over the stale socket, or exit this launch.\n\n"
                       "Last probe detail: %1")
            .arg(detail.trimmed().isEmpty() ? QStringLiteral("Unknown error") : detail));

    QPushButton *retryButton =
        dialog.addButton(QStringLiteral("Retry handoff"), QMessageBox::AcceptRole);
    QPushButton *takeOverButton =
        dialog.addButton(QStringLiteral("Take over"), QMessageBox::DestructiveRole);
    QPushButton *exitButton = dialog.addButton(QStringLiteral("Exit"), QMessageBox::RejectRole);

    dialog.setDefaultButton(exitButton);
    dialog.exec();

    if (dialog.clickedButton() == retryButton) {
        return InstanceTakeoverDecision::RetryHandoff;
    }
    if (dialog.clickedButton() == takeOverButton) {
        return InstanceTakeoverDecision::TakeOver;
    }
    return InstanceTakeoverDecision::Exit;
}

void AppController::handleSingleInstanceNewConnection() {
    if (!m_singleInstanceServer) {
        return;
    }

    while (m_singleInstanceServer->hasPendingConnections()) {
        QLocalSocket *socket = m_singleInstanceServer->nextPendingConnection();
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            const QString command = QString::fromUtf8(socket->readAll()).trimmed();
            if (!command.isEmpty()) {
                handleSingleInstanceCommand(command);
            }
        });
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);
    }
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

    m_shortcutBindings = defaultShortcutBindings();
    QHash<QString, bool> loadedFromV2;
    for (const auto &spec : allShortcutActionSpecs()) {
        const QString base =
            QStringLiteral("%1/%2").arg(kSettingsActionsPrefix, spec.id);
        const QString modeKey = base + QStringLiteral("/mode");
        const QString directKey = base + QStringLiteral("/direct");
        const QString chordFirstKey = base + QStringLiteral("/chord_first");
        const QString chordSecondKey = base + QStringLiteral("/chord_second");

        const bool hasMode = m_settings.contains(modeKey);
        const bool hasAnyV2 = hasMode || m_settings.contains(directKey) ||
                              m_settings.contains(chordFirstKey) ||
                              m_settings.contains(chordSecondKey);
        if (!hasAnyV2) {
            continue;
        }

        ShortcutBindingConfig binding;
        binding.directSequence =
            QKeySequence::fromString(m_settings.value(directKey).toString());
        binding.chordFirstSequence =
            QKeySequence::fromString(m_settings.value(chordFirstKey).toString());
        binding.chordSecondSequence =
            QKeySequence::fromString(m_settings.value(chordSecondKey).toString());

        if (hasMode) {
            binding.mode = shortcutBindingModeFromString(m_settings.value(modeKey).toString());
        } else if (!binding.directSequence.isEmpty()) {
            binding.mode = ShortcutBindingMode::Direct;
        } else if (!binding.chordFirstSequence.isEmpty() ||
                   !binding.chordSecondSequence.isEmpty()) {
            binding.mode = ShortcutBindingMode::Chord;
        } else {
            binding.mode = ShortcutBindingMode::Disabled;
        }

        m_shortcutBindings.insert(spec.id, binding);
        loadedFromV2.insert(spec.id, true);
    }

    const auto hasV2Action = [&loadedFromV2](const QString &actionId) {
        return loadedFromV2.value(actionId, false);
    };

    if (!hasV2Action(QStringLiteral("quick_paste_popup")) ||
        !hasV2Action(QStringLiteral("open_history_window")) ||
        !hasV2Action(QStringLiteral("open_inspector"))) {
        const QString quickPasteSequence =
            m_settings.contains(kSettingsQuickPasteShortcut)
                ? m_settings.value(kSettingsQuickPasteShortcut).toString()
                : m_settings.value(kSettingsShortcut, QString()).toString();
        const QString openHistorySequence =
            m_settings.value(kSettingsOpenHistoryShortcut, QString()).toString();
        const QString openInspectorSequence =
            m_settings.value(kSettingsOpenInspectorShortcut, QString()).toString();

        ShortcutBindingConfig quickBinding;
        quickBinding.directSequence = QKeySequence::fromString(quickPasteSequence);
        quickBinding.mode = quickBinding.directSequence.isEmpty()
                                ? ShortcutBindingMode::Disabled
                                : ShortcutBindingMode::Direct;
        if (!hasV2Action(QStringLiteral("quick_paste_popup"))) {
            m_shortcutBindings.insert(QStringLiteral("quick_paste_popup"), quickBinding);
        }

        ShortcutBindingConfig historyBinding;
        historyBinding.directSequence = QKeySequence::fromString(openHistorySequence);
        historyBinding.mode = historyBinding.directSequence.isEmpty()
                                  ? ShortcutBindingMode::Disabled
                                  : ShortcutBindingMode::Direct;
        if (!hasV2Action(QStringLiteral("open_history_window"))) {
            m_shortcutBindings.insert(QStringLiteral("open_history_window"), historyBinding);
        }

        ShortcutBindingConfig inspectorBinding;
        inspectorBinding.directSequence = QKeySequence::fromString(openInspectorSequence);
        inspectorBinding.mode = inspectorBinding.directSequence.isEmpty()
                                    ? ShortcutBindingMode::Disabled
                                    : ShortcutBindingMode::Direct;
        if (!hasV2Action(QStringLiteral("open_inspector"))) {
            m_shortcutBindings.insert(QStringLiteral("open_inspector"), inspectorBinding);
        }
    }

    m_autoPasteKey = QKeySequence::fromString(
        m_settings.value(kSettingsAutoPasteKey,
                         QKeySequence(Qt::CTRL | Qt::Key_V).toString())
            .toString());
    if (m_autoPasteKey.isEmpty()) {
        m_autoPasteKey = QKeySequence(Qt::CTRL | Qt::Key_V);
    }

    m_popupGeometry = m_settings.value(kSettingsPopupGeometry).toByteArray();
    if (m_settings.contains(kSettingsQuickPasteSize)) {
        m_quickPasteSize = m_settings.value(kSettingsQuickPasteSize).toSize();
    } else {
        m_quickPasteSize = QSize();
    }
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
    bool changed = false;
    auto setValueIfChanged = [this, &changed](const QString &key, const QVariant &value) {
        if (m_settings.value(key) == value) {
            return;
        }
        m_settings.setValue(key, value);
        changed = true;
    };

    setValueIfChanged(QString::fromUtf8(kSettingsStartToTray), m_startToTray);

    for (const auto &spec : allShortcutActionSpecs()) {
        const ShortcutBindingConfig binding = m_shortcutBindings.value(spec.id);
        const QString base =
            QStringLiteral("%1/%2").arg(kSettingsActionsPrefix, spec.id);
        setValueIfChanged(base + QStringLiteral("/mode"),
                          shortcutBindingModeToString(binding.mode));
        setValueIfChanged(base + QStringLiteral("/direct"),
                          binding.directSequence.toString());
        setValueIfChanged(base + QStringLiteral("/chord_first"),
                          binding.chordFirstSequence.toString());
        setValueIfChanged(base + QStringLiteral("/chord_second"),
                          binding.chordSecondSequence.toString());
    }

    setValueIfChanged(QString::fromUtf8(kSettingsAutoPasteKey), m_autoPasteKey.toString());

    const ShortcutBindingConfig quickBinding =
        m_shortcutBindings.value(QStringLiteral("quick_paste_popup"));
    const QString quickLegacy =
        (quickBinding.mode == ShortcutBindingMode::Direct)
            ? quickBinding.directSequence.toString()
            : QString();
    setValueIfChanged(QString::fromUtf8(kSettingsShortcut), quickLegacy);
    setValueIfChanged(QString::fromUtf8(kSettingsQuickPasteShortcut), quickLegacy);

    const ShortcutBindingConfig historyBinding =
        m_shortcutBindings.value(QStringLiteral("open_history_window"));
    setValueIfChanged(QString::fromUtf8(kSettingsOpenHistoryShortcut),
                      historyBinding.mode == ShortcutBindingMode::Direct
                          ? historyBinding.directSequence.toString()
                          : QString());

    const ShortcutBindingConfig inspectorBinding =
        m_shortcutBindings.value(QStringLiteral("open_inspector"));
    setValueIfChanged(QString::fromUtf8(kSettingsOpenInspectorShortcut),
                      inspectorBinding.mode == ShortcutBindingMode::Direct
                          ? inspectorBinding.directSequence.toString()
                          : QString());

    setValueIfChanged(QString::fromUtf8(kSettingsPopupGeometry), m_popupGeometry);
    if (m_quickPasteSize.isValid()) {
        setValueIfChanged(QString::fromUtf8(kSettingsQuickPasteSize), m_quickPasteSize);
    }
    setValueIfChanged(QString::fromUtf8(kSettingsHistoryColumns),
                      serializeColumns(m_historyColumns));
    setValueIfChanged(QString::fromUtf8(kSettingsQuickColumns),
                      serializeColumns(m_quickPasteColumns));
    setValueIfChanged(QString::fromUtf8(kSettingsPreviewLines), m_previewLineCount);
    setValueIfChanged(QString::fromUtf8(kSettingsSearchMode),
                      searchModeToString(m_searchMode));
    setValueIfChanged(QString::fromUtf8(kSettingsRegexStrict), m_regexStrictFullScan);

    if (changed) {
        m_settings.sync();
    }
}

void AppController::clearShortcutRegistrations() {
    for (GlobalShortcutService *service : m_baseShortcutServices) {
        if (!service) {
            continue;
        }
        service->unregisterShortcut();
        service->deleteLater();
    }
    m_baseShortcutServices.clear();
    m_serviceToDirectAction.clear();
    m_serviceToChordFirstKey.clear();
    m_chordFirstToActions.clear();
}

void AppController::clearChordCaptureRegistrations() {
    m_chordTimeoutTimer.stop();
    for (GlobalShortcutService *service : m_chordSecondShortcutServices) {
        if (!service) {
            continue;
        }
        service->unregisterShortcut();
        service->deleteLater();
    }
    m_chordSecondShortcutServices.clear();
    m_serviceToChordSecondAction.clear();
    m_pendingChordActions.clear();
    m_chordCaptureActive = false;
}

void AppController::applyShortcutSettings(bool notifyFailures) {
    clearChordCaptureRegistrations();
    clearShortcutRegistrations();

    m_shortcutStates.clear();
    m_shortcutErrors.clear();
    for (const auto &spec : allShortcutActionSpecs()) {
        m_shortcutStates.insert(spec.id, ShortcutRegistrationState::Disabled);
        m_shortcutErrors.insert(spec.id, QString());
    }

    const ShortcutValidationResult validation =
        validateShortcutBindings(m_shortcutBindings);
    for (auto it = validation.actionErrors.cbegin(); it != validation.actionErrors.cend();
         ++it) {
        m_shortcutStates[it.key()] = ShortcutRegistrationState::InvalidBinding;
        m_shortcutErrors[it.key()] = it.value();
    }

    QHash<QString, QString> directActionByKey;
    QHash<QString, QVector<QString>> chordActionsByFirst;
    for (const auto &spec : allShortcutActionSpecs()) {
        if (validation.actionErrors.contains(spec.id)) {
            continue;
        }

        const ShortcutBindingConfig binding = m_shortcutBindings.value(spec.id);
        if (binding.mode == ShortcutBindingMode::Direct) {
            const QString key = shortcutKey(binding.directSequence);
            if (!key.isEmpty()) {
                directActionByKey.insert(key, spec.id);
            }
        } else if (binding.mode == ShortcutBindingMode::Chord) {
            const QString key = shortcutKey(binding.chordFirstSequence);
            if (!key.isEmpty()) {
                chordActionsByFirst[key].push_back(spec.id);
            }
        }
    }

    for (auto it = directActionByKey.cbegin(); it != directActionByKey.cend(); ++it) {
        const QString actionId = it.value();
        const ShortcutBindingConfig binding = m_shortcutBindings.value(actionId);

        auto *service = new GlobalShortcutService(this, m_nextHotkeyId++);
        const ShortcutRegistrationState state =
            service->registerShortcut(binding.directSequence, true);
        const QString detail = service->lastError();

        m_shortcutStates[actionId] = state;
        m_shortcutErrors[actionId] = detail;

        if (state == ShortcutRegistrationState::Registered) {
            connect(service, &GlobalShortcutService::activated, this,
                    [this, actionId] { handleShortcutAction(actionId); });
            m_baseShortcutServices.push_back(service);
            m_serviceToDirectAction.insert(service, actionId);
        } else {
            service->deleteLater();
            if (notifyFailures &&
                (state == ShortcutRegistrationState::Failed ||
                 state == ShortcutRegistrationState::Unavailable)) {
                notifyShortcutWarning(
                    QStringLiteral("Pastetry shortcut"),
                    QStringLiteral("%1: %2")
                        .arg(actionLabel(actionId),
                             stateDetailText(state, detail)));
            }
        }
    }

    for (auto it = chordActionsByFirst.cbegin(); it != chordActionsByFirst.cend(); ++it) {
        const QVector<QString> actions = it.value();
        if (actions.isEmpty()) {
            continue;
        }

        const ShortcutBindingConfig binding =
            m_shortcutBindings.value(actions.first());
        auto *service = new GlobalShortcutService(this, m_nextHotkeyId++);
        const ShortcutRegistrationState state =
            service->registerShortcut(binding.chordFirstSequence, true);
        const QString detail = service->lastError();

        for (const QString &actionId : actions) {
            m_shortcutStates[actionId] = state;
            m_shortcutErrors[actionId] = detail;
        }

        if (state == ShortcutRegistrationState::Registered) {
            connect(service, &GlobalShortcutService::activated, this,
                    [this, firstKey = it.key()] { beginChordCapture(firstKey); });
            m_baseShortcutServices.push_back(service);
            m_serviceToChordFirstKey.insert(service, it.key());
            m_chordFirstToActions.insert(it.key(), actions);
        } else {
            service->deleteLater();
            if (notifyFailures &&
                (state == ShortcutRegistrationState::Failed ||
                 state == ShortcutRegistrationState::Unavailable)) {
                for (const QString &actionId : actions) {
                    notifyShortcutWarning(
                        QStringLiteral("Pastetry shortcut"),
                        QStringLiteral("%1: %2")
                            .arg(actionLabel(actionId),
                                 stateDetailText(state, detail)));
                }
            }
        }
    }
}

void AppController::beginChordCapture(const QString &firstKeyPortable) {
    if (m_chordCaptureActive) {
        endChordCapture();
    }

    const QVector<QString> actions = m_chordFirstToActions.value(firstKeyPortable);
    if (actions.isEmpty()) {
        return;
    }

    m_chordCaptureActive = true;
    m_pendingChordActions = actions;

    clearShortcutRegistrations();

    for (const QString &actionId : actions) {
        const ShortcutBindingConfig binding = m_shortcutBindings.value(actionId);
        auto *service = new GlobalShortcutService(this, m_nextHotkeyId++);
        const ShortcutRegistrationState state =
            service->registerShortcut(binding.chordSecondSequence, false);

        if (state == ShortcutRegistrationState::Registered) {
            connect(service, &GlobalShortcutService::activated, this,
                    [this, actionId] {
                        handleShortcutAction(actionId);
                        endChordCapture();
                    });
            m_chordSecondShortcutServices.push_back(service);
            m_serviceToChordSecondAction.insert(service, actionId);
            continue;
        }

        m_shortcutErrors[actionId] = service->lastError();
        service->deleteLater();
    }

    if (m_chordSecondShortcutServices.isEmpty()) {
        notifyShortcutWarning(
            QStringLiteral("Pastetry shortcut"),
            QStringLiteral("Unable to start chord second-step capture."));
        endChordCapture();
        return;
    }

    m_chordTimeoutTimer.start();
}

void AppController::endChordCapture() {
    if (!m_chordCaptureActive && m_chordSecondShortcutServices.isEmpty()) {
        return;
    }

    clearChordCaptureRegistrations();
    applyShortcutSettings(false);
}

void AppController::handleShortcutAction(const QString &actionId) {
    if (actionId == QStringLiteral("quick_paste_popup")) {
        showQuickPastePopup();
        return;
    }
    if (actionId == QStringLiteral("open_history_window")) {
        showMainWindow();
        return;
    }
    if (actionId == QStringLiteral("open_inspector")) {
        openClipboardInspector();
        return;
    }

    const ShortcutActionSpec *spec = findShortcutActionSpec(actionId);
    if (!spec || !spec->isSlotAction) {
        return;
    }
    executeSlotShortcut(*spec);
}

void AppController::executeSlotShortcut(const ShortcutActionSpec &spec) {
    QCborMap resolveParams;
    resolveParams.insert(QStringLiteral("group"),
                         spec.pinnedGroup ? QStringLiteral("pinned")
                                          : QStringLiteral("recent_non_pinned"));
    resolveParams.insert(QStringLiteral("slot"), spec.slot);

    m_ipcRunner.request(
        QStringLiteral("ResolveSlotEntry"), resolveParams, 1800, this,
        [this, spec](const QCborMap &resolved, const QString &resolveError) {
            if (!resolveError.isEmpty()) {
                notifyShortcutWarning(QStringLiteral("Pastetry shortcut"),
                                      QStringLiteral("%1: %2").arg(spec.label, resolveError));
                return;
            }

            const qint64 entryId = resolved.value(QStringLiteral("entry_id")).toInteger();
            if (entryId <= 0) {
                notifyShortcutWarning(QStringLiteral("Pastetry shortcut"),
                                      QStringLiteral("%1: slot %2 is empty")
                                          .arg(spec.label)
                                          .arg(spec.slot));
                return;
            }

            QCborMap activateParams;
            activateParams.insert(QStringLiteral("entry_id"), entryId);
            activateParams.insert(QStringLiteral("preferred_format"), QStringLiteral(""));
            m_ipcRunner.request(
                QStringLiteral("ActivateEntry"), activateParams, 2500, this,
                [this, spec](const QCborMap &, const QString &activateError) {
                    if (!activateError.isEmpty()) {
                        notifyShortcutWarning(
                            QStringLiteral("Pastetry shortcut"),
                            QStringLiteral("%1: %2").arg(spec.label, activateError));
                        return;
                    }

                    if (!spec.isPasteAction) {
                        return;
                    }

                    QString pasteError;
                    if (!sendSyntheticPaste(&pasteError)) {
                        notifyShortcutWarning(
                            QStringLiteral("Pastetry shortcut"),
                            QStringLiteral("%1: auto-paste failed: %2")
                                .arg(spec.label,
                                     pasteError.isEmpty() ? QStringLiteral("unsupported")
                                                          : pasteError));
                    }
                });
        });
}

bool AppController::sendSyntheticPaste(QString *error) const {
    const int keySpec = m_autoPasteKey.count() > 0 ? m_autoPasteKey[0].toCombined() : 0;
    if (keySpec == 0) {
        if (error) {
            *error = QStringLiteral("auto-paste key is not configured");
        }
        return false;
    }

    const Qt::KeyboardModifiers modifiers =
        Qt::KeyboardModifiers(keySpec & Qt::KeyboardModifierMask);
    const int qtKey = keySpec & ~Qt::KeyboardModifierMask;

#ifdef Q_OS_WIN
    UINT vk = 0;
    if (!qtKeyToWindowsVk(qtKey, &vk)) {
        if (error) {
            *error = QStringLiteral("unsupported key in auto-paste shortcut");
        }
        return false;
    }

    QVector<INPUT> inputs;
    auto pushVk = [&inputs](WORD keyCode, bool down) {
        INPUT input = {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = keyCode;
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        inputs.push_back(input);
    };

    if (modifiers & Qt::ControlModifier) {
        pushVk(VK_CONTROL, true);
    }
    if (modifiers & Qt::ShiftModifier) {
        pushVk(VK_SHIFT, true);
    }
    if (modifiers & Qt::AltModifier) {
        pushVk(VK_MENU, true);
    }
    if (modifiers & Qt::MetaModifier) {
        pushVk(VK_LWIN, true);
    }

    pushVk(static_cast<WORD>(vk), true);
    pushVk(static_cast<WORD>(vk), false);

    if (modifiers & Qt::MetaModifier) {
        pushVk(VK_LWIN, false);
    }
    if (modifiers & Qt::AltModifier) {
        pushVk(VK_MENU, false);
    }
    if (modifiers & Qt::ShiftModifier) {
        pushVk(VK_SHIFT, false);
    }
    if (modifiers & Qt::ControlModifier) {
        pushVk(VK_CONTROL, false);
    }

    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    if (sent != static_cast<UINT>(inputs.size())) {
        if (error) {
            *error = QStringLiteral("SendInput failed");
        }
        return false;
    }

    return true;
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
#if defined(PASTETRY_HAVE_XTEST)
    if (!QGuiApplication::platformName().contains(QStringLiteral("xcb"))) {
        if (error) {
            *error = QStringLiteral("auto-paste is unavailable outside X11 sessions");
        }
        return false;
    }

    auto *x11 = qGuiApp
                    ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>()
                    : nullptr;
    if (!x11 || !x11->display()) {
        if (error) {
            *error = QStringLiteral("X11 interface unavailable");
        }
        return false;
    }

    KeySym keySym = NoSymbol;
    if (!qtKeyToX11KeySym(qtKey, &keySym)) {
        if (error) {
            *error = QStringLiteral("unsupported key in auto-paste shortcut");
        }
        return false;
    }

    Display *display = x11->display();
    const KeyCode keyCode = XKeysymToKeycode(display, keySym);
    if (keyCode == 0) {
        if (error) {
            *error = QStringLiteral("failed to resolve keycode");
        }
        return false;
    }

    QVector<KeyCode> modifierCodes;
    if (modifiers & Qt::ControlModifier) {
        modifierCodes.push_back(XKeysymToKeycode(display, XK_Control_L));
    }
    if (modifiers & Qt::ShiftModifier) {
        modifierCodes.push_back(XKeysymToKeycode(display, XK_Shift_L));
    }
    if (modifiers & Qt::AltModifier) {
        modifierCodes.push_back(XKeysymToKeycode(display, XK_Alt_L));
    }
    if (modifiers & Qt::MetaModifier) {
        modifierCodes.push_back(XKeysymToKeycode(display, XK_Super_L));
    }

    for (const KeyCode modCode : modifierCodes) {
        if (modCode != 0) {
            XTestFakeKeyEvent(display, modCode, True, CurrentTime);
        }
    }
    XTestFakeKeyEvent(display, keyCode, True, CurrentTime);
    XTestFakeKeyEvent(display, keyCode, False, CurrentTime);
    for (int i = modifierCodes.size() - 1; i >= 0; --i) {
        const KeyCode modCode = modifierCodes.at(i);
        if (modCode != 0) {
            XTestFakeKeyEvent(display, modCode, False, CurrentTime);
        }
    }
    XFlush(display);
    return true;
#else
    if (error) {
        *error = QStringLiteral("X11 XTest library not available in this build");
    }
    return false;
#endif
#else
    if (error) {
        *error = QStringLiteral("auto-paste unsupported on this platform");
    }
    return false;
#endif
}

void AppController::notifyShortcutWarning(const QString &title,
                                          const QString &message) {
    if (m_trayIcon && m_trayIcon->isVisible()) {
        m_trayIcon->showMessage(title, message, QSystemTrayIcon::Warning, 3500);
        return;
    }
    QMessageBox::warning(&m_mainWindow, title, message);
}

void AppController::checkDaemonConnectivity(bool notifyIfUnavailable) {
    m_pingNotifyIfUnavailable = m_pingNotifyIfUnavailable || notifyIfUnavailable;
    if (m_pingInFlight) {
        return;
    }

    const bool shouldNotifyIfUnavailable = m_pingNotifyIfUnavailable;
    m_pingNotifyIfUnavailable = false;
    m_pingInFlight = true;
    m_ipcRunner.request(
        QStringLiteral("Ping"), QCborMap{}, 800, this,
        [this, shouldNotifyIfUnavailable](const QCborMap &, const QString &error) {
            m_pingInFlight = false;
            const bool reachable = error.isEmpty();
            if (!m_daemonStatusKnown) {
                m_daemonStatusKnown = true;
                m_daemonReachable = reachable;
                if (!reachable && shouldNotifyIfUnavailable) {
                    notifyDaemonUnavailable(error);
                }
            } else if (reachable != m_daemonReachable) {
                m_daemonReachable = reachable;
                if (reachable) {
                    notifyDaemonRecovered();
                } else {
                    notifyDaemonUnavailable(error);
                }
            }

            if (m_pingNotifyIfUnavailable) {
                checkDaemonConnectivity(false);
            }
        });
}

void AppController::notifyDaemonUnavailable(const QString &reason) {
    const QString detail = reason.isEmpty() ? QStringLiteral("Unknown error") : reason;
    const QString message = QStringLiteral(
                                "Cannot connect to daemon (pastetry-clipd).\n"
                                "Clipboard capture and activation may not work until the daemon "
                                "is running.\n\n"
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

    m_ipcRunner.request(
        QStringLiteral("GetCapturePolicy"), QCborMap{}, 1800, this,
        [this](const QCborMap &result, const QString &policyError) {
            if (!policyError.trimmed().isEmpty()) {
                qWarning().noquote()
                    << QStringLiteral("Failed to refresh capture policy after daemon recovery: %1")
                           .arg(policyError);
                return;
            }

            CapturePolicy loaded;
            QString parseError;
            if (!parseCapturePolicyFromCborResult(result, &loaded, &parseError)) {
                qWarning().noquote()
                    << QStringLiteral("Failed to refresh capture policy after daemon recovery: %1")
                           .arg(parseError);
                return;
            }
            m_capturePolicy = loaded;
        });
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
