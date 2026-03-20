#include "clip-ui/global_shortcut_service.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QtCore/qnativeinterface.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
#include <QtGui/qguiapplication_platform.h>
#endif

#if defined(PASTETRY_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <algorithm>
#endif

#if defined(PASTETRY_HAVE_DBUS)
#include <QtDBus/QDBusArgument>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>
#include <QtDBus/QDBusPendingReply>
#include <QtDBus/QDBusReply>
#include <QtDBus/qdbusmetatype.h>
#endif

Q_LOGGING_CATEGORY(logShortcut, "pastetry.shortcut")

namespace pastetry {

namespace {
int keyFromSequence(const QKeySequence &sequence) {
    return sequence.count() > 0 ? sequence[0].toCombined() : 0;
}

bool isWaylandPlatform() {
    return QGuiApplication::platformName().contains(QStringLiteral("wayland"),
                                                    Qt::CaseInsensitive);
}

bool isX11Platform() {
    return QGuiApplication::platformName().contains(QStringLiteral("xcb"),
                                                    Qt::CaseInsensitive);
}

ShortcutBackendKind backendKindFromText(const QString &value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("auto")) {
        return ShortcutBackendKind::Auto;
    }
    if (normalized == QStringLiteral("windows")) {
        return ShortcutBackendKind::Windows;
    }
    if (normalized == QStringLiteral("x11")) {
        return ShortcutBackendKind::X11;
    }
    if (normalized == QStringLiteral("wayland_portal")) {
        return ShortcutBackendKind::WaylandPortal;
    }
    if (normalized == QStringLiteral("wayland_wlroots")) {
        return ShortcutBackendKind::WaylandWlroots;
    }
    if (normalized == QStringLiteral("disabled") ||
        normalized == QStringLiteral("none")) {
        return ShortcutBackendKind::Disabled;
    }
    return ShortcutBackendKind::Auto;
}

QString backendKindName(ShortcutBackendKind backend) {
    switch (backend) {
        case ShortcutBackendKind::Windows:
            return QStringLiteral("windows");
        case ShortcutBackendKind::X11:
            return QStringLiteral("x11");
        case ShortcutBackendKind::WaylandPortal:
            return QStringLiteral("wayland_portal");
        case ShortcutBackendKind::WaylandWlroots:
            return QStringLiteral("wayland_wlroots");
        case ShortcutBackendKind::Disabled:
            return QStringLiteral("disabled");
        case ShortcutBackendKind::Auto:
        default:
            return QStringLiteral("auto");
    }
}

bool isBackendForcedByEnv() {
    return backendKindFromText(qEnvironmentVariable("PASTETRY_SHORTCUT_BACKEND")) !=
           ShortcutBackendKind::Auto;
}

#if defined(PASTETRY_HAVE_X11)
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
        case Qt::Key_Space:
            *outSym = XK_space;
            return true;
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
        case Qt::Key_Tab:
            *outSym = XK_Tab;
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            *outSym = XK_Return;
            return true;
        case Qt::Key_Escape:
            *outSym = XK_Escape;
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
        case Qt::Key_Insert:
            *outSym = XK_Insert;
            return true;
        case Qt::Key_Delete:
            *outSym = XK_Delete;
            return true;
        default:
            return false;
    }
}

unsigned int qtModsToX11(Qt::KeyboardModifiers modifiers) {
    unsigned int mask = 0;
    if (modifiers & Qt::ShiftModifier) {
        mask |= ShiftMask;
    }
    if (modifiers & Qt::ControlModifier) {
        mask |= ControlMask;
    }
    if (modifiers & Qt::AltModifier) {
        mask |= Mod1Mask;
    }
    if (modifiers & Qt::MetaModifier) {
        mask |= Mod4Mask;
    }
    return mask;
}

thread_local int *g_x11GrabErrorCode = nullptr;

int x11GrabErrorHandler(Display *display, XErrorEvent *event) {
    Q_UNUSED(display);
    if (g_x11GrabErrorCode && event && *g_x11GrabErrorCode == 0) {
        *g_x11GrabErrorCode = static_cast<int>(event->error_code);
    }
    return 0;
}

QString x11ErrorCodeText(Display *display, int errorCode) {
    if (!display || errorCode <= 0) {
        return QStringLiteral("Unknown X11 error");
    }

    char buffer[256] = {};
    XGetErrorText(display, errorCode, buffer, sizeof(buffer));
    const QString text = QString::fromLatin1(buffer).trimmed();
    if (!text.isEmpty()) {
        return text;
    }

    return QStringLiteral("X11 error code %1").arg(errorCode);
}

