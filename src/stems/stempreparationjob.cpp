#include "stems/stempreparationjob.h"

#include <algorithm>
#include <utility>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressDialog>

#include "preferences/stemseparationsettings.h"

#include "moc_stempreparationjob.cpp"

namespace mixxx {

namespace {

/// Suffixes worth separating. A local list rather than a query against the
/// sound sources: this runs over plain files before anything is loaded, and a
/// format demucs cannot read would only fail later anyway. Containers are not
/// in the list, so a prepared track is never offered twice.
const QStringList kAudioSuffixes = {
        QStringLiteral("mp3"),
        QStringLiteral("m4a"),
        QStringLiteral("aac"),
        QStringLiteral("ogg"),
        QStringLiteral("opus"),
        QStringLiteral("flac"),
        QStringLiteral("wav"),
        QStringLiteral("aiff"),
        QStringLiteral("aif"),
        QStringLiteral("wv"),
};

/// How many failing tracks are named in the summary before it stops listing
/// them. A library-wide run can fail on every track -- if the environment was
/// removed after being installed, for instance -- and a dialog with a hundred
/// lines in it helps nobody.
constexpr int kMaxFailuresReported = 5;

QString joinFailures(const QStringList& failures) {
    const QStringList listed = failures.mid(0, kMaxFailuresReported);
    QString text = listed.join(QStringLiteral("\n"));
    if (failures.size() > listed.size()) {
        text += QObject::tr("\n...and %1 more")
                        .arg(failures.size() - listed.size());
    }
    return text;
}

} // namespace

StemPreparationJob::StemPreparationJob(QObject* pParent,
        const QStringList& trackPaths,
        const StemSeparator::Settings& settings,
        bool replaceOriginal)
        : QThread(pParent),
          m_trackPaths(trackPaths),
          m_settings(settings),
          m_replaceOriginal(replaceOriginal) {
}

void StemPreparationJob::cancel() {
    m_cancelRequested.storeRelaxed(1);
}

void StemPreparationJob::run() {
    const StemSeparator separator(m_settings);
    int done = 0;

    for (const QString& trackPath : std::as_const(m_trackPaths)) {
        if (m_cancelRequested.loadRelaxed() != 0) {
            emit completed(done, m_trackPaths.size(), true);
            return;
        }

        emit progress(done, m_trackPaths.size(), trackPath);

        QString outputPath;
        QString error;
        if (!separator.separate(trackPath, &outputPath, &error)) {
            emit trackFailed(trackPath, error);
            ++done;
            continue;
        }

        QString replacedPath;
        if (m_replaceOriginal) {
            // Only ever after the container exists and has been validated: the
            // backend reads the original as its own main mix, so removing it
            // any earlier would delete the source mid-separation.
            if (QFile::moveToTrash(trackPath)) {
                replacedPath = trackPath;
            } else {
                // The separation worked, so report this but keep the container.
                emit trackFailed(trackPath,
                        tr("Separated into %1, but the original could not be "
                           "removed.")
                                .arg(outputPath));
                ++done;
                continue;
            }
        }

        emit trackSucceeded(trackPath, outputPath, replacedPath);
        ++done;
    }

    emit completed(done, m_trackPaths.size(), false);
}

QStringList findTracksWithoutStems(const QStringList& rootDirs,
        const StemSeparator::Settings& settings) {
    const StemSeparator separator(settings);
    QStringList tracks;
    for (const QString& root : rootDirs) {
        if (root.isEmpty() || !QDir(root).exists()) {
            continue;
        }
        QDirIterator it(root,
                QDir::Files | QDir::Readable | QDir::NoSymLinks,
                QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!kAudioSuffixes.contains(QFileInfo(path).suffix().toLower())) {
                continue;
            }
            if (!separator.hasContainer(path)) {
                tracks.append(path);
            }
        }
    }
    // A stable order, so an interrupted run is resumed in the same sequence and
    // the progress dialog does not jump around.
    std::sort(tracks.begin(), tracks.end());
    return tracks;
}

