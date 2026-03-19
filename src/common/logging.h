#pragma once

#include <QString>

namespace pastetry::logging {

bool initialize(const QString &appName, const QString &dataDir,
                QString *error = nullptr);
void shutdown();
QString logFilePath();

}  // namespace pastetry::logging