unsigned int detectModifierMask(Display *display, KeySym targetSym) {
    if (!display) {
        return 0;
    }

    XModifierKeymap *modMap = XGetModifierMapping(display);
    if (!modMap) {
        return 0;
    }

    const KeyCode targetCode = XKeysymToKeycode(display, targetSym);
    if (targetCode == 0) {
        XFreeModifiermap(modMap);
        return 0;
    }

    unsigned int mask = 0;
    for (int modIndex = 0; modIndex < 8; ++modIndex) {
        for (int keyIndex = 0; keyIndex < modMap->max_keypermod; ++keyIndex) {
            const int offset = modIndex * modMap->max_keypermod + keyIndex;
            if (modMap->modifiermap[offset] == targetCode) {
                mask = static_cast<unsigned int>(1U << modIndex);
                break;
            }
        }
        if (mask != 0) {
            break;
        }
    }

    XFreeModifiermap(modMap);
    return mask;
}

unsigned int detectIgnoredX11Mask(Display *display) {
    unsigned int ignored = 0;
    ignored |= detectModifierMask(display, XK_Num_Lock);
    ignored |= detectModifierMask(display, XK_Scroll_Lock);
    ignored |= detectModifierMask(display, XK_Mode_switch);
    ignored |= detectModifierMask(display, XK_ISO_Level3_Shift);
    ignored |= detectModifierMask(display, XK_ISO_Level5_Shift);

    ignored &= ~(ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
    return ignored;
}

QVector<unsigned int> buildGrabVariants(unsigned int toggledMaskBits) {
    QVector<unsigned int> bits;
    bits.reserve(8);
    for (int bit = 0; bit < 32; ++bit) {
        const unsigned int mask = (1U << bit);
        if ((toggledMaskBits & mask) != 0U) {
            bits.push_back(mask);
        }
    }

    QVector<unsigned int> variants;
    const int variantCount = 1 << bits.size();
    variants.reserve(std::max(1, variantCount));

    for (int i = 0; i < variantCount; ++i) {
        unsigned int combined = 0;
        for (int bitIndex = 0; bitIndex < bits.size(); ++bitIndex) {
            if ((i & (1 << bitIndex)) != 0) {
                combined |= bits[bitIndex];
            }
        }
        variants.push_back(combined);
    }

    if (variants.isEmpty()) {
        variants.push_back(0);
    }
    return variants;
}
#endif

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
        case Qt::Key_Tab:
            *outVk = VK_TAB;
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            *outVk = VK_RETURN;
            return true;
        case Qt::Key_Escape:
            *outVk = VK_ESCAPE;
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
        case Qt::Key_Insert:
            *outVk = VK_INSERT;
            return true;
        case Qt::Key_Delete:
            *outVk = VK_DELETE;
            return true;
        default:
            return false;
    }
}

UINT qtModsToWindows(Qt::KeyboardModifiers modifiers) {
    UINT mask = 0;
    if (modifiers & Qt::AltModifier) {
        mask |= MOD_ALT;
    }
    if (modifiers & Qt::ControlModifier) {
        mask |= MOD_CONTROL;
    }
    if (modifiers & Qt::ShiftModifier) {
        mask |= MOD_SHIFT;
    }
    if (modifiers & Qt::MetaModifier) {
        mask |= MOD_WIN;
    }
    return mask;
}
#endif

#if defined(PASTETRY_HAVE_DBUS)
void ensurePortalDbusTypesRegistered() {
    static bool registered = false;
    if (registered) {
        return;
    }
    qRegisterMetaType<pastetry::PortalShortcutBinding>("pastetry::PortalShortcutBinding");
    qRegisterMetaType<pastetry::PortalShortcutBindingList>(
        "pastetry::PortalShortcutBindingList");
    qDBusRegisterMetaType<pastetry::PortalShortcutBinding>();
    qDBusRegisterMetaType<pastetry::PortalShortcutBindingList>();
    registered = true;
}

QString dbusPathFromVariant(const QVariant &value) {
    if (value.canConvert<QDBusObjectPath>()) {
        const QDBusObjectPath path = qvariant_cast<QDBusObjectPath>(value);
        if (!path.path().trimmed().isEmpty()) {
            return path.path();
        }
    }

    const QString asText = value.toString().trimmed();
    if (!asText.isEmpty()) {
        return asText;
    }

    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument dbusArg = qvariant_cast<QDBusArgument>(value);
        QDBusObjectPath path;
        dbusArg >> path;
        return path.path().trimmed();
    }

    return QString();
}

