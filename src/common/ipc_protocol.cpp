#include "common/ipc_protocol.h"

#include <QDataStream>

namespace pastetry::ipc {

namespace {
constexpr quint32 kMaxFrameBytes = 8U * 1024U * 1024U;
}

QByteArray encodeFrame(const QCborMap &message) {
    QByteArray payload = QCborValue(message).toCbor();

    QByteArray frame;
    frame.reserve(static_cast<int>(sizeof(quint32) + payload.size()));

    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << static_cast<quint32>(payload.size());
    frame.append(payload);

    return frame;
}

bool tryDecodeFrame(QByteArray *buffer, QCborMap *message) {
    if (!buffer || buffer->size() < static_cast<int>(sizeof(quint32))) {
        return false;
    }

    QDataStream lenReader(*buffer);
    lenReader.setByteOrder(QDataStream::LittleEndian);

    quint32 payloadSize = 0;
    lenReader >> payloadSize;

    if (payloadSize > kMaxFrameBytes) {
        buffer->clear();
        return false;
    }

    if (buffer->size() < static_cast<int>(sizeof(quint32) + payloadSize)) {
        return false;
    }

    const QByteArray payload =
        buffer->mid(static_cast<int>(sizeof(quint32)), static_cast<int>(payloadSize));
    buffer->remove(0, static_cast<int>(sizeof(quint32) + payloadSize));

    const QCborValue value = QCborValue::fromCbor(payload);
    if (!value.isMap()) {
        return false;
    }

    *message = value.toMap();
    return true;
}

QCborMap makeResponse(const QString &id, const QCborValue &result) {
    QCborMap map;
    map.insert(QStringLiteral("version"), 1);
    map.insert(QStringLiteral("id"), id);
    map.insert(QStringLiteral("ok"), true);
    map.insert(QStringLiteral("result"), result);
    return map;
}

QCborMap makeError(const QString &id, const QString &error) {
    QCborMap map;
    map.insert(QStringLiteral("version"), 1);
    map.insert(QStringLiteral("id"), id);
    map.insert(QStringLiteral("ok"), false);
    map.insert(QStringLiteral("error"), error);
    return map;
}

}  // namespace pastetry::ipc
