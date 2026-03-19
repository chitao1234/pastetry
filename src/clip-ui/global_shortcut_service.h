#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>

namespace pastetry {

enum class ShortcutRegistrationState {
    Registered,
    Disabled,
    Unavailable,
    InvalidBinding,
    Failed,
};

class GlobalShortcutService : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit GlobalShortcutService(QObject *parent = nullptr, int windowsHotkeyId = 1);
    ~GlobalShortcutService() override;

    ShortcutRegistrationState registerShortcut(const QKeySequence &sequence);
    void unregisterShortcut();

    QString lastError() const;
    bool isSupported() const;

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

signals:
    void activated();

private:
    ShortcutRegistrationState registerWindowsShortcut(const QKeySequence &sequence);
    ShortcutRegistrationState registerX11Shortcut(const QKeySequence &sequence);

    void unregisterWindowsShortcut();
    void unregisterX11Shortcut();

    QString m_lastError;
    bool m_filterInstalled = false;

#ifdef Q_OS_WIN
    bool m_windowsRegistered = false;
#endif
    int m_hotkeyId = 1;

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    unsigned int m_x11Modifiers = 0;
    unsigned int m_x11Keycode = 0;
    unsigned int m_x11NumLockMask = 0;
    bool m_x11Registered = false;
#endif
};

}  // namespace pastetry
