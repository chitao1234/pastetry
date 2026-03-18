#pragma once

#include "common/app_paths.h"
#include "common/clipboard_repository.h"

#include <QLocalServer>
#include <QMimeData>
#include <QObject>
#include <QHash>

class QClipboard;
class QLocalSocket;

namespace pastetry {

class ClipboardDaemon : public QObject {
    Q_OBJECT

public:
    explicit ClipboardDaemon(AppPaths paths, QObject *parent = nullptr);
    bool start(QString *error);

private:
    void onClipboardChanged();
    void onNewConnection();
    void onClientReadyRead(QLocalSocket *socket);

    QCborMap handleRequest(const QCborMap &request);
    bool activateEntry(qint64 entryId, const QString &preferredFormat, QString *error);
    CapturedEntry captureFromMimeData(const QMimeData *mimeData) const;
    QString fingerprint(const CapturedEntry &entry) const;

    AppPaths m_paths;
    ClipboardRepository m_repo;
    QLocalServer m_server;
    QClipboard *m_clipboard = nullptr;
    QHash<QLocalSocket *, QByteArray> m_clientBuffers;
    bool m_suppressCapture = false;
    QString m_lastFingerprint;
    qint64 m_lastCaptureAtMs = 0;
};

}  // namespace pastetry
