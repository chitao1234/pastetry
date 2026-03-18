#pragma once

#include <QCborMap>
#include <QByteArray>
#include <QIODevice>

namespace pastetry::ipc {

QByteArray encodeFrame(const QCborMap &message);
bool tryDecodeFrame(QByteArray *buffer, QCborMap *message);

QCborMap makeResponse(const QString &id, const QCborValue &result);
QCborMap makeError(const QString &id, const QString &error);

}  // namespace pastetry::ipc
