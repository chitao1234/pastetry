#include "clipd/clipboard_daemon.h"
#include "clipd/wayland_clipboard_runtime.h"

#include "common/app_paths.h"
#include "common/logging.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logClipdMain, "pastetry.clipd.main")

namespace {

pastetry::WaylandEnvDecision evaluateStartupWaylandEnv() {
    return pastetry::evaluateWaylandDataControlEnv(
        qgetenv("WAYLAND_DISPLAY"), qgetenv("XDG_SESSION_TYPE"),
        qEnvironmentVariableIsSet("QT_WAYLAND_USE_DATA_CONTROL"),
        qgetenv("QT_WAYLAND_USE_DATA_CONTROL"));
}

}  // namespace

int main(int argc, char **argv) {
    const pastetry::WaylandEnvDecision envDecision = evaluateStartupWaylandEnv();
    bool autoEnabledDataControl = false;
    if (envDecision.shouldSetDataControlEnv) {
        autoEnabledDataControl = qputenv("QT_WAYLAND_USE_DATA_CONTROL", "1");
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("pastetry-clipd");
    QCoreApplication::setOrganizationName("pastetry");

    QCommandLineParser parser;
    parser.setApplicationDescription("Pastetry clipboard daemon");
    parser.addHelpOption();

    QCommandLineOption dataDirOpt(QStringList{"d", "data-dir"},
                                  "Override data directory", "dir");
    QCommandLineOption socketOpt(QStringList{"s", "socket"}, "Override socket name",
                                 "socket");
    parser.addOption(dataDirOpt);
    parser.addOption(socketOpt);
    parser.process(app);

    const auto paths = pastetry::resolveAppPaths(parser.value(dataDirOpt),
                                                 parser.value(socketOpt));

    QString logError;
    if (!pastetry::logging::initialize(QStringLiteral("pastetry-clipd"), paths.dataDir,
                                       &logError)) {
        qCritical().noquote() << "Failed to initialize logging:" << logError;
        return 1;
    }
    if (autoEnabledDataControl) {
        qCInfo(logClipdMain)
            << "Auto-enabled QT_WAYLAND_USE_DATA_CONTROL=1 for Wayland clipd startup";
    } else if (envDecision.shouldSetDataControlEnv) {
        qCWarning(logClipdMain)
            << "Failed to auto-enable QT_WAYLAND_USE_DATA_CONTROL for Wayland clipd startup";
    }

    pastetry::ClipboardDaemon daemon(paths);

    QString error;
    if (!daemon.start(&error)) {
        qCritical().noquote() << "Failed to start daemon:" << error;
        pastetry::logging::shutdown();
        return 1;
    }

    const int exitCode = app.exec();
    pastetry::logging::shutdown();
    return exitCode;
}
