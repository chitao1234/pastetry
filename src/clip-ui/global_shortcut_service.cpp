#include "clip-ui/global_shortcut_service.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QtCore/qnativeinterface.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#endif

Q_LOGGING_CATEGORY(logShortcut, "pastetry.shortcut")

namespace pastetry {

namespace {
int keyFromSequence(const QKeySequence &sequence) {
    return sequence.count() > 0 ? sequence[0].toCombined() : 0;
}

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

unsigned int detectNumLockMask(Display *display) {
    if (!display) {
        return 0;
    }

    unsigned int mask = 0;
    XModifierKeymap *modMap = XGetModifierMapping(display);
    if (!modMap) {
        return 0;
    }

    const KeyCode numLock = XKeysymToKeycode(display, XK_Num_Lock);
    for (int modIndex = 0; modIndex < 8; ++modIndex) {
        for (int keyIndex = 0; keyIndex < modMap->max_keypermod; ++keyIndex) {
            const int offset = modIndex * modMap->max_keypermod + keyIndex;
            if (modMap->modifiermap[offset] == numLock) {
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

}  // namespace

GlobalShortcutService::GlobalShortcutService(QObject *parent, int windowsHotkeyId)
    : QObject(parent), m_hotkeyId(windowsHotkeyId) {}

GlobalShortcutService::~GlobalShortcutService() {
    unregisterShortcut();
}

ShortcutRegistrationState GlobalShortcutService::registerShortcut(
    const QKeySequence &sequence) {
    m_lastError.clear();
    unregisterShortcut();

    if (sequence.isEmpty()) {
        return ShortcutRegistrationState::Disabled;
    }

#ifdef Q_OS_WIN
    return registerWindowsShortcut(sequence);
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    return registerX11Shortcut(sequence);
#else
    m_lastError = QStringLiteral("Global shortcuts are unsupported on this platform");
    return ShortcutRegistrationState::Unavailable;
#endif
}

void GlobalShortcutService::unregisterShortcut() {
#ifdef Q_OS_WIN
    unregisterWindowsShortcut();
#endif
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    unregisterX11Shortcut();
#endif

    if (m_filterInstalled) {
        QCoreApplication::instance()->removeNativeEventFilter(this);
        m_filterInstalled = false;
    }
}

QString GlobalShortcutService::lastError() const {
    return m_lastError;
}

bool GlobalShortcutService::isSupported() const {
#ifdef Q_OS_WIN
    return true;
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    return QGuiApplication::platformName().contains(QStringLiteral("xcb"));
#else
    return false;
#endif
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

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
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

        const unsigned int ignoredMasks = LockMask | m_x11NumLockMask;
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
    const QKeySequence &sequence) {
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
    if (nativeMods == 0) {
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

    qCInfo(logShortcut) << "Registered Windows global shortcut" << sequence.toString();
    return ShortcutRegistrationState::Registered;
#else
    Q_UNUSED(sequence);
    m_lastError = QStringLiteral("Windows shortcut backend unavailable");
    return ShortcutRegistrationState::Unavailable;
#endif
}

ShortcutRegistrationState GlobalShortcutService::registerX11Shortcut(
    const QKeySequence &sequence) {
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    if (!QGuiApplication::platformName().contains(QStringLiteral("xcb"))) {
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
    if (nativeMods == 0) {
        m_lastError = QStringLiteral("Shortcut requires at least one modifier");
        return ShortcutRegistrationState::InvalidBinding;
    }

    const Window root = DefaultRootWindow(display);
    m_x11NumLockMask = detectNumLockMask(display);

    int grabErrorCode = 0;
    g_x11GrabErrorCode = &grabErrorCode;
    int (*oldErrorHandler)(Display *, XErrorEvent *) =
        XSetErrorHandler(x11GrabErrorHandler);

    const unsigned int lockVariants[] = {0, LockMask, m_x11NumLockMask,
                                         LockMask | m_x11NumLockMask};
    for (const unsigned int lockMask : lockVariants) {
        XGrabKey(display, keyCode, nativeMods | lockMask, root, True, GrabModeAsync,
                 GrabModeAsync);
    }
    XSync(display, False);
    XSetErrorHandler(oldErrorHandler);
    g_x11GrabErrorCode = nullptr;

    if (grabErrorCode != 0) {
        for (const unsigned int lockMask : lockVariants) {
            XUngrabKey(display, static_cast<int>(keyCode), nativeMods | lockMask, root);
        }
        XSync(display, False);
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
    m_lastError = QStringLiteral("X11 shortcut backend unavailable");
    return ShortcutRegistrationState::Unavailable;
#endif
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
#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    if (!m_x11Registered) {
        return;
    }

    auto *app = qGuiApp;
    auto *x11 =
        app ? app->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    if (x11 && x11->display()) {
        Display *display = x11->display();
        const Window root = DefaultRootWindow(display);

        const unsigned int lockVariants[] = {0, LockMask, m_x11NumLockMask,
                                             LockMask | m_x11NumLockMask};
        for (const unsigned int lockMask : lockVariants) {
            XUngrabKey(display, static_cast<int>(m_x11Keycode),
                       m_x11Modifiers | lockMask, root);
        }
        XSync(display, False);
    }

    m_x11Registered = false;
    m_x11Modifiers = 0;
    m_x11Keycode = 0;
    m_x11NumLockMask = 0;
#endif
}

}  // namespace pastetry