QVariantMap dbusMapFromVariant(const QVariant &value) {
    if (value.canConvert<QVariantMap>()) {
        return value.toMap();
    }

    QVariantMap mapped = qdbus_cast<QVariantMap>(value);
    if (!mapped.isEmpty()) {
        return mapped;
    }

    if (value.userType() == qMetaTypeId<QDBusArgument>()) {
        const QDBusArgument dbusArg = qvariant_cast<QDBusArgument>(value);
        dbusArg >> mapped;
        return mapped;
    }

    return {};
}

QString portalToken(const QString &prefix) {
    QString token = QStringLiteral("pastetry_%1_%2")
                        .arg(prefix,
                             QUuid::createUuid().toString(QUuid::Id128));
    token.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")),
                  QStringLiteral("_"));
    return token;
}
#endif

}  // namespace

// DBus marshalling for a(sa{sv}) shortcut list entries.
#if defined(PASTETRY_HAVE_DBUS)
QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcutBinding &binding) {
    arg.beginStructure();
    arg << binding.id << binding.options;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg,
                                PortalShortcutBinding &binding) {
    arg.beginStructure();
    arg >> binding.id >> binding.options;
    arg.endStructure();
    return arg;
}
#endif

GlobalShortcutService::GlobalShortcutService(QObject *parent, int windowsHotkeyId)
    : QObject(parent), m_hotkeyId(windowsHotkeyId) {}

GlobalShortcutService::~GlobalShortcutService() {
    unregisterShortcut();
}

ShortcutBackendKind GlobalShortcutService::selectedBackend() const {
    const ShortcutBackendKind overridden =
        backendKindFromText(qEnvironmentVariable("PASTETRY_SHORTCUT_BACKEND"));
    if (overridden != ShortcutBackendKind::Auto) {
        return overridden;
    }

#ifdef Q_OS_WIN
    return ShortcutBackendKind::Windows;
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    if (isWaylandPlatform()) {
        return ShortcutBackendKind::WaylandPortal;
    }
    if (isX11Platform()) {
        return ShortcutBackendKind::X11;
    }
    return ShortcutBackendKind::Disabled;
#else
    return ShortcutBackendKind::Disabled;
#endif
}

ShortcutRegistrationState GlobalShortcutService::registerShortcut(
    const QKeySequence &sequence, bool requireModifier,
    ShortcutInteractionPolicy interactionPolicy) {
    m_lastError.clear();
    unregisterShortcut();

    if (sequence.isEmpty()) {
        return ShortcutRegistrationState::Disabled;
    }

    const ShortcutBackendKind backend = selectedBackend();
    return registerWithBackend(backend, sequence, requireModifier, interactionPolicy);
}

ShortcutRegistrationState GlobalShortcutService::registerWithBackend(
    ShortcutBackendKind backend, const QKeySequence &sequence, bool requireModifier,
    ShortcutInteractionPolicy interactionPolicy) {
    switch (backend) {
        case ShortcutBackendKind::Windows:
            return registerWindowsShortcut(sequence, requireModifier);
        case ShortcutBackendKind::X11:
            return registerX11Shortcut(sequence, requireModifier);
        case ShortcutBackendKind::WaylandPortal: {
            const ShortcutRegistrationState portalState =
                registerWaylandPortalShortcut(sequence, requireModifier,
                                              interactionPolicy);
            if (portalState == ShortcutRegistrationState::Registered ||
                portalState == ShortcutRegistrationState::InvalidBinding ||
                portalState == ShortcutRegistrationState::Failed ||
                isBackendForcedByEnv()) {
                return portalState;
            }

            const QString portalError = m_lastError;
            const ShortcutRegistrationState wlrootsState =
                registerWaylandWlrootsShortcut(sequence, requireModifier,
                                               interactionPolicy);
            if (wlrootsState == ShortcutRegistrationState::Registered) {
                return wlrootsState;
            }
            if (wlrootsState == ShortcutRegistrationState::Unavailable &&
                !portalError.trimmed().isEmpty() && !m_lastError.trimmed().isEmpty()) {
                m_lastError = QStringLiteral("Portal: %1 | wlroots: %2")
                                  .arg(portalError, m_lastError);
            }
            return wlrootsState;
        }
        case ShortcutBackendKind::WaylandWlroots:
            return registerWaylandWlrootsShortcut(sequence, requireModifier,
                                                  interactionPolicy);
        case ShortcutBackendKind::Disabled:
            m_lastError = backendUnavailableText(backend);
            return ShortcutRegistrationState::Unavailable;
        case ShortcutBackendKind::Auto:
        default:
            m_lastError = QStringLiteral("No shortcut backend available");
            return ShortcutRegistrationState::Unavailable;
    }
}

