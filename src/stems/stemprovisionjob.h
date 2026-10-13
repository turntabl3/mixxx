#pragma once

#include <QElapsedTimer>
#include <QStringList>
#include <QThread>

namespace mixxx {

/// Creates the Python environment that stem separation runs in.
///
/// res/ provision.py installs PyTorch, so this downloads a few hundred
/// megabytes and takes minutes. It must never run on the GUI thread or the
/// engine thread. Modelled on StemSeparationJob: connect to the signals first,
/// then start(), and delete the job from QThread::finished.
class StemProvisionJob : public QThread {
    Q_OBJECT
  public:
    /// `interpreter` is a Python used only to create the environment, and is
    /// allowed not to exist: this looks for a usable one elsewhere rather than
    /// failing, because on a fresh machine the configured path names an
    /// interpreter that has not been created yet. `environmentDir` is where the
    /// new interpreter ends up.
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
    /// Runs one step of the install, forwarding its output as it arrives.
    /// Emits failed() and returns false when the step does not succeed, so
    /// callers can simply return.
    bool runStep(const QString& program, const QStringList& arguments);

    QString m_interpreter;
    QString m_provisionScript;
    QString m_environmentDir;
    /// Shared by every step, so the time limit covers the install as a whole
    /// rather than each command separately.
    QElapsedTimer m_elapsed;
};

} // namespace mixxx
