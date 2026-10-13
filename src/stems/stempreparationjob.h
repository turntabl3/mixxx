#pragma once

#include <QAtomicInt>
#include <QObject>
#include <QStringList>
#include <QThread>

#include "preferences/usersettings.h"
#include "stems/stemseparator.h"

class QProgressDialog;
class QWidget;

namespace mixxx {

/// Separates a list of tracks one after another on a worker thread.
///
/// This backs the library-wide "prepare stems" action, which on a real library
/// is an hours-long job: separation takes minutes per track. It is therefore
/// cancellable between tracks, and it reports progress, because a long run that
/// shows no sign of life is indistinguishable from a hung one. A track that
/// fails does not stop the rest; failures are reported once everything has been
/// attempted.
class StemPreparationJob : public QThread {
    Q_OBJECT
  public:
    StemPreparationJob(QObject* pParent,
            const QStringList& trackPaths,
            const StemSeparator::Settings& settings,
            bool replaceOriginal);

    /// Asks the job to stop. It takes effect between tracks: a separation
    /// already running cannot be interrupted, because the backend is a child
    /// process with no cancellation support.
    void cancel();

    void run() override;

  signals:
    /// Emitted before each track, with the number already finished.
    void progress(int done, int total, const QString& trackPath);
    /// A track was separated. `replacedPath` is set when the original was
    /// removed to make room for the container.
    void trackSucceeded(const QString& trackPath,
            const QString& outputPath,
            const QString& replacedPath);
    /// A track could not be separated, or was separated but could not be put in
    /// place. The message is user presentable.
    void trackFailed(const QString& trackPath, const QString& message);
    /// Every track was either done or failed. `cancelled` means the run stopped
    /// early, so `done` is less than `total`.
    void completed(int done, int total, bool cancelled);

  private:
    QStringList m_trackPaths;
    StemSeparator::Settings m_settings;
    bool m_replaceOriginal;
    QAtomicInt m_cancelRequested;
};

/// Lists audio files under the given library root directories that have no
/// container yet. Recursive; an unreadable directory is skipped rather than
/// failing the run.
QStringList findTracksWithoutStems(const QStringList& rootDirs,
        const StemSeparator::Settings& settings);

/// Owns the job and its progress dialog for a library-wide run, reports the
/// outcome when it finishes, and then deletes itself. Callers start a run and
/// forget about it.
class StemPreparationService : public QObject {
    Q_OBJECT
  public:
    StemPreparationService(QWidget* pParent,
            const QStringList& trackPaths,
            const StemSeparator::Settings& settings);
    ~StemPreparationService() override;

    void start();

  private:
    void onCompleted(int done, int total, bool cancelled);

    QStringList m_trackPaths;
    StemSeparator::Settings m_settings;
    QProgressDialog* m_pProgress;
    StemPreparationJob* m_pJob;
    QStringList m_failures;
    int m_failed = 0;
    int m_replaced = 0;
};

/// Asks for confirmation, then separates every track in the library that does
/// not have stems yet, and reports the outcome. Returns immediately: the work
/// happens on a worker thread and the progress dialog is parented to pParent.
void prepareStemsForLibrary(QWidget* pParent,
        const QStringList& rootDirs,
        UserSettingsPointer pConfig);

} // namespace mixxx
