#include "clip-ui/app_controller.h"
#include "common/ipc_protocol.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QKeySequenceEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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
                        QString requestError;
                        if (!buildResponse(method, request.value(QStringLiteral("params")).toMap(),
                                           &result, &requestError)) {
                            socket->write(ipc::encodeFrame(ipc::makeError(id, requestError)));
                            continue;
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
    bool buildResponse(const QString &method, const QCborMap &params, QCborMap *result,
                       QString *error) {
        Q_UNUSED(params);
        if (!result) {
            if (error) {
                *error = QStringLiteral("result output missing");
            }
            return false;
        }

        if (method == QStringLiteral("Ping")) {
            *result = QCborMap{};
            return true;
        }
        if (method == QStringLiteral("GetCapturePolicy")) {
            QCborMap policy;
            policy.insert(QStringLiteral("profile"), QStringLiteral("balanced"));
            policy.insert(QStringLiteral("custom_allowlist"), QCborArray{});
            policy.insert(QStringLiteral("max_format_bytes"), 10 * 1024 * 1024);
            policy.insert(QStringLiteral("max_entry_bytes"), 32 * 1024 * 1024);
            *result = policy;
            return true;
        }
        if (method == QStringLiteral("ResolveSlotEntry")) {
            *result = QCborMap{{QStringLiteral("entry_id"), 0}};
            return true;
        }
        if (method == QStringLiteral("SearchEntries")) {
            *result = QCborMap{
                {QStringLiteral("entries"), QCborArray{}},
                {QStringLiteral("next_cursor"), -1},
                {QStringLiteral("query_valid"), true},
            };
            return true;
        }
        if (method == QStringLiteral("ActivateEntry") ||
            method == QStringLiteral("SetCapturePolicy")) {
            *result = QCborMap{};
            return true;
        }

        *result = QCborMap{};
        return true;
    }

    QString m_socketName;
    QLocalServer m_server;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};

class FakeSingleInstanceController final : public ISingleInstanceController {
public:
    bool notifyExistingInstance(const QString &instanceName, int timeoutMs,
                                QString *error) override {
        Q_UNUSED(instanceName);
        Q_UNUSED(timeoutMs);
        const int index = notifyCallCount++;
        const bool result = index < notifyResults.size() ? notifyResults.at(index) : false;
        if (error) {
            if (result) {
                error->clear();
            } else {
                *error = index < notifyErrors.size() ? notifyErrors.at(index)
                                                     : QStringLiteral("handoff failed");
            }
        }
        return result;
    }

    bool hasLikelyPeerUiProcess(qint64 selfPid, const QString &executableName,
                                QString *detail) const override {
        Q_UNUSED(selfPid);
        Q_UNUSED(executableName);
        ++peerProbeCallCount;
        if (detail) {
            *detail = peerDetail;
        }
        return peerRunning;
    }

    SingleInstanceStartResult startServer(
        const QString &instanceName, QObject *owner,
        const std::function<void(const QString &command)> &commandHandler,
        QString *error) override {
        Q_UNUSED(owner);
        lastInstanceName = instanceName;
        m_commandHandler = commandHandler;
        const int index = startCallCount++;
        const SingleInstanceStartResult result =
            index < startResults.size() ? startResults.at(index)
                                        : SingleInstanceStartResult::Started;
        if (error) {
            *error = index < startErrors.size() ? startErrors.at(index) : QString();
        }
        return result;
    }

    void removeServer(const QString &instanceName) override {
        ++removeCallCount;
        lastRemovedInstanceName = instanceName;
    }

    void triggerCommand(const QString &command) {
        if (m_commandHandler) {
            m_commandHandler(command);
        }
    }

    QVector<bool> notifyResults;
    QVector<QString> notifyErrors;
    QVector<SingleInstanceStartResult> startResults;
    QVector<QString> startErrors;
    mutable int peerProbeCallCount = 0;
    bool peerRunning = false;
    QString peerDetail = QStringLiteral("No peer UI process found");

    int notifyCallCount = 0;
    int startCallCount = 0;
    int removeCallCount = 0;
    QString lastInstanceName;
    QString lastRemovedInstanceName;

private:
    std::function<void(const QString &)> m_commandHandler;
};

class FakeShortcutService final : public IShortcutService {
public:
    explicit FakeShortcutService(QObject *parent = nullptr) : IShortcutService(parent) {}

