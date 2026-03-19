#include "clip-ui/ipc_async_runner.h"
#include "clip-ui/quick_paste_dialog.h"
#include "common/ipc_protocol.h"

#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMoveEvent>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

using namespace pastetry;

namespace {

class FakeDaemonServer {
public:
    explicit FakeDaemonServer(QString socketName) : m_socketName(std::move(socketName)) {}

    ~FakeDaemonServer() {
        m_server.close();
        QLocalServer::removeServer(m_socketName);
    }

    bool start(QString *error) {
        QLocalServer::removeServer(m_socketName);
        if (!m_server.listen(m_socketName)) {
            if (error) {
                *error = m_server.errorString();
            }
            return false;
        }

        QObject::connect(&m_server, &QLocalServer::newConnection, &m_server, [this] {
            while (m_server.hasPendingConnections()) {
                QLocalSocket *socket = m_server.nextPendingConnection();
                QObject::connect(socket, &QLocalSocket::readyRead, &m_server, [this, socket] {
                    QByteArray &buffer = m_buffers[socket];
                    buffer.append(socket->readAll());

                    QCborMap request;
                    while (ipc::tryDecodeFrame(&buffer, &request)) {
                        const QString id = request.value(QStringLiteral("id")).toString();
                        const QString method =
                            request.value(QStringLiteral("method")).toString();
                        QCborMap result;
                        if (method == QStringLiteral("SearchEntries")) {
                            result.insert(QStringLiteral("entries"), QCborArray{});
                            result.insert(QStringLiteral("next_cursor"), -1);
                            result.insert(QStringLiteral("query_valid"), true);
                        } else {
                            result = QCborMap{};
                        }
                        socket->write(
                            ipc::encodeFrame(ipc::makeResponse(id, QCborValue(result))));
                    }
                });
                QObject::connect(socket, &QLocalSocket::disconnected, &m_server, [this, socket] {
                    m_buffers.remove(socket);
                    socket->deleteLater();
                });
            }
        });
        return true;
    }

private:
    QString m_socketName;
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};

}  // namespace

class QuickPasteDialogTests : public QObject {
    Q_OBJECT

private slots:
    void deferredHideAfterDeactivate();
    void moveEventCancelsDeferredHide();
    void lastLocationModeReopensAtRememberedPosition();
};

void QuickPasteDialogTests::deferredHideAfterDeactivate() {
    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    IpcAsyncRunner ipcRunner(socketName);
    QuickPasteDialog dialog(&ipcRunner);
    QSignalSpy hiddenSpy(&dialog, &QuickPasteDialog::popupHidden);

    dialog.openPopup();
    QVERIFY(dialog.isVisible());
    QDialog blocker;
    blocker.show();
    blocker.raise();
    blocker.activateWindow();

    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&dialog, &deactivate);
    QVERIFY(dialog.isVisible());

    QTest::qWait(280);
    QTRY_VERIFY(!dialog.isVisible());
    QVERIFY(hiddenSpy.count() >= 1);
}

void QuickPasteDialogTests::moveEventCancelsDeferredHide() {
    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    IpcAsyncRunner ipcRunner(socketName);
    QuickPasteDialog dialog(&ipcRunner);
    QSignalSpy hiddenSpy(&dialog, &QuickPasteDialog::popupHidden);

    dialog.openPopup();
    QVERIFY(dialog.isVisible());
    QDialog blocker;
    blocker.show();
    blocker.raise();
    blocker.activateWindow();

    QEvent deactivate(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&dialog, &deactivate);

    QMoveEvent moveEvent(dialog.pos() + QPoint(12, 6), dialog.pos());
    QCoreApplication::sendEvent(&dialog, &moveEvent);

    QTest::qWait(90);
    QVERIFY(dialog.isVisible());
    QCOMPARE(hiddenSpy.count(), 0);

    dialog.hide();
}

void QuickPasteDialogTests::lastLocationModeReopensAtRememberedPosition() {
    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    IpcAsyncRunner ipcRunner(socketName);
    QuickPasteDialog dialog(&ipcRunner);
    dialog.resize(640, 360);
    dialog.setPopupPositionMode(QuickPasteDialog::PopupPositionMode::LastLocation);
    dialog.setLastPopupPosition(QPoint(10, 20), true);

    dialog.openPopup();
    QCoreApplication::processEvents();
    const QPoint firstOpenedPos = dialog.pos();
    dialog.hide();
    QVERIFY(dialog.hasLastPopupPosition());

    dialog.openPopup();
    QCoreApplication::processEvents();
    QCOMPARE(dialog.pos(), firstOpenedPos);
    dialog.hide();
}

QTEST_MAIN(QuickPasteDialogTests)

#include "quick_paste_dialog_tests.moc"
