#include "stems/stemprovisionjob.h"

#include "stems/stemscripts.h"
#include "stems/stempython.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>

#include "moc_stemprovisionjob.cpp"

namespace mixxx {

namespace {

constexpr int kStartTimeoutMillis = 10 * 1000;
// Polling interval. The process is polled rather than waited on so that output
// can be forwarded while pip is running.
constexpr int kPollMillis = 1000;
// Overall budget, shared by every step. A cold PyTorch download on a slow link
// can take a long time, and killing it half way through leaves a broken
// environment behind, so the limit is generous: it exists only so a wedged
// child cannot hang forever.
constexpr int kTotalTimeoutMillis = 60 * 60 * 1000;
// How much recent output a failure message keeps. Enough to see what went
// wrong without pasting an entire pip log into a dialog.
constexpr int kTailLines = 10;

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

bool StemProvisionJob::runStep(const QString& program,
        const QStringList& arguments) {
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(kStartTimeoutMillis)) {
        emit failed(QObject::tr("Could not run %1: %2")
                            .arg(program, process.errorString()));
        return false;
    }

    QByteArray pending;
    QStringList tail;
    const auto flushLines = [&]() {
        pending += process.readAllStandardOutput();
        int newline;
        while ((newline = pending.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(pending.left(newline)).trimmed();
            pending.remove(0, newline + 1);
            if (!line.isEmpty()) {
                emit outputLine(line);
                tail.append(line);
                while (tail.size() > kTailLines) {
                    tail.removeFirst();
                }
            }
        }
    };

    while (!process.waitForFinished(kPollMillis)) {
        flushLines();
        if (m_elapsed.elapsed() > kTotalTimeoutMillis) {
            process.kill();
            process.waitForFinished(kStartTimeoutMillis);
            emit failed(QObject::tr(
                    "Installing the separation environment timed out."));
            return false;
        }
    }
    flushLines();

    if (process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0) {
        emit failed(QObject::tr("%1 failed (exit %2). The last output was:\n%3")
                            .arg(program)
                            .arg(process.exitCode())
                            .arg(tail.join(QChar::LineFeed)));
        return false;
    }
    return true;
}

void StemProvisionJob::run() {
    if (m_provisionScript.isEmpty() || m_environmentDir.isEmpty()) {
        emit failed(QObject::tr(
                "The separation environment cannot be installed because the "
                "installer is not configured."));
        return;
    }
    m_elapsed.start();

    // The installer is compiled into the binary, so write it out beside the
    // environment it is about to create rather than requiring it to be shipped
    // as a file.
    QString reason;
    if (!stemscripts::materialize(
                QFileInfo(m_provisionScript).absolutePath(), &reason)) {
        emit failed(QObject::tr("Could not write the installer: %1")
                            .arg(reason));
        return;
    }
    if (!QFileInfo::exists(m_provisionScript)) {
        emit failed(QObject::tr("Installer script not found: %1")
                            .arg(m_provisionScript));
        return;
    }

    // Something to run the installer with. The configured interpreter is tried
    // first, but on a machine that has never been set up it names the
    // environment's own interpreter, which does not exist yet, so anything on
    // PATH will do.
    QString interpreter = stempython::findInterpreter(m_interpreter);

    if (interpreter.isEmpty()) {
        // No Python at all, which is the normal state of a Windows install.
        // Fetch a stand-alone build using tools the platform already has, and
        // use that as the base of the environment.
        const QString url = stempython::downloadUrl();
        const QString archive =
                QDir(m_environmentDir).filePath(QStringLiteral("python.tar.gz"));
        const QStringList download = stempython::downloadCommand(url, archive);
        const QStringList unpack =
                stempython::unpackCommand(archive, m_environmentDir);
        if (url.isEmpty() || download.isEmpty() || unpack.isEmpty()) {
            emit failed(QObject::tr(
                    "Separating stems needs Python, and this machine does not "
                    "have it. Install Python 3.12 or newer (with pip) and try "
                    "again."));
            return;
        }

        emit outputLine(QObject::tr(
                "No Python on this machine; downloading one to install into "
                "Mixxx's own directory. This is a one-off."));
        if (!runStep(download.first(), download.mid(1))) {
            return;
        }
        if (!runStep(unpack.first(), unpack.mid(1))) {
            return;
        }
        QFile::remove(archive);

        interpreter = stempython::downloadedInterpreter(m_environmentDir);
        if (!QFileInfo(interpreter).isExecutable()) {
            emit failed(QObject::tr(
                    "The downloaded Python is not usable: %1")
                                .arg(interpreter));
            return;
        }
        emit outputLine(QObject::tr("Using the downloaded Python: %1")
                                .arg(interpreter));
    } else {
        emit outputLine(QObject::tr("Creating the environment with %1")
                                .arg(interpreter));
    }

    if (!runStep(interpreter, {m_provisionScript, m_environmentDir})) {
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
