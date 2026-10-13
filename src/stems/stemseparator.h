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

  private:
    Settings m_settings;
};

} // namespace mixxx
