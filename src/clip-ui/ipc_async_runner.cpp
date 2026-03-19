#include "clip-ui/ipc_async_runner.h"

#include "common/ipc_client.h"

#include <QMetaObject>
#include <QMutexLocker>
#include <QThreadPool>

namespace pastetry {

IpcAsyncRunner::IpcAsyncRunner(QString socketName, QObject *parent)
    : QObject(parent), m_socketName(std::move(socketName)) {}

qint64 IpcAsyncRunner::request(
    const QString &method, const QCborMap &params, int timeoutMs, QObject *context,
    std::function<void(const QCborMap &, const QString &)> callback) {
    qint64 requestId = 0;
    {
        QMutexLocker locker(&m_mutex);
        requestId = m_nextRequestId++;
        m_pending.insert(requestId, PendingRequest{context, std::move(callback)});
    }

    const QString socketName = m_socketName;
    const QString requestMethod = method;
    const QCborMap requestParams = params;
    const QPointer<IpcAsyncRunner> self(this);

    QThreadPool::globalInstance()->start(
        [self, requestId, socketName, requestMethod, requestParams, timeoutMs] {
            IpcClient client(socketName);
            QString error;
            QCborMap result = client.request(requestMethod, requestParams, timeoutMs, &error);

            if (!self) {
                return;
            }

            QMetaObject::invokeMethod(
                self,
                [self, requestId, result = std::move(result), error = std::move(error)]() mutable {
                    if (!self) {
                        return;
                    }
                    self->finishRequest(requestId, std::move(result), std::move(error));
                },
                Qt::QueuedConnection);
        });

    return requestId;
}

void IpcAsyncRunner::finishRequest(qint64 requestId, QCborMap result, QString error) {
    PendingRequest pending;
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_pending.find(requestId);
        if (it == m_pending.end()) {
            return;
        }
        pending = it.value();
        m_pending.erase(it);
    }

    if (!pending.context || !pending.callback) {
        return;
    }

    pending.callback(result, error);
}

}  // namespace pastetry