    ShortcutRegistrationState registerShortcut(
        const QKeySequence &sequence, bool requireModifier,
        ShortcutInteractionPolicy interactionPolicy) override {
        registerCalls.push_back({sequence, requireModifier, interactionPolicy});
        return nextRegisterState;
    }

    void unregisterShortcut() override {
        ++unregisterCallCount;
    }

    QString lastError() const override {
        return lastErrorText;
    }

    void triggerActivated() {
        emit activated();
    }

    ShortcutRegistrationState nextRegisterState = ShortcutRegistrationState::Registered;
    QString lastErrorText;
    struct RegisterCall {
        QKeySequence sequence;
        bool requireModifier = false;
        ShortcutInteractionPolicy interactionPolicy =
            ShortcutInteractionPolicy::Interactive;
    };

    QVector<RegisterCall> registerCalls;
    int unregisterCallCount = 0;
};

class FakeShortcutServiceFactory final : public IShortcutServiceFactory {
public:
    IShortcutService *create(QObject *parent, int windowsHotkeyId) override {
        Q_UNUSED(windowsHotkeyId);
        auto *service = new FakeShortcutService(parent);
        service->nextRegisterState = defaultRegisterState;
        service->lastErrorText = defaultError;
        createdServices.push_back(service);
        return service;
    }

    ShortcutRegistrationState defaultRegisterState = ShortcutRegistrationState::Registered;
    QString defaultError;
    QVector<FakeShortcutService *> createdServices;
};

class FakeUserInteraction final : public IUserInteraction {
public:
    TakeoverPromptChoice promptSingleInstanceTakeover(QWidget *parent,
                                                      const QString &detail) override {
        Q_UNUSED(parent);
        promptDetails.push_back(detail);
        const int index = promptCallCount++;
        return index < promptChoices.size() ? promptChoices.at(index)
                                            : TakeoverPromptChoice::Exit;
    }

    void showWarning(QWidget *parent, const QString &title,
                     const QString &message) override {
        Q_UNUSED(parent);
        warnings.push_back({title, message});
    }

    void onSettingsDialogOpened(QDialog *dialog) override {
        settingsDialogOpenCount++;
        if (settingsDialogHook) {
            settingsDialogHook(dialog);
        }
    }

    struct WarningItem {
        QString title;
        QString message;
    };

    int promptCallCount = 0;
    int settingsDialogOpenCount = 0;
    QVector<TakeoverPromptChoice> promptChoices;
    QVector<QString> promptDetails;
    QVector<WarningItem> warnings;
    std::function<void(QDialog *)> settingsDialogHook;
};

void configureSettingsRoot(const QString &rootDir) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, rootDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, rootDir);

    QSettings settings(QStringLiteral("pastetry"), QStringLiteral("pastetry"));
    settings.clear();
    settings.sync();
}

void setShortcutBinding(const QString &actionId, const QString &mode, const QString &direct,
                        const QString &chordFirst = QString(),
                        const QString &chordSecond = QString()) {
    QSettings settings(QStringLiteral("pastetry"), QStringLiteral("pastetry"));
    const QString base = QStringLiteral("hotkey/actions_v2/%1").arg(actionId);
    settings.setValue(base + QStringLiteral("/mode"), mode);
    settings.setValue(base + QStringLiteral("/direct"), direct);
    settings.setValue(base + QStringLiteral("/chord_first"), chordFirst);
    settings.setValue(base + QStringLiteral("/chord_second"), chordSecond);
    settings.sync();
}

AppPaths makeAppPaths(const QTemporaryDir &dir, const QString &socketName) {
    AppPaths paths;
    paths.dataDir = dir.path();
    paths.dbPath = dir.filePath(QStringLiteral("history.sqlite3"));
    paths.blobDir = dir.filePath(QStringLiteral("blobs"));
    QDir().mkpath(paths.blobDir);
    paths.socketName = socketName;
    return paths;
}

FakeShortcutService *findServiceByPortableSequence(const QVector<FakeShortcutService *> &services,
                                                   const QString &portableText) {
    for (FakeShortcutService *service : services) {
        if (!service || service->registerCalls.isEmpty()) {
            continue;
        }
        if (service->registerCalls.first().sequence.toString(QKeySequence::PortableText) ==
            portableText) {
            return service;
        }
    }
    return nullptr;
}