void GlobalShortcutService::unregisterShortcut() {
#ifdef Q_OS_WIN
    unregisterWindowsShortcut();
#endif
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    unregisterX11Shortcut();
#endif
    unregisterWaylandPortalShortcut();

    if (m_filterInstalled) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
        m_filterInstalled = false;
    }
}

QString GlobalShortcutService::lastError() const {
    return m_lastError;
}

bool GlobalShortcutService::isSupported() const {
    const ShortcutBackendKind backend = selectedBackend();
    switch (backend) {
#ifdef Q_OS_WIN
        case ShortcutBackendKind::Windows:
            return true;
#endif
        case ShortcutBackendKind::X11:
#if defined(PASTETRY_HAVE_X11)
            return isX11Platform();
#else
            return false;
#endif
        case ShortcutBackendKind::WaylandPortal:
#if defined(PASTETRY_HAVE_DBUS)
            return isWaylandPlatform() && QDBusConnection::sessionBus().isConnected();
#else
            return false;
#endif
        case ShortcutBackendKind::WaylandWlroots:
            return isWaylandPlatform();
        case ShortcutBackendKind::Disabled:
        case ShortcutBackendKind::Auto:
        default:
            return false;
    }
}

QString GlobalShortcutService::backendUnavailableText(ShortcutBackendKind backend) const {
    switch (backend) {
        case ShortcutBackendKind::Windows:
            return QStringLiteral("Windows shortcut backend unavailable");
        case ShortcutBackendKind::X11:
            return QStringLiteral("X11 shortcut backend unavailable");
        case ShortcutBackendKind::WaylandPortal:
            return QStringLiteral("Wayland portal shortcut backend unavailable");
        case ShortcutBackendKind::WaylandWlroots:
            return QStringLiteral("Wayland wlroots shortcut backend unavailable");
        case ShortcutBackendKind::Disabled:
            return QStringLiteral("Global shortcuts disabled by backend selection");
        case ShortcutBackendKind::Auto:
        default:
            return QStringLiteral("No shortcut backend available");
    }
}

bool GlobalShortcutService::nativeEventFilter(const QByteArray &eventType, void *message,
                                              qintptr *result) {
    Q_UNUSED(result);

#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (m_windowsRegistered && msg->message == WM_HOTKEY &&
            static_cast<int>(msg->wParam) == m_hotkeyId) {
            emit activated();
            return true;
        }
    }
#endif

#if defined(PASTETRY_HAVE_X11)
    if (eventType == "xcb_generic_event_t" && m_x11Registered) {
        auto *event = static_cast<xcb_generic_event_t *>(message);
        if (!event) {
            return false;
        }

        const uint8_t responseType = event->response_type & 0x7f;
        if (responseType != XCB_KEY_PRESS) {
            return false;
        }

        auto *keyEvent = reinterpret_cast<xcb_key_press_event_t *>(event);
        if (keyEvent->detail != m_x11Keycode) {
            return false;
        }

        const unsigned int pointerMasks =
            Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask;
        const unsigned int ignoredMasks = LockMask | m_x11IgnoredMask | pointerMasks;
        const unsigned int normalized = keyEvent->state & ~ignoredMasks;
        if (normalized != m_x11Modifiers) {
            return false;
        }

        emit activated();
        return true;
    }
#endif

    return false;
}

ShortcutRegistrationState GlobalShortcutService::registerWindowsShortcut(
    const QKeySequence &sequence, bool requireModifier) {
#ifdef Q_OS_WIN
    const int keySpec = keyFromSequence(sequence);
    if (keySpec == 0) {
        m_lastError = QStringLiteral("Shortcut is empty");
        return ShortcutRegistrationState::InvalidBinding;
    }

    const Qt::KeyboardModifiers mods =
        Qt::KeyboardModifiers(keySpec & Qt::KeyboardModifierMask);
    const int qtKey = keySpec & ~Qt::KeyboardModifierMask;

    UINT vk = 0;
    if (!qtKeyToWindowsVk(qtKey, &vk)) {
        m_lastError = QStringLiteral("Unsupported key in shortcut");
        return ShortcutRegistrationState::InvalidBinding;
    }

    const UINT nativeMods = qtModsToWindows(mods);
    if (requireModifier && nativeMods == 0) {
        m_lastError = QStringLiteral("Shortcut requires at least one modifier");
        return ShortcutRegistrationState::InvalidBinding;
    }

    if (!RegisterHotKey(nullptr, m_hotkeyId, nativeMods, vk)) {
        m_lastError = QStringLiteral("RegisterHotKey failed");
        return ShortcutRegistrationState::Failed;
    }

    m_windowsRegistered = true;

    if (!m_filterInstalled) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }

    qCInfo(logShortcut) << "Registered Windows global shortcut"
                        << sequence.toString();
    return ShortcutRegistrationState::Registered;
