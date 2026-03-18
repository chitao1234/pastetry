#include "common/app_paths.h"

#include <QDir>
#include <QStandardPaths>

namespace pastetry {

AppPaths resolveAppPaths(const QString &overrideDataDir,
                         const QString &overrideSocketName) {
    AppPaths paths;

    if (!overrideDataDir.isEmpty()) {
        paths.dataDir = overrideDataDir;
    } else {
        const QString base =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        paths.dataDir = QDir(base).filePath("pastetry");
    }

    QDir().mkpath(paths.dataDir);
    paths.dbPath = QDir(paths.dataDir).filePath("history.sqlite3");
    paths.blobDir = QDir(paths.dataDir).filePath("blobs");
    QDir().mkpath(paths.blobDir);

    paths.socketName =
        overrideSocketName.isEmpty() ? QStringLiteral("pastetry.clipd.v1")
                                     : overrideSocketName;

    return paths;
}

}  // namespace pastetry
