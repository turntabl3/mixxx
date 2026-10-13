#include "stems/stemprovisionjob.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>

namespace mixxx {

namespace {

constexpr int kStartTimeoutMillis = 10 * 1000;
// Polling interval. The process is polled rather than waited on so that output
// can be forwarded while pip is running.
constexpr int kPollMillis = 1000;
// Overall budget. A cold PyTorch download on a slow link can take a long time,
// and killing it half way through leaves a broken environment behind, so the
// limit is generous: it exists only so a wedged child cannot hang forever.
constexpr int kTotalTimeoutMillis = 60 * 60 * 1000;

/// The interpreter inside the environment the provision script creates.
QString environmentInterpreter(const QString& environmentDir) {
#if defined(Q_OS_WIN)
    const QString interpreter = QStringLiteral("venv/Scripts/python.exe");
#else
    const QString interpreter = QStringLiteral("venv/bin/python");
#endif
    return QDir(environmentDir).filePath(interpreter);
}

} // namespace

StemProvisionJob::StemProvisionJob(QObject* pParent,
        const QString& interpreter,
        const QString& provisionScript,
        const QString& environmentDir)
        : QThread(pParent),
          m_interpreter(interpreter),
          m_provisionScript(provisionScript),
          m_environmentDir(environmentDir) {
}

void StemProvisionJob::run() {
    if (m_interpreter.isEmpty() || m_provisionScript.isEmpty() ||
            m_environmentDir.isEmpty()) {
        emit failed(QObject::tr(
                "The separation environment cannot be installed because the "
                "installer is not configured."));
        return;
    }
    if (!QFileInfo::exists(m_provisionScript)) {
        emit failed(QObject::tr("Installer script not found: %1")
                            .arg(m_provisionScript));
        return;
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(m_interpreter, {m_provisionScript, m_environmentDir});
    if (!process.waitForStarted(kStartTimeoutMillis)) {
        emit failed(QObject::tr("Could not run %1: %2")
                            .arg(m_interpreter, process.errorString()));
        return;
    }

    QByteArray pending;
    const auto flushLines = [&]() {
        pending += process.readAllStandardOutput();
        int newline;
        while ((newline = pending.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(pending.left(newline)).trimmed();
            pending.remove(0, newline + 1);
            if (!line.isEmpty()) {
                emit outputLine(line);
            }
        }
    };

    QElapsedTimer elapsed;
    elapsed.start();
    while (!process.waitForFinished(kPollMillis)) {
        flushLines();
        if (elapsed.elapsed() > kTotalTimeoutMillis) {
            process.kill();
            process.waitForFinished(kStartTimeoutMillis);
            emit failed(QObject::tr(
                    "Installing the separation environment timed out."));
            return;
        }
    }
    flushLines();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        emit failed(QObject::tr("Installing the separation environment failed "
                                "(exit %1). The last output was:\n%2")
                            .arg(process.exitCode())
                            .arg(QString::fromUtf8(pending).trimmed()));
        return;
    }

    const QString python = environmentInterpreter(m_environmentDir);
    if (!QFileInfo::exists(python)) {
        emit failed(QObject::tr(
                "The installer finished but %1 is missing.")
                            .arg(python));
        return;
    }
    emit completed(python);
}

} // namespace mixxx