#else
    Q_UNUSED(sequence);
    Q_UNUSED(requireModifier);
    m_lastError = QStringLiteral("Windows shortcut backend unavailable");
    return ShortcutRegistrationState::Unavailable;
#endif
}

ShortcutRegistrationState GlobalShortcutService::registerX11Shortcut(
    const QKeySequence &sequence, bool requireModifier) {
#if defined(PASTETRY_HAVE_X11)
    if (!isX11Platform()) {
        m_lastError = QStringLiteral("Global shortcut unavailable outside X11 session");
        return ShortcutRegistrationState::Unavailable;
    }

    auto *app = qGuiApp;
    auto *x11 =
        app ? app->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    if (!x11 || !x11->display()) {
        m_lastError = QStringLiteral("X11 native interface unavailable");
        return ShortcutRegistrationState::Unavailable;
    }

    const int keySpec = keyFromSequence(sequence);
    if (keySpec == 0) {
        m_lastError = QStringLiteral("Shortcut is empty");
        return ShortcutRegistrationState::InvalidBinding;
    }

    const Qt::KeyboardModifiers mods =
        Qt::KeyboardModifiers(keySpec & Qt::KeyboardModifierMask);
    const int qtKey = keySpec & ~Qt::KeyboardModifierMask;

    KeySym keySym = NoSymbol;
    if (!qtKeyToX11KeySym(qtKey, &keySym)) {
        m_lastError = QStringLiteral("Unsupported key in shortcut");
        return ShortcutRegistrationState::InvalidBinding;
    }

    Display *display = x11->display();
    const KeyCode keyCode = XKeysymToKeycode(display, keySym);
    if (keyCode == 0) {
        m_lastError = QStringLiteral("Failed to resolve X11 keycode");
        return ShortcutRegistrationState::Failed;
    }

    const unsigned int nativeMods = qtModsToX11(mods);
    if (requireModifier && nativeMods == 0) {
        m_lastError = QStringLiteral("Shortcut requires at least one modifier");
        return ShortcutRegistrationState::InvalidBinding;
    }

    const Window root = DefaultRootWindow(display);
    m_x11IgnoredMask = detectIgnoredX11Mask(display);
    m_x11GrabVariants = buildGrabVariants(LockMask | m_x11IgnoredMask);

    int grabErrorCode = 0;
    g_x11GrabErrorCode = &grabErrorCode;
    int (*oldErrorHandler)(Display *, XErrorEvent *) =
        XSetErrorHandler(x11GrabErrorHandler);

    for (const unsigned int variantMask : m_x11GrabVariants) {
        XGrabKey(display, keyCode, nativeMods | variantMask, root, False,
                 GrabModeAsync, GrabModeAsync);
    }
    XSync(display, False);
    XSetErrorHandler(oldErrorHandler);
    g_x11GrabErrorCode = nullptr;

    if (grabErrorCode != 0) {
        for (const unsigned int variantMask : m_x11GrabVariants) {
            XUngrabKey(display, static_cast<int>(keyCode), nativeMods | variantMask,
                       root);
        }
        XSync(display, False);
        m_x11GrabVariants.clear();
        m_x11IgnoredMask = 0;
        m_lastError = QStringLiteral("X11 refused shortcut (%1)")
                          .arg(x11ErrorCodeText(display, grabErrorCode));
        return ShortcutRegistrationState::Failed;
    }

    m_x11Registered = true;
    m_x11Keycode = keyCode;
    m_x11Modifiers = nativeMods;

    if (!m_filterInstalled) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }

    qCInfo(logShortcut) << "Registered X11 global shortcut" << sequence.toString();
    return ShortcutRegistrationState::Registered;
#else
    Q_UNUSED(sequence);
    Q_UNUSED(requireModifier);
    m_lastError = QStringLiteral("X11 shortcut backend unavailable");
    return ShortcutRegistrationState::Unavailable;
#endif
}