QWidget *findTopLevelByTitle(const QString &title) {
    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget *widget : topLevels) {
        if (!widget) {
            continue;
        }
        if (widget->windowTitle() == title) {
            return widget;
        }
    }
    return nullptr;
}

}  // namespace

class AppControllerIntegrationTests : public QObject {
    Q_OBJECT

private slots:
    void staleSocketFalseAlarmTakesOverWithoutPrompt();
    void peerConflictRetryThenHandoffPath();
    void peerConflictExitPath();
    void peerConflictTakeOverPath();
    void singleInstanceCommandDispatch();
    void shortcutRegistrationAndChordLifecycle();
    void settingsCancelAfterEditKeepsShortcutRegistrations();
};

void AppControllerIntegrationTests::staleSocketFalseAlarmTakesOverWithoutPrompt() {
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());
    configureSettingsRoot(settingsDir.path());

    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    auto *singleInstance = new FakeSingleInstanceController();
    singleInstance->notifyResults = {false, false};
    singleInstance->notifyErrors = {QStringLiteral("connect timeout"),
                                    QStringLiteral("broken pipe")};
    singleInstance->startResults = {SingleInstanceStartResult::AddressInUse,
                                    SingleInstanceStartResult::Started};
    singleInstance->peerRunning = false;
    singleInstance->peerDetail = QStringLiteral("No peer UI process found");

    auto *shortcutFactory = new FakeShortcutServiceFactory();
    auto *userInteraction = new FakeUserInteraction();

    AppController controller(makeAppPaths(dataDir, socketName),
                             std::unique_ptr<ISingleInstanceController>(singleInstance),
                             std::unique_ptr<IShortcutServiceFactory>(shortcutFactory),
                             std::unique_ptr<IUserInteraction>(userInteraction));

    QString error;
    QVERIFY2(controller.initialize(&error), qPrintable(error));
    QCOMPARE(singleInstance->notifyCallCount, 2);
    QCOMPARE(singleInstance->startCallCount, 2);
    QCOMPARE(singleInstance->removeCallCount, 1);
    QCOMPARE(userInteraction->promptCallCount, 0);
}

void AppControllerIntegrationTests::peerConflictRetryThenHandoffPath() {
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());
    configureSettingsRoot(settingsDir.path());

    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    auto *singleInstance = new FakeSingleInstanceController();
    singleInstance->notifyResults = {false, false, true};
    singleInstance->notifyErrors = {QStringLiteral("not connected"),
                                    QStringLiteral("broken pipe"), QString()};
    singleInstance->startResults = {SingleInstanceStartResult::AddressInUse};
    singleInstance->peerRunning = true;
    singleInstance->peerDetail = QStringLiteral("Found peer UI process PID(s): 4321");

    auto *shortcutFactory = new FakeShortcutServiceFactory();
    auto *userInteraction = new FakeUserInteraction();
    userInteraction->promptChoices = {TakeoverPromptChoice::RetryHandoff};

    AppController controller(makeAppPaths(dataDir, socketName),
                             std::unique_ptr<ISingleInstanceController>(singleInstance),
                             std::unique_ptr<IShortcutServiceFactory>(shortcutFactory),
                             std::unique_ptr<IUserInteraction>(userInteraction));

    QString error;
    QVERIFY(!controller.initialize(&error));
    QVERIFY(error.isEmpty());
    QCOMPARE(singleInstance->notifyCallCount, 3);
    QCOMPARE(singleInstance->startCallCount, 1);
    QCOMPARE(singleInstance->removeCallCount, 0);
    QCOMPARE(userInteraction->promptCallCount, 1);
}

void AppControllerIntegrationTests::peerConflictExitPath() {
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());
    configureSettingsRoot(settingsDir.path());

    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    auto *singleInstance = new FakeSingleInstanceController();
    singleInstance->notifyResults = {false, false};
    singleInstance->startResults = {SingleInstanceStartResult::AddressInUse};
    singleInstance->peerRunning = true;
    singleInstance->peerDetail = QStringLiteral("Found peer UI process PID(s): 1234");

    auto *shortcutFactory = new FakeShortcutServiceFactory();
    auto *userInteraction = new FakeUserInteraction();
    userInteraction->promptChoices = {TakeoverPromptChoice::Exit};

    AppController controller(makeAppPaths(dataDir, socketName),
                             std::unique_ptr<ISingleInstanceController>(singleInstance),
                             std::unique_ptr<IShortcutServiceFactory>(shortcutFactory),
                             std::unique_ptr<IUserInteraction>(userInteraction));

    QString error;
    QVERIFY(!controller.initialize(&error));
    QVERIFY(error.isEmpty());
    QCOMPARE(singleInstance->removeCallCount, 0);
    QCOMPARE(userInteraction->promptCallCount, 1);
}

