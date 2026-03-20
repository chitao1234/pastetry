#include "clipd/wayland_clipboard_runtime.h"

namespace pastetry {

bool parseTruthyEnvFlag(const QByteArray &value) {
    const QByteArray normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }

    if (normalized == "0" || normalized == "false" || normalized == "off" ||
        normalized == "no" || normalized == "n") {
        return false;
    }

    return true;
}

WaylandEnvDecision evaluateWaylandDataControlEnv(const QByteArray &waylandDisplay,
                                                 const QByteArray &xdgSessionType,
                                                 bool qtWaylandUseDataControlSet,
                                                 const QByteArray &qtWaylandUseDataControl) {
    WaylandEnvDecision decision;
    const bool hasWaylandDisplay = !waylandDisplay.trimmed().isEmpty();
    const bool xdgWayland = xdgSessionType.trimmed().toLower() == "wayland";

    decision.waylandSession = hasWaylandDisplay || xdgWayland;
    decision.dataControlEnvWasSet = qtWaylandUseDataControlSet;
    decision.dataControlRequested =
        qtWaylandUseDataControlSet && parseTruthyEnvFlag(qtWaylandUseDataControl);
    decision.shouldSetDataControlEnv = decision.waylandSession && !qtWaylandUseDataControlSet;
    return decision;
}

WaylandBackendSelection selectWaylandClipboardBackend(
    const WaylandBackendSelectionInput &input, int robustPollIntervalMs,
    int degradedPollIntervalMs) {
    WaylandBackendSelection selection;

    if (!input.waylandSession) {
        selection.mode = WaylandClipboardMode::NotWayland;
        selection.pollIntervalMs = 0;
        selection.robust = false;
        selection.reason = QStringLiteral("Non-Wayland session");
        return selection;
    }

    selection.mode = WaylandClipboardMode::DegradedQt;
    selection.pollIntervalMs = degradedPollIntervalMs;
    selection.robust = false;

    if (!input.dataControlRequested) {
        selection.reason =
            QStringLiteral("QT_WAYLAND_USE_DATA_CONTROL is disabled or unset");
        return selection;
    }

    if (input.probe.availability != WaylandProbeAvailability::Supported) {
        selection.reason =
            QStringLiteral("Native Wayland clipboard probe unavailable in this build");
        return selection;
    }

    if (!input.probe.hasZwlrDataControlManager &&
        !input.probe.hasExtDataControlManager) {
        selection.reason = QStringLiteral(
            "Compositor does not advertise zwlr/ext data-control manager");
        return selection;
    }

    selection.mode = WaylandClipboardMode::NativeRobust;
    selection.pollIntervalMs = robustPollIntervalMs;
    selection.robust = true;
    selection.reason = input.probe.hasZwlrDataControlManager
                           ? QStringLiteral("Using zwlr_data_control_manager_v1")
                           : QStringLiteral("Using ext_data_control_manager_v1");
    return selection;
}

QString waylandProbeAvailabilityToString(WaylandProbeAvailability availability) {
    switch (availability) {
        case WaylandProbeAvailability::Supported:
            return QStringLiteral("supported");
        case WaylandProbeAvailability::Unsupported:
            return QStringLiteral("unsupported");
    }
    return QStringLiteral("unknown");
}

QString waylandClipboardModeToString(WaylandClipboardMode mode) {
    switch (mode) {
        case WaylandClipboardMode::NotWayland:
            return QStringLiteral("not_wayland");
        case WaylandClipboardMode::NativeRobust:
            return QStringLiteral("native_robust");
        case WaylandClipboardMode::DegradedQt:
            return QStringLiteral("degraded_qt");
    }
    return QStringLiteral("unknown");
}

}  // namespace pastetry

