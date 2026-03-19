#pragma once

#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QObject>
#include <QVector>

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

    ShortcutRegistrationState registerShortcut(const QKeySequence &sequence,
                                               bool requireModifier = true);
    void unregisterShortcut();

    QString lastError() const;
    bool isSupported() const;

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

signals:
    void activated();

private:
    ShortcutRegistrationState registerWindowsShortcut(const QKeySequence &sequence,
                                                      bool requireModifier);
    ShortcutRegistrationState registerX11Shortcut(const QKeySequence &sequence,
                                                  bool requireModifier);

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
    unsigned int m_x11IgnoredMask = 0;
    QVector<unsigned int> m_x11GrabVariants;
    bool m_x11Registered = false;
#endif
};

}  // namespace pastetry