void AppControllerIntegrationTests::peerConflictTakeOverPath() {
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());
    configureSettingsRoot(settingsDir.path());

    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    auto *singleInstance = new FakeSingleInstanceController();
    singleInstance->notifyResults = {false, false};
    singleInstance->startResults = {SingleInstanceStartResult::AddressInUse,
                                    SingleInstanceStartResult::Started};
    singleInstance->peerRunning = true;
    singleInstance->peerDetail = QStringLiteral("Found peer UI process PID(s): 1234");

    auto *shortcutFactory = new FakeShortcutServiceFactory();
    auto *userInteraction = new FakeUserInteraction();
    userInteraction->promptChoices = {TakeoverPromptChoice::TakeOver};

    AppController controller(makeAppPaths(dataDir, socketName),
                             std::unique_ptr<ISingleInstanceController>(singleInstance),
                             std::unique_ptr<IShortcutServiceFactory>(shortcutFactory),
                             std::unique_ptr<IUserInteraction>(userInteraction));

    QString error;
    QVERIFY2(controller.initialize(&error), qPrintable(error));
    QCOMPARE(singleInstance->removeCallCount, 1);
    QCOMPARE(userInteraction->promptCallCount, 1);
}

void AppControllerIntegrationTests::singleInstanceCommandDispatch() {
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());
    configureSettingsRoot(settingsDir.path());

    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    auto *singleInstance = new FakeSingleInstanceController();
    singleInstance->notifyResults = {false};
    singleInstance->startResults = {SingleInstanceStartResult::Started};

    auto *shortcutFactory = new FakeShortcutServiceFactory();
    auto *userInteraction = new FakeUserInteraction();

    AppController controller(makeAppPaths(dataDir, socketName),
                             std::unique_ptr<ISingleInstanceController>(singleInstance),
                             std::unique_ptr<IShortcutServiceFactory>(shortcutFactory),
                             std::unique_ptr<IUserInteraction>(userInteraction));

    QString error;
    QVERIFY2(controller.initialize(&error), qPrintable(error));

    QWidget *mainWindow = findTopLevelByTitle(QStringLiteral("Pastetry"));
    QVERIFY(mainWindow);
    mainWindow->hide();
    QTRY_VERIFY(!mainWindow->isVisible());

    singleInstance->triggerCommand(QStringLiteral("show-main"));
    QTRY_VERIFY(mainWindow->isVisible());

    singleInstance->triggerCommand(QStringLiteral("toggle-popup"));
    QWidget *quickPaste = findTopLevelByTitle(QStringLiteral("Quick Paste"));
    QVERIFY(quickPaste);
    QTRY_VERIFY(quickPaste->isVisible());

    singleInstance->triggerCommand(QStringLiteral("toggle-popup"));
    QTRY_VERIFY(!quickPaste->isVisible());
}

