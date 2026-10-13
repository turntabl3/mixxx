#include "stems/stemseparator.h"
#include "stems/stemscripts.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <utility>

#include "track/steminfoimporter.h"
#include "util/logger.h"

namespace mixxx {

namespace {

const Logger kLogger("StemSeparator");

constexpr int kStartTimeoutMillis = 5000;

/// demucs writes its progress bars to stderr, so stdout carries the final
/// container path printed by separate.py. Take the last line that looks like
/// one, so stray output ahead of it cannot be mistaken for the result.
QString lastStemPath(const QString& stdoutText) {
    QString outputPath;
    const auto lines = stdoutText.split(QChar('\n'), Qt::SkipEmptyParts);
    for (const auto& line : lines) {
        const QString candidate = line.trimmed();
        if (candidate.endsWith(QStringLiteral(".stem.mp4"), Qt::CaseInsensitive)) {
            outputPath = candidate;
        }
    }
    return outputPath;
}

QString stderrTail(const QString& stderrText) {
    const QString trimmed = stderrText.trimmed();
    constexpr int kMaxChars = 400;
    if (trimmed.size() <= kMaxChars) {
        return trimmed;
    }
    return QStringLiteral("...") + trimmed.right(kMaxChars);
}

} // namespace

StemSeparator::StemSeparator(Settings settings)
        : m_settings(std::move(settings)) {
}

bool StemSeparator::separate(const QString& trackPath,
        QString* pOutputPath,
        QString* pError) const {
    VERIFY_OR_DEBUG_ASSERT(pOutputPath != nullptr && pError != nullptr) {
        return false;
    }
    pOutputPath->clear();
    pError->clear();

    // A misconfigured backend is the common failure, so name the offending path
    // rather than reporting a generic child-process error.
    if (!QFileInfo::exists(m_settings.pythonPath)) {
        *pError = QStringLiteral("separation interpreter not found: %1")
                          .arg(m_settings.pythonPath);
        return false;
    }
    if (!QFileInfo::exists(m_settings.scriptPath)) {
        // The pipeline is compiled into the binary rather than shipped as
        // files, so write it out where the settings say the script lives and
        // look again. This is what makes an installation of Mixxx alone enough
        // to separate a track.
        if (!stemscripts::materialize(
                    QFileInfo(m_settings.scriptPath).absolutePath(), pError)) {
            return false;
        }
    }
    if (!QFileInfo::exists(m_settings.scriptPath)) {
        *pError = QStringLiteral("separation script not found: %1")
                          .arg(m_settings.scriptPath);
        return false;
    }
    if (!QFileInfo::exists(trackPath)) {
        *pError = QStringLiteral("track not found: %1").arg(trackPath);
        return false;
    }

    QStringList args{m_settings.scriptPath, trackPath};
    if (!m_settings.outputDir.isEmpty()) {
        if (!QDir().mkpath(m_settings.outputDir)) {
            *pError = QStringLiteral("cannot create output directory: %1")
                              .arg(m_settings.outputDir);
            return false;
        }
        args.append(containerPathFor(trackPath, m_settings));
    }
    if (!m_settings.model.isEmpty()) {
        // separate.py takes --model so the position of the output path does not
        // depend on whether a model was configured.
        args.append(QStringLiteral("--model"));
        args.append(m_settings.model);
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(m_settings.pythonPath, args);
    if (!process.waitForStarted(kStartTimeoutMillis)) {
        *pError = QStringLiteral("failed to start %1: %2")
                          .arg(m_settings.pythonPath, process.errorString());
        return false;
    }
    if (!process.waitForFinished(m_settings.timeoutMillis)) {
        process.kill();
        process.waitForFinished(kStartTimeoutMillis);
        *pError = QStringLiteral("separation timed out after %1 ms")
                          .arg(m_settings.timeoutMillis);
        return false;
    }

    const QString stdoutText = QString::fromUtf8(process.readAllStandardOutput());
    const QString stderrText = QString::fromUtf8(process.readAllStandardError());

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *pError = QStringLiteral("separation failed (exit %1): %2")
                          .arg(process.exitCode())
                          .arg(stderrTail(stderrText));
        return false;
    }

    const QString outputPath = lastStemPath(stdoutText);
    if (outputPath.isEmpty()) {
        *pError = QStringLiteral(
                "separation produced no container path in its output");
        return false;
    }

    const QString absoluteOutputPath = QFileInfo(outputPath).absoluteFilePath();
    // Trust the file only after Mixxx's own importer agrees it is a stem file;
    // a truncated or mis-muxed container would otherwise be loaded as a single
    // stereo track and lose the stems silently.
    if (!QFileInfo::exists(absoluteOutputPath) ||
            !StemInfoImporter::hasStemAtom(absoluteOutputPath)) {
        *pError = QStringLiteral(
                "separation output is not a readable stem container: %1")
                          .arg(absoluteOutputPath);
        return false;
    }

    kLogger.info() << "separated" << trackPath << "into" << absoluteOutputPath;
    *pOutputPath = absoluteOutputPath;
    return true;
}

QString StemSeparator::containerPathFor(const QString& trackPath, const Settings& settings) {
    const QString name = QFileInfo(trackPath).completeBaseName() +
            QStringLiteral(".stem.mp4");
    if (settings.outputDir.isEmpty()) {
        // The backend defaults to writing beside the track.
        return QFileInfo(trackPath).dir().filePath(name);
    }
    return QDir(settings.outputDir).filePath(name);
}

bool StemSeparator::hasContainer(const QString& trackPath) const {
    // A container is not separated again: completeBaseName() would turn
    // "song.stem.mp4" into "song.stem.stem.mp4", which is never what is meant.
    if (trackPath.endsWith(QStringLiteral(".stem.mp4"), Qt::CaseInsensitive)) {
        return true;
    }
    return QFileInfo::exists(containerPathFor(trackPath, m_settings));
}

} // namespace mixxx
