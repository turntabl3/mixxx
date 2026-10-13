#pragma once

#include <utility>

#include <QDir>
#include <QFileInfo>

#include "preferences/usersettings.h"
#include "stems/stemscripts.h"
#include "util/logger.h"

#define STEM_SEPARATION_CONFIG_KEY "[StemSeparation]"

// Preferences for the offline stem separation backend. Separation is done by
// an external process (res/stems/separate.py) rather than linked into Mixxx,
// so the interpreter and script locations are user settings.
//
// Both have working defaults: the script ships with the application, and the
// interpreter points at the environment Mixxx provisions for itself under its
// settings directory (see res/stems/provision.py). A fresh installation
// therefore needs no hand configuration, only the one-off environment install
// offered on the preferences page.
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

    // Interpreter that has demucs installed. Defaults to the environment in
    // Mixxx's settings directory rather than to an empty string, so the only
    // thing left to do on a fresh installation is to create that environment.
    QString getPythonPath() const {
        return resolvePath(STEM_SEPARATION_PYTHON_PATH, getPythonPathDefault());
    }

    QString getPythonPathDefault() const {
#if defined(Q_OS_WIN)
        const QString interpreter = QStringLiteral("stems/venv/Scripts/python.exe");
#else
        const QString interpreter = QStringLiteral("stems/venv/bin/python");
#endif
        return QDir(m_pConfig->getSettingsPath()).filePath(interpreter);
    }

    void setPythonPathToDefault() {
        setPythonPath(getPythonPathDefault());
    }

    void setPythonPath(const QString& value) {
        m_pConfig->setValue(
                ConfigKey(STEM_SEPARATION_CONFIG_KEY, STEM_SEPARATION_PYTHON_PATH),
                value);
    }

    // The separation script, which ships with the application's resources.
    QString getScriptPath() const {
        return resolvePath(STEM_SEPARATION_SCRIPT_PATH, getScriptPathDefault());
    }

    QString getScriptPathDefault() const {
        // The script is compiled into the binary and written out beside the
        // interpreter environment the first time it is needed, so the default
        // lives in Mixxx's own directory rather than in a resource directory
        // that may not exist or may hold a stale copy.
        return QDir(getEnvironmentDir())
                .filePath(mixxx::stemscripts::separationScriptName());
    }

    void setScriptPathToDefault() {
        setScriptPath(getScriptPathDefault());
    }

    void setScriptPath(const QString& value) {
        m_pConfig->setValue(
                ConfigKey(STEM_SEPARATION_CONFIG_KEY, STEM_SEPARATION_SCRIPT_PATH),
                value);
    }

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

    // True when the backend is enabled and its interpreter exists. The script
    // is compiled into the binary, so unlike a shipped file it cannot go
    // missing; the interpreter is the part that has to be present on this
    // machine. Callers use this to avoid offering a job that can only fail. A
    // path the user pointed somewhere else on purpose is not checked here; the
    // separator itself reports that by name.
    bool isConfigured() const {
        return getEnabled() && QFileInfo::exists(getPythonPath());
    }

    // Directory holding the environment and anything else the backend needs.
    // Also the right place for a provisioning job to write into.
    QString getEnvironmentDir() const {
        return QDir(m_pConfig->getSettingsPath())
                .filePath(QStringLiteral("stems"));
    }

    // Path of the script that creates the environment. Compiled into the
    // binary like the separation script, and written out beside it before a
    // provisioning run.
    QString getProvisionScriptPath() const {
        return QDir(getEnvironmentDir())
                .filePath(mixxx::stemscripts::provisionScriptName());
    }

  private:
    // A configured path is only meaningful while it still points at something.
    // A stored value that has since gone missing is invisible until the
    // preferences page is opened, and until then it leaves the feature failing
    // with "not found" even though the bundled default would have worked --
    // which is exactly what happens when the pipeline is moved, or when the
    // interpreter lives on a drive that is not mounted right now. Fall back to
    // the default instead, and let the preferences page show the path actually
    // in use, so applying the page corrects the stale value.
    QString resolvePath(const QString& key, const QString& defaultValue) const {
        const QString stored = m_pConfig->getValue<QString>(
                ConfigKey(STEM_SEPARATION_CONFIG_KEY, key), QString());
        if (stored.isEmpty()) {
            return defaultValue;
        }
        if (QFileInfo::exists(stored)) {
            return stored;
        }
        // Deliberately silent: a header cannot log (Mixxx defines kLogger per
        // translation unit), and the preferences page shows the path actually
        // in use, so the stale value is visible there.
        return defaultValue;
    }

    UserSettingsPointer m_pConfig;
};