ShortcutRegistrationState GlobalShortcutService::registerWaylandPortalShortcut(
    const QKeySequence &sequence, bool requireModifier,
    ShortcutInteractionPolicy interactionPolicy) {
#if defined(PASTETRY_HAVE_DBUS)
    if (!isWaylandPlatform()) {
        m_lastError = QStringLiteral("Wayland portal backend unavailable outside Wayland session");
        return ShortcutRegistrationState::Unavailable;
    }

    if (interactionPolicy == ShortcutInteractionPolicy::NonInteractive) {
        m_lastError =
            QStringLiteral("Wayland shortcut registration deferred until Settings Apply");
        return ShortcutRegistrationState::Unavailable;
    }

    QString triggerError;
    const QString trigger = qtSequenceToPortalTrigger(sequence, requireModifier,
                                                      &triggerError);
    if (trigger.isEmpty()) {
        m_lastError = triggerError.isEmpty()
                          ? QStringLiteral("Unsupported key in shortcut")
                          : triggerError;
        return ShortcutRegistrationState::InvalidBinding;
    }

    if (!createPortalShortcutSession(&m_lastError)) {
        return ShortcutRegistrationState::Unavailable;
    }

    if (!bindPortalShortcut(trigger, &m_lastError)) {
        closePortalShortcutSession();
        return ShortcutRegistrationState::Failed;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"),
                     QStringLiteral("/org/freedesktop/portal/desktop"),
                     QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
                     QStringLiteral("Activated"), this,
                     SLOT(onPortalShortcutActivated(QDBusMessage)))) {
        m_lastError = QStringLiteral("Failed to subscribe to portal activation signal");
        closePortalShortcutSession();
        return ShortcutRegistrationState::Failed;
    }

    m_portalRegistered = true;

    qCInfo(logShortcut) << "Registered Wayland portal shortcut" << sequence.toString()
                        << "backend=" << backendKindName(selectedBackend());
    return ShortcutRegistrationState::Registered;
#else
    Q_UNUSED(sequence);
    Q_UNUSED(requireModifier);
    Q_UNUSED(interactionPolicy);
    m_lastError = QStringLiteral("Wayland portal backend unavailable in this build");
    return ShortcutRegistrationState::Unavailable;
#endif
}

ShortcutRegistrationState GlobalShortcutService::registerWaylandWlrootsShortcut(
    const QKeySequence &sequence, bool requireModifier,
    ShortcutInteractionPolicy interactionPolicy) {
    Q_UNUSED(sequence);
    Q_UNUSED(requireModifier);
    Q_UNUSED(interactionPolicy);

    if (!isWaylandPlatform()) {
        m_lastError = QStringLiteral("Wayland wlroots backend unavailable outside Wayland session");
        return ShortcutRegistrationState::Unavailable;
    }

#if defined(PASTETRY_HAVE_QT_WAYLAND_CLIENT)
    m_lastError = QStringLiteral(
        "Wayland wlroots backend is not enabled in this build variant");
#else
    m_lastError = QStringLiteral(
        "Wayland wlroots backend requires Qt Wayland client support");
#endif
    return ShortcutRegistrationState::Unavailable;
}

void GlobalShortcutService::unregisterWindowsShortcut() {
#ifdef Q_OS_WIN
    if (!m_windowsRegistered) {
        return;
    }
    UnregisterHotKey(nullptr, m_hotkeyId);
    m_windowsRegistered = false;
#endif
}

void GlobalShortcutService::unregisterX11Shortcut() {
#if defined(PASTETRY_HAVE_X11)
    if (!m_x11Registered) {
        return;
    }

    auto *app = qGuiApp;
    auto *x11 =
        app ? app->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    if (x11 && x11->display()) {
        Display *display = x11->display();
        const Window root = DefaultRootWindow(display);

        for (const unsigned int lockMask : m_x11GrabVariants) {
            XUngrabKey(display, static_cast<int>(m_x11Keycode),
                       m_x11Modifiers | lockMask, root);
        }
        XSync(display, False);
    }

    m_x11Registered = false;
    m_x11Modifiers = 0;
    m_x11Keycode = 0;
    m_x11IgnoredMask = 0;
    m_x11GrabVariants.clear();
#endif
}

void GlobalShortcutService::unregisterWaylandPortalShortcut() {
#if defined(PASTETRY_HAVE_DBUS)
    if (!m_portalRegistered) {
        closePortalShortcutSession();
        return;
    }

    QDBusConnection::sessionBus().disconnect(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
        QStringLiteral("Activated"), this,
        SLOT(onPortalShortcutActivated(QDBusMessage)));

    closePortalShortcutSession();
    m_portalRegistered = false;
#endif
}

#if defined(PASTETRY_HAVE_DBUS)
void GlobalShortcutService::onPortalRequestResponse(const QDBusMessage &message) {
    if (!m_portalRequestPath.isEmpty() && message.path() != m_portalRequestPath) {
        return;
    }

    const QList<QVariant> args = message.arguments();
    if (args.isEmpty()) {
        return;
    }

    m_portalRequestCode = args.at(0).toUInt();
    if (args.size() >= 2) {
        m_portalRequestResults = dbusMapFromVariant(args.at(1));
    } else {
        m_portalRequestResults.clear();
    }
    m_portalRequestCompleted = true;
}

