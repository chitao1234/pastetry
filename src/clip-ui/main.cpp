#include "clip-ui/app_controller.h"

#include "common/app_paths.h"

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

    pastetry::AppController controller(paths);
    QString error;
    const bool shouldRun = controller.initialize(&error);
    if (!shouldRun) {
        if (!error.isEmpty()) {
            QMessageBox::critical(nullptr, QStringLiteral("Pastetry startup failed"), error);
            return 1;
        }
        return 0;
    }

    return QApplication::exec();
}
