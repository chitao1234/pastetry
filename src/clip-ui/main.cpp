#include "clip-ui/app_controller.h"

#include "common/app_paths.h"
#include "common/logging.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("pastetry");
    QCoreApplication::setOrganizationName("pastetry");

    QCommandLineParser parser;
    parser.setApplicationDescription("Pastetry clipboard manager UI");
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

    QApplication::setQuitOnLastWindowClosed(false);

    QString logError;
    if (!pastetry::logging::initialize(QStringLiteral("pastetry-clip-ui"), paths.dataDir,
                                       &logError)) {
        QMessageBox::critical(nullptr, QStringLiteral("Pastetry startup failed"),
                              QStringLiteral("Failed to initialize logging: %1")
                                  .arg(logError));
        return 1;
    }

    pastetry::AppController controller(paths);
    QString error;
    const bool shouldRun = controller.initialize(&error);
    if (!shouldRun) {
        if (!error.isEmpty()) {
            QMessageBox::critical(nullptr, QStringLiteral("Pastetry startup failed"), error);
            pastetry::logging::shutdown();
            return 1;
        }
        pastetry::logging::shutdown();
        return 0;
    }

    const int exitCode = QApplication::exec();
    pastetry::logging::shutdown();
    return exitCode;
}
