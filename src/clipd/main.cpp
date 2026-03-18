#include "clipd/clipboard_daemon.h"

#include "common/app_paths.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QLoggingCategory>

int main(int argc, char **argv) {
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

    pastetry::ClipboardDaemon daemon(paths);

    QString error;
    if (!daemon.start(&error)) {
        qCritical().noquote() << "Failed to start daemon:" << error;
        return 1;
    }

    return app.exec();
}
