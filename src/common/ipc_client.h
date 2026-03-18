#pragma once

#include <QCborMap>
#include <QString>

namespace pastetry {

class IpcClient {
public:
    explicit IpcClient(QString socketName);

    QCborMap request(const QString &method, const QCborMap &params,
                     int timeoutMs = 3000, QString *error = nullptr) const;

private:
    QString m_socketName;
};

}  // namespace pastetry
