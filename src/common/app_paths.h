#pragma once

#include <QString>

namespace pastetry {

struct AppPaths {
    QString dataDir;
    QString dbPath;
    QString blobDir;
    QString socketName;
};

AppPaths resolveAppPaths(const QString &overrideDataDir = QString(),
                         const QString &overrideSocketName = QString());

}  // namespace pastetry
