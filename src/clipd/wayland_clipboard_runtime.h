#pragma once

#include <QByteArray>
#include <QString>

namespace pastetry {

constexpr int kWaylandRobustPollIntervalMs = 1200;
constexpr int kWaylandDegradedPollIntervalMs = 450;

enum class WaylandClipboardMode {
    NotWayland,
    NativeRobust,
    DegradedQt,
};

enum class WaylandProbeAvailability {
    Supported,
    Unsupported,
};

struct WaylandClipboardProbeResult {
    WaylandProbeAvailability availability = WaylandProbeAvailability::Unsupported;
    bool hasZwlrDataControlManager = false;
    bool hasExtDataControlManager = false;
    QString error;
};

struct WaylandEnvDecision {
    bool waylandSession = false;
    bool dataControlEnvWasSet = false;
    bool dataControlRequested = false;
    bool shouldSetDataControlEnv = false;
};

bool parseTruthyEnvFlag(const QByteArray &value);
WaylandEnvDecision evaluateWaylandDataControlEnv(const QByteArray &waylandDisplay,
                                                 const QByteArray &xdgSessionType,
                                                 bool qtWaylandUseDataControlSet,
                                                 const QByteArray &qtWaylandUseDataControl);

struct WaylandBackendSelectionInput {
    bool waylandSession = false;
    bool dataControlRequested = false;
    WaylandClipboardProbeResult probe;
};

struct WaylandBackendSelection {
    WaylandClipboardMode mode = WaylandClipboardMode::NotWayland;
    int pollIntervalMs = 0;
    bool robust = false;
    QString reason;
};

WaylandBackendSelection selectWaylandClipboardBackend(
    const WaylandBackendSelectionInput &input,
    int robustPollIntervalMs = kWaylandRobustPollIntervalMs,
    int degradedPollIntervalMs = kWaylandDegradedPollIntervalMs);

QString waylandProbeAvailabilityToString(WaylandProbeAvailability availability);
QString waylandClipboardModeToString(WaylandClipboardMode mode);

}  // namespace pastetry

