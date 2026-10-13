#pragma once

#include <QThread>

#include "stems/stemseparator.h"

namespace mixxx {

/// Runs a stem separation on a worker thread.
///
/// Separation takes minutes (roughly 0.4x realtime on a laptop CPU) and blocks
/// on a child process, so it must never run on the engine thread or on the GUI
/// thread. The deck keeps playing the original track while this runs; nothing
/// here touches the audio path.
///
/// Owned by the caller, like EnginePrimeExportJob: connect to completed() and
/// failed() first, then start(), and delete the job from QThread::finished.
class StemSeparationJob : public QThread {
    Q_OBJECT
  public:
    StemSeparationJob(QObject* pParent,
            const QString& trackPath,
            const StemSeparator::Settings& settings);

    void run() override;

  signals:
    /// The container was written and validated by StemSeparator.
    void completed(const QString& trackPath, const QString& outputPath);
    /// Separation failed. Both arguments are user presentable.
    void failed(const QString& trackPath, const QString& message);

  private:
    QString m_trackPath;
    StemSeparator::Settings m_settings;
};

} // namespace mixxx
