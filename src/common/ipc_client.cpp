#include "common/ipc_client.h"

#include "common/ipc_protocol.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocalSocket>
#include <QUuid>

namespace pastetry {

IpcClient::IpcClient(QString socketName) : m_socketName(std::move(socketName)) {}

QCborMap IpcClient::request(const QString &method, const QCborMap &params,
                            int timeoutMs, QString *error) const {
    QLocalSocket socket;
    socket.connectToServer(m_socketName);
    if (!socket.waitForConnected(timeoutMs)) {
        if (error) {
            *error = socket.errorString();
        }
        return {};
    }

    const QString reqId = QUuid::createUuid().toString(QUuid::Id128);

    QCborMap requestMap;
    requestMap.insert(QStringLiteral("version"), 1);
    requestMap.insert(QStringLiteral("id"), reqId);
    requestMap.insert(QStringLiteral("method"), method);
    requestMap.insert(QStringLiteral("params"), params);

    const QByteArray frame = ipc::encodeFrame(requestMap);
    if (socket.write(frame) != frame.size()) {
        if (error) {
            *error = "Failed to write full request";
        }
        return {};
    }

    if (!socket.waitForBytesWritten(timeoutMs)) {
        if (error) {
            *error = socket.errorString();
        }
        return {};
    }

    QByteArray buffer;
    QCborMap response;

    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        if (!socket.waitForReadyRead(50)) {
            continue;
        }
        buffer.append(socket.readAll());
        if (ipc::tryDecodeFrame(&buffer, &response)) {
            break;
        }
    }

    if (response.isEmpty()) {
        if (error) {
            *error = "Timed out waiting for response";
        }
        return {};
    }

    const QString responseId = response.value("id").toString();
    if (responseId != reqId) {
        if (error) {
            *error = "Mismatched response id";
        }
        return {};
    }

    if (!response.value("ok").toBool()) {
        if (error) {
            *error = response.value("error").toString();
        }
        return {};
    }

    const QCborValue result = response.value("result");
    if (!result.isMap()) {
        if (error) {
            *error = "Invalid response payload";
        }
        return {};
    }

    return result.toMap();
}

}  // namespace pastetry