void GlobalShortcutService::onPortalShortcutActivated(const QDBusMessage &message) {
    const QList<QVariant> args = message.arguments();
    if (args.size() < 2) {
        return;
    }

    const QString sessionPath = dbusPathFromVariant(args.at(0));
    const QString shortcutId = args.at(1).toString().trimmed();
    if (sessionPath.isEmpty() || shortcutId.isEmpty()) {
        return;
    }

    if (sessionPath != m_portalSessionPath || shortcutId != m_portalShortcutId) {
        return;
    }

    emit activated();
}

bool GlobalShortcutService::waitForPortalRequest(const QString &requestPath, int timeoutMs,
                                                 QVariantMap *results,
                                                 QString *error) {
    if (requestPath.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Portal request handle path is empty");
        }
        return false;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (error) {
            *error = QStringLiteral("D-Bus session bus unavailable");
        }
        return false;
    }

    m_portalRequestPath = requestPath;
    m_portalRequestCompleted = false;
    m_portalRequestCode = 2;
    m_portalRequestResults.clear();

    const bool connected =
        bus.connect(QStringLiteral("org.freedesktop.portal.Desktop"), requestPath,
                    QStringLiteral("org.freedesktop.portal.Request"),
                    QStringLiteral("Response"), this,
                    SLOT(onPortalRequestResponse(QDBusMessage)));
    if (!connected) {
        if (error) {
            *error = QStringLiteral("Failed to subscribe to portal request response");
        }
        return false;
    }

    QEventLoop loop;
    QTimer poll;
    QTimer timeout;
    poll.setInterval(15);
    timeout.setSingleShot(true);

    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (m_portalRequestCompleted) {
            loop.quit();
        }
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    poll.start();
    timeout.start(qMax(500, timeoutMs));
    loop.exec();
    poll.stop();

    bus.disconnect(QStringLiteral("org.freedesktop.portal.Desktop"), requestPath,
                   QStringLiteral("org.freedesktop.portal.Request"),
                   QStringLiteral("Response"), this,
                   SLOT(onPortalRequestResponse(QDBusMessage)));

    if (!m_portalRequestCompleted) {
        if (error) {
            *error = QStringLiteral("Portal request timed out");
        }
        return false;
    }

    if (m_portalRequestCode != 0) {
        if (error) {
            *error = QStringLiteral("Portal request rejected (code %1)")
                         .arg(m_portalRequestCode);
        }
        return false;
    }

    if (results) {
        *results = m_portalRequestResults;
    }
    return true;
}

bool GlobalShortcutService::createPortalShortcutSession(QString *error) {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        if (error) {
            *error = QStringLiteral("D-Bus session bus unavailable");
        }
        return false;
    }

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), portalToken(QStringLiteral("create")));
    options.insert(QStringLiteral("session_handle_token"),
                   portalToken(QStringLiteral("session")));

    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
        QStringLiteral("CreateSession"));
    msg << options;

    QDBusReply<QDBusObjectPath> reply = bus.call(msg, QDBus::Block, 5000);
    if (!reply.isValid()) {
        if (error) {
            *error = QStringLiteral("CreateSession failed: %1")
                         .arg(reply.error().message());
        }
        return false;
    }

    QVariantMap results;
    if (!waitForPortalRequest(reply.value().path(), 8000, &results, error)) {
        return false;
    }

    const QString sessionPath = dbusPathFromVariant(results.value(QStringLiteral("session_handle")));
    if (sessionPath.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Portal session handle missing in response");
        }
        return false;
    }

    m_portalSessionPath = sessionPath;
    return true;
}

bool GlobalShortcutService::bindPortalShortcut(const QString &trigger, QString *error) {
    if (m_portalSessionPath.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Portal session unavailable");
        }
        return false;
    }

    ensurePortalDbusTypesRegistered();

    m_portalShortcutId = portalToken(QStringLiteral("shortcut"));

    QVariantMap shortcutOptions;
    shortcutOptions.insert(QStringLiteral("description"),
                           QStringLiteral("Pastetry shortcut"));
    shortcutOptions.insert(QStringLiteral("preferred_trigger"), trigger);

    PortalShortcutBindingList shortcuts;
    shortcuts.push_back(PortalShortcutBinding{m_portalShortcutId, shortcutOptions});

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), portalToken(QStringLiteral("bind")));

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
        QStringLiteral("BindShortcuts"));
    msg << QDBusObjectPath(m_portalSessionPath)
        << QVariant::fromValue(shortcuts)
        << QString()
        << options;

    QDBusReply<QDBusObjectPath> reply = bus.call(msg, QDBus::Block, 5000);
    if (!reply.isValid()) {
        if (error) {
            *error = QStringLiteral("BindShortcuts failed: %1")
                         .arg(reply.error().message());
        }
        return false;
    }

    QVariantMap results;
    if (!waitForPortalRequest(reply.value().path(), 8000, &results, error)) {
        return false;
    }

    Q_UNUSED(results);
    return true;
}

