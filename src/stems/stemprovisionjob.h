#pragma once

#include <QThread>

namespace mixxx {

/// Creates the Python environment that stem separation runs in.
///
/// Res/ provision.py installs PyTorch, so this downloads a few hundred
/// megabytes and takes minutes. It must never run on the GUI thread or the
/// engine thread. Modelled on StemSeparationJob: connect to the signals first,
/// then start(), and delete the job from QThread::finished.
class StemProvisionJob : public QThread {
    Q_OBJECT
  public:
    /// `interpreter` is a system Python used only to create the environment;
    /// `environmentDir` is where the new interpreter ends up. Neither is the
    /// interpreter Mixxx will separate with -- that one does not exist yet.
    StemProvisionJob(QObject* pParent,
            const QString& interpreter,
            const QString& provisionScript,
            const QString& environmentDir);

    void run() override;

  signals:
    /// One line of installer output, so the caller can show progress. pip is
    /// quiet for long stretches while it downloads, so do not treat a gap as
    /// a stall.
    void outputLine(const QString& line);
    /// The environment exists and demucs can be imported from it.
    void completed(const QString& interpreterPath);
    /// Provisioning failed. The message is user presentable.
    void failed(const QString& message);

  private:
    QString m_interpreter;
    QString m_provisionScript;
    QString m_environmentDir;
};

} // namespace mixxx
