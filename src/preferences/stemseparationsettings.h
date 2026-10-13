#pragma once

#include <utility>

#include "preferences/usersettings.h"

#define STEM_SEPARATION_CONFIG_KEY "[StemSeparation]"

// Preferences for the offline stem separation backend. Separation is done by
// an external process (tools/stems/separate.py) rather than linked into Mixxx,
// so the interpreter and script locations are user settings.
#define STEM_SEPARATION_ENABLED "Enabled"
#define STEM_SEPARATION_PYTHON_PATH "PythonPath"
#define STEM_SEPARATION_SCRIPT_PATH "ScriptPath"
#define STEM_SEPARATION_MODEL "Model"
#define STEM_SEPARATION_OUTPUT_DIR "OutputDir"

class StemSeparationSettings {
  public:
    explicit StemSeparationSettings(UserSettingsPointer pConfig)
            : m_pConfig(std::move(pConfig)) {
    }

    // Mixxx never starts a separation on its own; the user asks for it per
    // track. This gate only decides whether the action is offered at all.
    DEFINE_PREFERENCE_HELPERS(Enabled,
            bool,
            STEM_SEPARATION_CONFIG_KEY,
            STEM_SEPARATION_ENABLED,
            false);

    // Interpreter that has demucs installed, e.g. ~/.venvs/stems/bin/python.
    // Left empty by default: there is no way to guess where the user put it.
    DEFINE_PREFERENCE_HELPERS(PythonPath,
            QString,
            STEM_SEPARATION_CONFIG_KEY,
            STEM_SEPARATION_PYTHON_PATH,
            QString());

    // Path to tools/stems/separate.py.
    DEFINE_PREFERENCE_HELPERS(ScriptPath,
            QString,
            STEM_SEPARATION_CONFIG_KEY,
            STEM_SEPARATION_SCRIPT_PATH,
            QString());

    DEFINE_PREFERENCE_HELPERS(Model,
            QString,
            STEM_SEPARATION_CONFIG_KEY,
            STEM_SEPARATION_MODEL,
            QStringLiteral("htdemucs"));

    // Where separated containers are cached. Empty means beside the track,
    // which is what you want for a portable library.
    DEFINE_PREFERENCE_HELPERS(OutputDir,
            QString,
            STEM_SEPARATION_CONFIG_KEY,
            STEM_SEPARATION_OUTPUT_DIR,
            QString());

    // True when the backend has been pointed at something usable. Callers use
    // this to decide whether the "Separate stems" action is offered, rather
    // than starting a job that can only fail.
    bool isConfigured() const {
        return getEnabled() &&
                !getPythonPath().isEmpty() &&
                !getScriptPath().isEmpty();
    }

  private:
    UserSettingsPointer m_pConfig;
};
