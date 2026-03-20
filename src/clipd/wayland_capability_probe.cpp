#include "clipd/wayland_capability_probe.h"

#include <QString>

#if defined(PASTETRY_HAVE_WAYLAND_CLIENT)
#include <wayland-client.h>

#include <cerrno>
#include <cstring>
#endif

namespace pastetry {

#if defined(PASTETRY_HAVE_WAYLAND_CLIENT)
namespace {

struct ProbeState {
    bool hasZwlrDataControlManager = false;
    bool hasExtDataControlManager = false;
};

void onRegistryGlobal(void *data, wl_registry *registry, uint32_t name,
                      const char *interfaceName, uint32_t version) {
    Q_UNUSED(registry);
    Q_UNUSED(name);
    Q_UNUSED(version);

    ProbeState *state = static_cast<ProbeState *>(data);
    const QString iface = QString::fromLatin1(interfaceName ? interfaceName : "");
    if (iface == QStringLiteral("zwlr_data_control_manager_v1")) {
        state->hasZwlrDataControlManager = true;
    } else if (iface == QStringLiteral("ext_data_control_manager_v1")) {
        state->hasExtDataControlManager = true;
    }
}

void onRegistryGlobalRemove(void *data, wl_registry *registry, uint32_t name) {
    Q_UNUSED(data);
    Q_UNUSED(registry);
    Q_UNUSED(name);
}

constexpr wl_registry_listener kRegistryListener = {
    onRegistryGlobal,
    onRegistryGlobalRemove,
};

}  // namespace
#endif

WaylandClipboardProbeResult probeWaylandClipboardCapabilities() {
    WaylandClipboardProbeResult result;

#if defined(PASTETRY_HAVE_WAYLAND_CLIENT)
    result.availability = WaylandProbeAvailability::Supported;

    wl_display *display = wl_display_connect(nullptr);
    if (!display) {
        result.error = QStringLiteral("wl_display_connect failed: %1")
                           .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    wl_registry *registry = wl_display_get_registry(display);
    if (!registry) {
        result.error = QStringLiteral("wl_display_get_registry returned null");
        wl_display_disconnect(display);
        return result;
    }

    ProbeState state;
    if (wl_registry_add_listener(registry, &kRegistryListener, &state) != 0) {
        result.error = QStringLiteral("wl_registry_add_listener failed");
        wl_registry_destroy(registry);
        wl_display_disconnect(display);
        return result;
    }

    if (wl_display_roundtrip(display) < 0) {
        result.error = QStringLiteral("wl_display_roundtrip failed: %1")
                           .arg(QString::fromLocal8Bit(std::strerror(errno)));
    }

    result.hasZwlrDataControlManager = state.hasZwlrDataControlManager;
    result.hasExtDataControlManager = state.hasExtDataControlManager;

    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return result;
#else
    result.availability = WaylandProbeAvailability::Unsupported;
    result.error = QStringLiteral("Built without wayland-client support");
    return result;
#endif
}

}  // namespace pastetry
