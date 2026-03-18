#include "clip-ui/main_window.h"

#include "common/app_paths.h"
#include "common/ipc_client.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>

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

    pastetry::IpcClient client(paths.socketName);
    pastetry::MainWindow window(std::move(client));
    window.show();

    return app.exec();
}
