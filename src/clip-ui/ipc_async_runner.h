#pragma once

#include <QCborMap>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace pastetry {

class IpcAsyncRunner : public QObject {
    Q_OBJECT

public:
    explicit IpcAsyncRunner(QString socketName, QObject *parent = nullptr);

    qint64 request(const QString &method, const QCborMap &params, int timeoutMs,
                   QObject *context,
                   std::function<void(const QCborMap &result, const QString &error)> callback);

private:
    struct PendingRequest {
        QPointer<QObject> context;
        std::function<void(const QCborMap &, const QString &)> callback;
    };

    void finishRequest(qint64 requestId, QCborMap result, QString error);

    QString m_socketName;
    mutable QMutex m_mutex;
    qint64 m_nextRequestId = 1;
    QHash<qint64, PendingRequest> m_pending;
};

}  // namespace pastetry
