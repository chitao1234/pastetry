#pragma once

#include "common/app_paths.h"
#include "common/clipboard_repository.h"
#include "clipd/wayland_clipboard_runtime.h"

#include <QLocalServer>
#include <QMimeData>
#include <QObject>
#include <QHash>
#include <QTimer>

class QClipboard;
class QLocalSocket;

namespace pastetry {

class ClipboardDaemon : public QObject {
    Q_OBJECT

public:
    explicit ClipboardDaemon(AppPaths paths, QObject *parent = nullptr);
    bool start(QString *error);

private:
    bool isWaylandPlatform() const;
    bool captureEntry(const CapturedEntry &entry, bool fromPoll);
    bool captureCurrentClipboard(bool fromPoll);

    void onClipboardChanged();
    void pollClipboard();
    void onNewConnection();
    void onClientReadyRead(QLocalSocket *socket);

    QCborMap handleRequest(const QCborMap &request);
    bool activateEntry(qint64 entryId, const QString &preferredFormat, QString *error);
    CapturedEntry captureFromMimeData(const QMimeData *mimeData) const;
    bool validateCapturePolicy(const CapturePolicy &policy, QString *error) const;
    bool setCapturePolicy(const CapturePolicy &policy, QString *error);
    bool loadCapturePolicy(QString *error);
    QString fingerprint(const CapturedEntry &entry) const;

    AppPaths m_paths;
    ClipboardRepository m_repo;
    QLocalServer m_server;
    QClipboard *m_clipboard = nullptr;
    QHash<QLocalSocket *, QByteArray> m_clientBuffers;
    bool m_suppressCapture = false;
    QString m_lastFingerprint;
    QString m_lastObservedFingerprint;
    qint64 m_lastCaptureAtMs = 0;
    QTimer m_clipboardPollTimer;
    bool m_waylandSession = false;
    WaylandBackendSelection m_waylandBackendSelection;
    WaylandClipboardProbeResult m_waylandProbeResult;
    CapturePolicy m_capturePolicy;
};

}  // namespace pastetry