void AppControllerIntegrationTests::shortcutRegistrationAndChordLifecycle() {
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());
    configureSettingsRoot(settingsDir.path());

    setShortcutBinding(QStringLiteral("quick_paste_popup"), QStringLiteral("direct"),
                       QStringLiteral("Ctrl+Alt+P"));
    setShortcutBinding(QStringLiteral("copy_recent_1"), QStringLiteral("chord"), QString(),
                       QStringLiteral("Ctrl+K"), QStringLiteral("Ctrl+C"));

    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    auto *singleInstance = new FakeSingleInstanceController();
    singleInstance->notifyResults = {false};
    singleInstance->startResults = {SingleInstanceStartResult::Started};

    auto *shortcutFactory = new FakeShortcutServiceFactory();
    auto *userInteraction = new FakeUserInteraction();

    AppController controller(makeAppPaths(dataDir, socketName),
                             std::unique_ptr<ISingleInstanceController>(singleInstance),
                             std::unique_ptr<IShortcutServiceFactory>(shortcutFactory),
                             std::unique_ptr<IUserInteraction>(userInteraction));

    QString error;
    QVERIFY2(controller.initialize(&error), qPrintable(error));
    QCOMPARE(shortcutFactory->createdServices.size(), 2);

    FakeShortcutService *directService =
        findServiceByPortableSequence(shortcutFactory->createdServices,
                                      QStringLiteral("Ctrl+Alt+P"));
    FakeShortcutService *chordFirstService =
        findServiceByPortableSequence(shortcutFactory->createdServices,
                                      QStringLiteral("Ctrl+K"));
    QVERIFY(directService);
    QVERIFY(chordFirstService);
    QCOMPARE(directService->registerCalls.first().interactionPolicy,
             ShortcutInteractionPolicy::NonInteractive);
    QCOMPARE(chordFirstService->registerCalls.first().interactionPolicy,
             ShortcutInteractionPolicy::NonInteractive);

    chordFirstService->triggerActivated();
    QTRY_COMPARE(shortcutFactory->createdServices.size(), 3);

    FakeShortcutService *chordSecondService = shortcutFactory->createdServices.last();
    QVERIFY(chordSecondService);
    QCOMPARE(chordSecondService->registerCalls.size(), 1);
    QCOMPARE(chordSecondService->registerCalls.first().sequence.toString(QKeySequence::PortableText),
             QStringLiteral("Ctrl+C"));
    QVERIFY(!chordSecondService->registerCalls.first().requireModifier);
    QCOMPARE(chordSecondService->registerCalls.first().interactionPolicy,
             ShortcutInteractionPolicy::NonInteractive);

    chordSecondService->triggerActivated();
    QTRY_COMPARE(shortcutFactory->createdServices.size(), 5);
    QVERIFY(directService->unregisterCallCount > 0);
}

void AppControllerIntegrationTests::settingsCancelAfterEditKeepsShortcutRegistrations() {
    QTemporaryDir settingsDir;
    QTemporaryDir dataDir;
    QVERIFY(settingsDir.isValid());
    QVERIFY(dataDir.isValid());
    configureSettingsRoot(settingsDir.path());

    setShortcutBinding(QStringLiteral("quick_paste_popup"), QStringLiteral("direct"),
                       QStringLiteral("Ctrl+Alt+P"));

    const QString socketName =
        QStringLiteral("pastetry-test-%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    FakeDaemonServer daemon(socketName);
    QString daemonError;
    QVERIFY2(daemon.start(&daemonError), qPrintable(daemonError));

    auto *singleInstance = new FakeSingleInstanceController();
    singleInstance->notifyResults = {false};
    singleInstance->startResults = {SingleInstanceStartResult::Started};

    auto *shortcutFactory = new FakeShortcutServiceFactory();
    auto *userInteraction = new FakeUserInteraction();

    bool hookRan = false;
    bool editedShortcut = false;
    userInteraction->settingsDialogHook =
        [&hookRan, &editedShortcut](QDialog *dialog) {
            hookRan = true;
            QList<QKeySequenceEdit *> edits = dialog->findChildren<QKeySequenceEdit *>();
            if (!edits.isEmpty()) {
                edits.first()->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
                editedShortcut = true;
            }
            QTimer::singleShot(0, dialog, &QDialog::reject);
        };

    AppController controller(makeAppPaths(dataDir, socketName),
                             std::unique_ptr<ISingleInstanceController>(singleInstance),
                             std::unique_ptr<IShortcutServiceFactory>(shortcutFactory),
                             std::unique_ptr<IUserInteraction>(userInteraction));

    QString error;
    QVERIFY2(controller.initialize(&error), qPrintable(error));

    FakeShortcutService *directService =
        findServiceByPortableSequence(shortcutFactory->createdServices,
                                      QStringLiteral("Ctrl+Alt+P"));
    QVERIFY(directService);
    const int createdBefore = shortcutFactory->createdServices.size();

    QVERIFY(QMetaObject::invokeMethod(&controller, "openSettings", Qt::DirectConnection));
    QVERIFY(hookRan);
    QVERIFY(editedShortcut);
    QCOMPARE(userInteraction->settingsDialogOpenCount, 1);
    QVERIFY(shortcutFactory->createdServices.size() > createdBefore);
}

QTEST_MAIN(AppControllerIntegrationTests)

#include "app_controller_integration_tests.moc"