void GlobalShortcutService::closePortalShortcutSession() {
    if (m_portalSessionPath.trimmed().isEmpty()) {
        m_portalShortcutId.clear();
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        QDBusMessage closeMsg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_portalSessionPath,
            QStringLiteral("org.freedesktop.portal.Session"),
            QStringLiteral("Close"));
        bus.call(closeMsg, QDBus::NoBlock);
    }

    m_portalSessionPath.clear();
    m_portalShortcutId.clear();
    m_portalRegistered = false;
}

QString GlobalShortcutService::qtSequenceToPortalTrigger(
    const QKeySequence &sequence, bool requireModifier, QString *error) const {
    const int keySpec = keyFromSequence(sequence);
    if (keySpec == 0) {
        if (error) {
            *error = QStringLiteral("Shortcut is empty");
        }
        return QString();
    }

    const Qt::KeyboardModifiers modifiers =
        Qt::KeyboardModifiers(keySpec & Qt::KeyboardModifierMask);
    const int qtKey = keySpec & ~Qt::KeyboardModifierMask;

    auto keyName = [qtKey]() -> QString {
        if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
            return QString(QChar('A' + (qtKey - Qt::Key_A)));
        }
        if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
            return QString(QChar('0' + (qtKey - Qt::Key_0)));
        }
        if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
            return QStringLiteral("F%1").arg(qtKey - Qt::Key_F1 + 1);
        }

        switch (qtKey) {
            case Qt::Key_Space:
                return QStringLiteral("space");
            case Qt::Key_Tab:
                return QStringLiteral("Tab");
            case Qt::Key_Return:
            case Qt::Key_Enter:
                return QStringLiteral("Return");
            case Qt::Key_Escape:
                return QStringLiteral("Escape");
            case Qt::Key_Left:
                return QStringLiteral("Left");
            case Qt::Key_Right:
                return QStringLiteral("Right");
            case Qt::Key_Up:
                return QStringLiteral("Up");
            case Qt::Key_Down:
                return QStringLiteral("Down");
            case Qt::Key_Home:
                return QStringLiteral("Home");
            case Qt::Key_End:
                return QStringLiteral("End");
            case Qt::Key_PageUp:
                return QStringLiteral("Page_Up");
            case Qt::Key_PageDown:
                return QStringLiteral("Page_Down");
            case Qt::Key_Insert:
                return QStringLiteral("Insert");
            case Qt::Key_Delete:
                return QStringLiteral("Delete");
            case Qt::Key_BracketLeft:
                return QStringLiteral("bracketleft");
            case Qt::Key_BracketRight:
                return QStringLiteral("bracketright");
            case Qt::Key_Backslash:
                return QStringLiteral("backslash");
            case Qt::Key_Slash:
                return QStringLiteral("slash");
            case Qt::Key_Minus:
                return QStringLiteral("minus");
            case Qt::Key_Equal:
                return QStringLiteral("equal");
            case Qt::Key_Comma:
                return QStringLiteral("comma");
            case Qt::Key_Period:
                return QStringLiteral("period");
            case Qt::Key_Semicolon:
                return QStringLiteral("semicolon");
            case Qt::Key_Apostrophe:
                return QStringLiteral("apostrophe");
            case Qt::Key_QuoteLeft:
                return QStringLiteral("grave");
            default:
                return QString();
        }
    };

    const QString key = keyName();
    if (key.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Unsupported key in shortcut");
        }
        return QString();
    }

    QStringList parts;
    if (modifiers & Qt::ControlModifier) {
        parts.push_back(QStringLiteral("<Ctrl>"));
    }
    if (modifiers & Qt::AltModifier) {
        parts.push_back(QStringLiteral("<Alt>"));
    }
    if (modifiers & Qt::ShiftModifier) {
        parts.push_back(QStringLiteral("<Shift>"));
    }
    if (modifiers & Qt::MetaModifier) {
        parts.push_back(QStringLiteral("<Super>"));
    }

    if (requireModifier && parts.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Shortcut requires at least one modifier");
        }
        return QString();
    }

    QString trigger = parts.join(QString()) + key;
    if (trigger.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("Unsupported shortcut");
        }
        return QString();
    }

    return trigger;
}
#endif

}  // namespace pastetry
