#include "clipd/wayland_clipboard_runtime.h"

#include <QtTest/QtTest>

using namespace pastetry;

class WaylandClipboardRuntimeTests : public QObject {
    Q_OBJECT

private slots:
    void envDecisionAutoEnableWhenWaylandAndUnset();
    void envDecisionRespectsExplicitDisable();
    void envDecisionRespectsExplicitEnable();
    void envDecisionNoWaylandNoAutoEnable();

    void backendSelectionRobustWithZwlrManager();
    void backendSelectionRobustWithExtManager();
    void backendSelectionDegradedWhenDataControlDisabled();
    void backendSelectionDegradedWhenProbeUnavailable();
    void backendSelectionDegradedWithoutDataControlManagers();
    void backendSelectionNotWayland();
};

void WaylandClipboardRuntimeTests::envDecisionAutoEnableWhenWaylandAndUnset() {
    const WaylandEnvDecision decision = evaluateWaylandDataControlEnv(
        QByteArray("wayland-0"), QByteArray(), false, QByteArray());

    QVERIFY(decision.waylandSession);
    QVERIFY(!decision.dataControlEnvWasSet);
    QVERIFY(!decision.dataControlRequested);
    QVERIFY(decision.shouldSetDataControlEnv);
}

void WaylandClipboardRuntimeTests::envDecisionRespectsExplicitDisable() {
    const WaylandEnvDecision decision = evaluateWaylandDataControlEnv(
        QByteArray(), QByteArray("wayland"), true, QByteArray("0"));

    QVERIFY(decision.waylandSession);
    QVERIFY(decision.dataControlEnvWasSet);
    QVERIFY(!decision.dataControlRequested);
    QVERIFY(!decision.shouldSetDataControlEnv);
}

void WaylandClipboardRuntimeTests::envDecisionRespectsExplicitEnable() {
    const WaylandEnvDecision decision = evaluateWaylandDataControlEnv(
        QByteArray(), QByteArray("Wayland"), true, QByteArray("1"));

    QVERIFY(decision.waylandSession);
    QVERIFY(decision.dataControlEnvWasSet);
    QVERIFY(decision.dataControlRequested);
    QVERIFY(!decision.shouldSetDataControlEnv);
}

void WaylandClipboardRuntimeTests::envDecisionNoWaylandNoAutoEnable() {
    const WaylandEnvDecision decision = evaluateWaylandDataControlEnv(
        QByteArray(), QByteArray("x11"), false, QByteArray());

    QVERIFY(!decision.waylandSession);
    QVERIFY(!decision.dataControlEnvWasSet);
    QVERIFY(!decision.dataControlRequested);
    QVERIFY(!decision.shouldSetDataControlEnv);
}

void WaylandClipboardRuntimeTests::backendSelectionRobustWithZwlrManager() {
    WaylandClipboardProbeResult probe;
    probe.availability = WaylandProbeAvailability::Supported;
    probe.hasZwlrDataControlManager = true;

    const WaylandBackendSelection selection = selectWaylandClipboardBackend(
        {true, true, probe}, 1300, 500);

    QCOMPARE(selection.mode, WaylandClipboardMode::NativeRobust);
    QCOMPARE(selection.pollIntervalMs, 1300);
    QVERIFY(selection.robust);
}

void WaylandClipboardRuntimeTests::backendSelectionRobustWithExtManager() {
    WaylandClipboardProbeResult probe;
    probe.availability = WaylandProbeAvailability::Supported;
    probe.hasExtDataControlManager = true;

    const WaylandBackendSelection selection = selectWaylandClipboardBackend(
        {true, true, probe}, 1200, 450);

    QCOMPARE(selection.mode, WaylandClipboardMode::NativeRobust);
    QCOMPARE(selection.pollIntervalMs, 1200);
    QVERIFY(selection.robust);
}

void WaylandClipboardRuntimeTests::backendSelectionDegradedWhenDataControlDisabled() {
    WaylandClipboardProbeResult probe;
    probe.availability = WaylandProbeAvailability::Supported;
    probe.hasZwlrDataControlManager = true;

    const WaylandBackendSelection selection = selectWaylandClipboardBackend(
        {true, false, probe}, 1200, 450);

    QCOMPARE(selection.mode, WaylandClipboardMode::DegradedQt);
    QCOMPARE(selection.pollIntervalMs, 450);
    QVERIFY(!selection.robust);
}

void WaylandClipboardRuntimeTests::backendSelectionDegradedWhenProbeUnavailable() {
    WaylandClipboardProbeResult probe;
    probe.availability = WaylandProbeAvailability::Unsupported;

    const WaylandBackendSelection selection = selectWaylandClipboardBackend(
        {true, true, probe}, 1200, 450);

    QCOMPARE(selection.mode, WaylandClipboardMode::DegradedQt);
    QCOMPARE(selection.pollIntervalMs, 450);
    QVERIFY(!selection.robust);
}

void WaylandClipboardRuntimeTests::backendSelectionDegradedWithoutDataControlManagers() {
    WaylandClipboardProbeResult probe;
    probe.availability = WaylandProbeAvailability::Supported;

    const WaylandBackendSelection selection = selectWaylandClipboardBackend(
        {true, true, probe}, 1200, 450);

    QCOMPARE(selection.mode, WaylandClipboardMode::DegradedQt);
    QCOMPARE(selection.pollIntervalMs, 450);
    QVERIFY(!selection.robust);
}

void WaylandClipboardRuntimeTests::backendSelectionNotWayland() {
    WaylandClipboardProbeResult probe;
    probe.availability = WaylandProbeAvailability::Supported;
    probe.hasZwlrDataControlManager = true;

    const WaylandBackendSelection selection = selectWaylandClipboardBackend(
        {false, true, probe}, 1200, 450);

    QCOMPARE(selection.mode, WaylandClipboardMode::NotWayland);
    QCOMPARE(selection.pollIntervalMs, 0);
    QVERIFY(!selection.robust);
}

QTEST_MAIN(WaylandClipboardRuntimeTests)

#include "wayland_clipboard_runtime_tests.moc"

