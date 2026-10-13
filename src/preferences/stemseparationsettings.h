#pragma once

#include <utility>

#include <QDir>
#include <QFileInfo>

#include "preferences/usersettings.h"

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
        return m_pConfig->getValue<QString>(
                ConfigKey(STEM_SEPARATION_CONFIG_KEY, STEM_SEPARATION_PYTHON_PATH),
                getPythonPathDefault());
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
        return m_pConfig->getValue<QString>(
                ConfigKey(STEM_SEPARATION_CONFIG_KEY, STEM_SEPARATION_SCRIPT_PATH),
                getScriptPathDefault());
    }

    QString getScriptPathDefault() const {
        return QDir(m_pConfig->getResourcePath())
                .filePath(QStringLiteral("stems/separate.py"));
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

    // True when the backend is enabled and both paths exist. The paths have
    // defaults now, so it is existence that matters rather than emptiness: a
    // fresh installation has usable paths but no environment behind them yet,
    // and callers use this to avoid offering a job that can only fail.
    bool isConfigured() const {
        return getEnabled() &&
                QFileInfo::exists(getPythonPath()) &&
                QFileInfo::exists(getScriptPath());
    }

    // Directory holding the environment and anything else the backend needs.
    // Also the right place for a provisioning job to write into.
    QString getEnvironmentDir() const {
        return QDir(m_pConfig->getSettingsPath())
                .filePath(QStringLiteral("stems"));
    }

    // Path of the script that creates the environment, resolved against
    // Mixxx's bundled resources next to the separation script itself.
    QString getProvisionScriptPath() const {
        return QDir(m_pConfig->getResourcePath())
                .filePath(QStringLiteral("stems/provision.py"));
    }

  private:
    UserSettingsPointer m_pConfig;
};
