#include "common/logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>

#include <cstdlib>
#include <cstdio>

namespace pastetry::logging {
namespace {

constexpr qint64 kMaxLogSizeBytes = 5 * 1024 * 1024;

QMutex g_mutex;
QFile *g_logFile = nullptr;
QString g_logPath;
QtMessageHandler g_previousHandler = nullptr;
bool g_initialized = false;

const char *typeToText(QtMsgType type) {
    switch (type) {
        case QtDebugMsg:
            return "DEBUG";
        case QtInfoMsg:
            return "INFO";
        case QtWarningMsg:
            return "WARN";
        case QtCriticalMsg:
            return "ERROR";
        case QtFatalMsg:
            return "FATAL";
    }
    return "UNKNOWN";
}

QByteArray formatLogLine(QtMsgType type, const QMessageLogContext &context,
                         const QString &message) {
    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString category =
        context.category ? QString::fromUtf8(context.category) : QStringLiteral("default");
    const quintptr tid = reinterpret_cast<quintptr>(QThread::currentThreadId());

    return QStringLiteral("%1 [%2] [tid=%3] [%4] %5\n")
        .arg(timestamp, QString::fromLatin1(typeToText(type)))
        .arg(tid)
        .arg(category, message)
        .toUtf8();
}

void writeRawToStderr(const QByteArray &line) {
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    std::fflush(stderr);
}

void messageHandler(QtMsgType type, const QMessageLogContext &context,
                    const QString &message) {
    const QByteArray line = formatLogLine(type, context, message);

    writeRawToStderr(line);

    {
        QMutexLocker lock(&g_mutex);
        if (g_logFile && g_logFile->isOpen()) {
            g_logFile->write(line);
            g_logFile->flush();
        }
    }

    if (type == QtFatalMsg) {
        std::abort();
    }
}

bool rotateIfNeeded(const QString &path, QString *error) {
    QFile current(path);
    if (!current.exists()) {
        return true;
    }

    if (current.size() < kMaxLogSizeBytes) {
        return true;
    }

    const QString backupPath = path + QStringLiteral(".1");
    QFile::remove(backupPath);

    if (!QFile::rename(path, backupPath)) {
        if (error) {
            *error = QStringLiteral("Failed to rotate log file %1").arg(path);
        }
        return false;
    }

    return true;
}

}  // namespace

bool initialize(const QString &appName, const QString &dataDir, QString *error) {
    QMutexLocker lock(&g_mutex);

    if (g_initialized) {
        return true;
    }

    const QString logsDir = QDir(dataDir).filePath(QStringLiteral("logs"));
    if (!QDir().mkpath(logsDir)) {
        if (error) {
            *error = QStringLiteral("Failed to create log directory: %1").arg(logsDir);
        }
        return false;
    }

    g_logPath = QDir(logsDir).filePath(appName + QStringLiteral(".log"));

    if (!rotateIfNeeded(g_logPath, error)) {
        return false;
    }

    g_logFile = new QFile(g_logPath);
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (error) {
            *error = g_logFile->errorString();
        }
        delete g_logFile;
        g_logFile = nullptr;
        return false;
    }

    const QString rules = qEnvironmentVariableIsSet("PASTETRY_LOG_RULES")
                              ? qEnvironmentVariable("PASTETRY_LOG_RULES")
                              : QStringLiteral("*.debug=false\nqt.*.debug=false");
    QLoggingCategory::setFilterRules(rules);

    g_previousHandler = qInstallMessageHandler(messageHandler);
    g_initialized = true;

    const QMessageLogContext startupContext("", 0, "", "pastetry.logging");
    const QByteArray startupLine =
        formatLogLine(QtInfoMsg, startupContext,
                      QStringLiteral("Logging initialized at %1").arg(g_logPath));
    writeRawToStderr(startupLine);
    if (g_logFile->isOpen()) {
        g_logFile->write(startupLine);
        g_logFile->flush();
    }

    return true;
}

void shutdown() {
    QMutexLocker lock(&g_mutex);
    if (!g_initialized) {
        return;
    }

    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;

    if (g_logFile) {
        if (g_logFile->isOpen()) {
            g_logFile->flush();
            g_logFile->close();
        }
        delete g_logFile;
        g_logFile = nullptr;
    }

    g_logPath.clear();
    g_initialized = false;
}

QString logFilePath() {
    QMutexLocker lock(&g_mutex);
    return g_logPath;
}

}  // namespace pastetry::logging
