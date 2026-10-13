#pragma once

#include <QString>
#include <QStringList>

namespace mixxx {

/// Runs an external separation backend (res/stems/separate.py) over a track
/// and returns the path of the produced NI Stems container.
///
/// Separation is slow (roughly 0.4x realtime on a laptop CPU), so callers must
/// run this off the engine thread -- it blocks on a child process. Nothing here
/// touches the audio path; the deck keeps playing the original track until the
/// container exists and is loaded.
class StemSeparator {
  public:
    struct Settings {
        /// Interpreter that has demucs installed, e.g. ~/.venvs/stems/bin/python.
        QString pythonPath;
        /// Path to res/stems/separate.py.
        QString scriptPath;
        /// Directory for the produced container. Empty means "beside the track".
        QString outputDir;
        /// Separation model, e.g. "htdemucs". Only four-stem models are usable
        /// because Mixxx supports at most kMaxSupportedStems stems. Empty means
        /// the backend's own default.
        QString model;
        /// Give up after this long. Model load plus separation of a ten minute
        /// track is minutes, not seconds, even on a fast CPU.
        int timeoutMillis = 45 * 60 * 1000;
    };

    explicit StemSeparator(Settings settings);

    /// Blocking. On success writes the container path to pOutputPath.
    /// On failure writes a human readable reason to pError.
    bool separate(const QString& trackPath,
            QString* pOutputPath,
            QString* pError) const;

    /// Where the container for a track is expected to be. Mirrors the naming
    /// the backend applies, so callers can tell whether a track has already
    /// been separated without running anything.
    static QString containerPathFor(const QString& trackPath, const Settings& settings);

    /// True when there is already a container for this track. A track that is
    /// itself a container counts, so containers are never separated again.
    bool hasContainer(const QString& trackPath) const;

  private:
    Settings m_settings;
};

} // namespace mixxx
