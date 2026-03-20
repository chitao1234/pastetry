#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class QDBusMessage;
#if defined(PASTETRY_HAVE_DBUS)
class QDBusArgument;
#endif

namespace pastetry {

enum class ShortcutRegistrationState {
    Registered,
    Disabled,
    Unavailable,
    InvalidBinding,
    Failed,
};

enum class ShortcutInteractionPolicy {
    NonInteractive,
    Interactive,
};

enum class ShortcutBackendKind {
    Auto,
    Windows,
    X11,
    WaylandPortal,
    WaylandWlroots,
    Disabled,
};

#if defined(PASTETRY_HAVE_DBUS)
struct PortalShortcutBinding {
    QString id;
    QVariantMap options;
};
using PortalShortcutBindingList = QList<PortalShortcutBinding>;

QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcutBinding &binding);
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcutBinding &binding);
#endif

class GlobalShortcutService : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit GlobalShortcutService(QObject *parent = nullptr, int windowsHotkeyId = 1);
    ~GlobalShortcutService() override;

    ShortcutRegistrationState registerShortcut(const QKeySequence &sequence,
                                               bool requireModifier = true,
                                               ShortcutInteractionPolicy interactionPolicy =
                                                   ShortcutInteractionPolicy::Interactive);
    void unregisterShortcut();

    QString lastError() const;
    bool isSupported() const;
    ShortcutBackendKind selectedBackend() const;

    bool nativeEventFilter(const QByteArray &eventType, void *message,
                           qintptr *result) override;

signals:
    void activated();

private:
    ShortcutRegistrationState registerWithBackend(ShortcutBackendKind backend,
                                                  const QKeySequence &sequence,
                                                  bool requireModifier,
                                                  ShortcutInteractionPolicy interactionPolicy);
    ShortcutRegistrationState registerWindowsShortcut(const QKeySequence &sequence,
                                                      bool requireModifier);
    ShortcutRegistrationState registerX11Shortcut(const QKeySequence &sequence,
                                                  bool requireModifier);
    ShortcutRegistrationState registerWaylandPortalShortcut(
        const QKeySequence &sequence, bool requireModifier,
        ShortcutInteractionPolicy interactionPolicy);
    ShortcutRegistrationState registerWaylandWlrootsShortcut(
        const QKeySequence &sequence, bool requireModifier,
        ShortcutInteractionPolicy interactionPolicy);

    void unregisterWindowsShortcut();
    void unregisterX11Shortcut();
    void unregisterWaylandPortalShortcut();

    QString backendUnavailableText(ShortcutBackendKind backend) const;

#if defined(PASTETRY_HAVE_DBUS)
private slots:
    void onPortalRequestResponse(const QDBusMessage &message);
    void onPortalShortcutActivated(const QDBusMessage &message);
#endif

#if defined(PASTETRY_HAVE_DBUS)
private:
    struct WaylandPortalCapabilities {
        bool sessionBusConnected = false;
        bool portalServiceRegistered = false;
        int globalShortcutsVersion = -1;
        int clipboardVersion = -1;
        int inputCaptureVersion = -1;
        bool kdeGlobalAccelServiceRegistered = false;
    };

    WaylandPortalCapabilities probeWaylandPortalCapabilities() const;
    bool isWaylandPortalShortcutAvailable(QString *error = nullptr) const;
    bool listPortalShortcuts(QStringList *shortcutIds, QString *error);
    void logWaylandCapabilityProbe() const;

    bool waitForPortalRequest(const QString &requestPath, int timeoutMs,
                              QVariantMap *results, QString *error);
    bool createPortalShortcutSession(QString *error);
    bool bindPortalShortcut(const QString &trigger, QString *error);
    void closePortalShortcutSession();
    QString qtSequenceToPortalTrigger(const QKeySequence &sequence,
                                      bool requireModifier, QString *error) const;
#endif

    QString m_lastError;
    bool m_filterInstalled = false;

#ifdef Q_OS_WIN
    bool m_windowsRegistered = false;
#endif
    int m_hotkeyId = 1;

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC) && defined(PASTETRY_HAVE_X11)
    unsigned int m_x11Modifiers = 0;
    unsigned int m_x11Keycode = 0;
    unsigned int m_x11IgnoredMask = 0;
    QVector<unsigned int> m_x11GrabVariants;
    bool m_x11Registered = false;
#endif

#if defined(PASTETRY_HAVE_DBUS)
    QString m_portalRequestPath;
    bool m_portalRequestCompleted = false;
    uint m_portalRequestCode = 2;
    QVariantMap m_portalRequestResults;
    QString m_portalSessionPath;
    QString m_portalShortcutId;
    bool m_portalRegistered = false;
    mutable bool m_waylandCapabilityLogged = false;
#endif
};

}  // namespace pastetry

#if defined(PASTETRY_HAVE_DBUS)
Q_DECLARE_METATYPE(pastetry::PortalShortcutBinding)
Q_DECLARE_METATYPE(pastetry::PortalShortcutBindingList)
#endif