StemPreparationService::StemPreparationService(QWidget* pParent,
        const QStringList& trackPaths,
        const StemSeparator::Settings& settings)
        : m_trackPaths(trackPaths),
          m_settings(settings),
          m_pProgress(new QProgressDialog(
                  tr("Preparing stems..."),
                  tr("Stop"),
                  0,
                  trackPaths.size(),
                  pParent)),
          m_pJob(new StemPreparationJob(this, trackPaths, settings, true)) {
    m_pProgress->setWindowTitle(tr("Preparing Stems"));
    m_pProgress->setWindowModality(Qt::WindowModal);
    m_pProgress->setMinimumDuration(0);
    // Cancelling cannot interrupt a separation already running, so the dialog
    // stays up after the request until the current track finishes.
    m_pProgress->setAutoClose(false);
    m_pProgress->setAutoReset(false);
    m_pProgress->setValue(0);

    connect(m_pProgress,
            &QProgressDialog::canceled,
            this,
            [this] {
                m_pJob->cancel();
                m_pProgress->setLabelText(
                        tr("Stopping after the current track..."));
            });

    connect(m_pJob,
            &StemPreparationJob::progress,
            this,
            [this](int done, int total, const QString& trackPath) {
                m_pProgress->setLabelText(
                        tr("%1 of %2: %3")
                                .arg(done + 1)
                                .arg(total)
                                .arg(QFileInfo(trackPath).fileName()));
                m_pProgress->setValue(done);
            });
    connect(m_pJob,
            &StemPreparationJob::trackSucceeded,
            this,
            [this](const QString&, const QString&, const QString& replacedPath) {
                if (!replacedPath.isEmpty()) {
                    ++m_replaced;
                }
            });
    connect(m_pJob,
            &StemPreparationJob::trackFailed,
            this,
            [this](const QString& trackPath, const QString& message) {
                ++m_failed;
                m_failures.append(tr("%1: %2")
                                          .arg(QFileInfo(trackPath).fileName(),
                                                  message));
            });
    connect(m_pJob,
            &StemPreparationJob::completed,
            this,
            &StemPreparationService::onCompleted);
}

StemPreparationService::~StemPreparationService() {
    // Destroying a running QThread terminates the process, and the run can be
    // hours long, so ask it to stop first. This only happens when the window is
    // closing under a run that is still going.
    if (m_pJob->isRunning()) {
        m_pJob->cancel();
        m_pJob->wait();
    }
}

void StemPreparationService::start() {
    m_pProgress->show();
    m_pJob->start();
}

void StemPreparationService::onCompleted(int done, int total, bool cancelled) {
    m_pProgress->close();

    const QString paragraphBreak = QStringLiteral("\n\n");
    QString summary;
    if (cancelled) {
        summary = tr("Stopped after %1 of %2 tracks.").arg(done).arg(total);
    } else {
        summary = tr("Processed %1 tracks.").arg(total);
    }
    if (m_replaced > 0) {
        summary += paragraphBreak +
                tr("%1 original files were moved to the trash; their stem "
                   "containers took their place.")
                        .arg(m_replaced);
    }
    if (m_failed > 0) {
        summary += paragraphBreak +
                tr("%1 tracks could not be prepared:").arg(m_failed) +
                QChar::LineFeed + joinFailures(m_failures);
    }
    if (m_replaced > 0) {
        // The library still points at the files that were removed.
        summary += paragraphBreak +
                tr("Use Library > Rescan Library so the library picks up the "
                   "changes.");
    }

    if (m_failed > 0) {
        QMessageBox::warning(nullptr, tr("Preparing Stems"), summary);
    } else {
        QMessageBox::information(nullptr, tr("Preparing Stems"), summary);
    }

    // The job (a child) has finished by the time this is called, so deleting
    // both is safe.
    deleteLater();
}

void prepareStemsForLibrary(QWidget* pParent,
        const QStringList& rootDirs,
        UserSettingsPointer pConfig) {
    const StemSeparationSettings stemSettings(pConfig);

    StemSeparator::Settings settings;
    settings.pythonPath = stemSettings.getPythonPath();
    settings.scriptPath = stemSettings.getScriptPath();
    settings.model = stemSettings.getModel();
    settings.outputDir = stemSettings.getOutputDir();

    if (!stemSettings.isConfigured()) {
        QMessageBox::information(pParent,
                QObject::tr("Stem Separation Is Not Set Up"),
                QObject::tr("Stem separation is disabled, or its program is not "
                   "installed. Set it up in Preferences > Stems first."));
        return;
    }

    const QStringList tracks = findTracksWithoutStems(rootDirs, settings);
    if (tracks.isEmpty()) {
        QMessageBox::information(pParent,
                QObject::tr("Nothing To Prepare"),
                QObject::tr("Every track in the library already has stems."));
        return;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(pParent,
            QObject::tr("Prepare Stems"),
            QObject::tr("Create stems for %n track(s)?\n\n"
               "This takes several minutes per track, so it can run for hours. "
               "Tracks keep playing normally while it runs, and it can be "
               "stopped between tracks.\n\n"
               "The original files are moved to the trash and the stem "
               "containers take their place.",
                    "",
                    tracks.size()),
            QMessageBox::Ok | QMessageBox::Cancel,
            QMessageBox::Cancel);
    if (answer != QMessageBox::Ok) {
        return;
    }

    auto* pService = new StemPreparationService(pParent, tracks, settings);
    pService->start();
}

} // namespace mixxx
