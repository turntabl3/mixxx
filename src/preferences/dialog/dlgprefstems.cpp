#include "preferences/dialog/dlgprefstems.h"

#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QThread>

#include "stems/stemprovisionjob.h"
#include "stems/stemscripts.h"

#include "moc_dlgprefstems.cpp"

namespace {

/// Where a browse dialog should open: alongside the current value if it points
/// somewhere real, otherwise the home directory. Config values survive moving
/// machines and deleting the venv, so the stored path is often stale.
QString startDirectory(const QString& current) {
    if (!current.isEmpty()) {
        const QFileInfo info(current);
        if (info.exists()) {
            return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
        }
    }
    return QDir::homePath();
}

/// Interpreter used only to bootstrap the environment. Separation never runs
/// under this one; it runs under the interpreter that comes out of it.
QString systemInterpreter() {
#if defined(Q_OS_WIN)
    return QStringLiteral("python");
#else
    return QStringLiteral("python3");
#endif
}

} // namespace

DlgPrefStems::DlgPrefStems(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_settings(pConfig) {
    setupUi(this);

    connect(checkBoxEnabled,
            &QCheckBox::toggled,
            this,
            &DlgPrefStems::slotInputChanged);
    connect(lineEditPythonPath,
            &QLineEdit::textChanged,
            this,
            &DlgPrefStems::slotInputChanged);
    connect(lineEditScriptPath,
            &QLineEdit::textChanged,
            this,
            &DlgPrefStems::slotInputChanged);
    connect(lineEditOutputDir,
            &QLineEdit::textChanged,
            this,
            &DlgPrefStems::slotInputChanged);
    connect(comboBoxModel,
            &QComboBox::currentTextChanged,
            this,
            &DlgPrefStems::slotInputChanged);

    connect(pushButtonBrowsePython,
            &QPushButton::clicked,
            this,
            &DlgPrefStems::slotBrowsePython);
    connect(pushButtonBrowseScript,
            &QPushButton::clicked,
            this,
            &DlgPrefStems::slotBrowseScript);
    connect(pushButtonBrowseOutputDir,
            &QPushButton::clicked,
            this,
            &DlgPrefStems::slotBrowseOutputDir);
    connect(pushButtonInstallEnvironment,
            &QPushButton::clicked,
            this,
            &DlgPrefStems::slotInstallEnvironment);

    setScrollSafeGuardForAllInputWidgets(this);

    // The separation script is compiled into the binary rather than shipped as
    // a file, so write it out before the page reports on it. Without this the
    // page would call a working installation unconfigured, because the path it
    // shows would not exist until something tried to separate a track.
    QString scriptError;
    if (!mixxx::stemscripts::materialize(
                QFileInfo(m_settings.getScriptPath()).absolutePath(),
                &scriptError)) {
        qWarning() << "stems: could not write the separation scripts:"
                   << scriptError;
    }

    slotUpdate();
}

DlgPrefStems::~DlgPrefStems() {
}

void DlgPrefStems::slotUpdate() {
    checkBoxEnabled->setChecked(m_settings.getEnabled());
    lineEditPythonPath->setText(m_settings.getPythonPath());
    lineEditScriptPath->setText(m_settings.getScriptPath());
    comboBoxModel->setCurrentText(m_settings.getModel());
    lineEditOutputDir->setText(m_settings.getOutputDir());
    updateStatus();
}

void DlgPrefStems::slotApply() {
    m_settings.setEnabled(checkBoxEnabled->isChecked());
    m_settings.setPythonPath(lineEditPythonPath->text().trimmed());
    m_settings.setScriptPath(lineEditScriptPath->text().trimmed());
    m_settings.setModel(comboBoxModel->currentText().trimmed());
    m_settings.setOutputDir(lineEditOutputDir->text().trimmed());
    updateStatus();
}

void DlgPrefStems::slotResetToDefaults() {
    checkBoxEnabled->setChecked(m_settings.getEnabledDefault());
    lineEditPythonPath->setText(m_settings.getPythonPathDefault());
    lineEditScriptPath->setText(m_settings.getScriptPathDefault());
    comboBoxModel->setCurrentText(m_settings.getModelDefault());
    lineEditOutputDir->setText(m_settings.getOutputDirDefault());
    updateStatus();
}

void DlgPrefStems::slotInputChanged() {
    updateStatus();
}

void DlgPrefStems::updateStatus() {
    const bool enabled = checkBoxEnabled->isChecked();
    const QList<QWidget*> dependents = {
            labelPython,
            lineEditPythonPath,
            pushButtonBrowsePython,
            labelScript,
            lineEditScriptPath,
            pushButtonBrowseScript,
            labelModel,
            comboBoxModel,
            labelOutputDir,
            lineEditOutputDir,
            pushButtonBrowseOutputDir,
    };
    for (QWidget* pWidget : dependents) {
        pWidget->setEnabled(enabled);
    }

    if (!enabled) {
        labelStatus->setText(tr("Stem separation is disabled."));
        return;
    }

    const QString pythonPath = lineEditPythonPath->text().trimmed();
    const QString scriptPath = lineEditScriptPath->text().trimmed();
    if (pythonPath.isEmpty() || scriptPath.isEmpty()) {
        labelStatus->setText(tr("Point Mixxx at the separation program and the "
                                "separation script to enable separating tracks."));
        return;
    }
    // Checked here rather than only at separation time: the paths are the two
    // things that are commonly wrong, and finding out from a failed background
    // job after the dialog has closed is worse.
    if (!QFileInfo::exists(scriptPath)) {
        labelStatus->setText(tr("Separation script not found: %1").arg(scriptPath));
        return;
    }
    if (!QFileInfo::exists(pythonPath)) {
        // The normal state on a fresh installation: the interpreter path is the
        // default one, but no environment has been created there yet.
        labelStatus->setText(tr("The separation environment is not installed "
                                "yet. Use \"Install separation environment\" "
                                "above, or point the program at an interpreter "
                                "that has demucs."));
        return;
    }

    labelStatus->setText(tr("Ready. Separating a track runs in the background and "
                            "takes a few times the length of the track, so the deck "
                            "keeps playing the original until it finishes."));
}

void DlgPrefStems::slotInstallEnvironment() {
    if (m_pProvisionJob != nullptr) {
        return;
    }

    pushButtonInstallEnvironment->setEnabled(false);
    labelStatus->setText(tr("Installing the separation environment. This "
                            "downloads PyTorch and takes a few minutes."));

    auto* pJob = new mixxx::StemProvisionJob(nullptr,
            systemInterpreter(),
            m_settings.getProvisionScriptPath(),
            m_settings.getEnvironmentDir());
    m_pProvisionJob = pJob;

    // The installer prints pip's output, so showing the last line is enough to
    // tell a download in progress from a stall.
    connect(pJob,
            &mixxx::StemProvisionJob::outputLine,
            this,
            [this](const QString& line) {
                labelStatus->setText(line);
            });
    connect(pJob,
            &mixxx::StemProvisionJob::completed,
            this,
            [this](const QString& interpreterPath) {
                // Store the interpreter that was just created. It is normally
                // the default path already, but writing it explicitly means a
                // user who had pointed at something else ends up with settings
                // that match the environment on disk.
                lineEditPythonPath->setText(interpreterPath);
                m_settings.setPythonPath(interpreterPath);
                pushButtonInstallEnvironment->setEnabled(true);
                updateStatus();
                QMessageBox::information(this,
                        tr("Separation Environment Installed"),
                        tr("%1 is ready. Tracks can now be separated.")
                                .arg(interpreterPath));
            });
    connect(pJob,
            &mixxx::StemProvisionJob::failed,
            this,
            [this](const QString& message) {
                pushButtonInstallEnvironment->setEnabled(true);
                updateStatus();
                QMessageBox::warning(this,
                        tr("Could Not Install Separation Environment"),
                        message);
            });
    // Neither the job nor its dialog survives the menu that started it, so the
    // job deletes itself and is not owned by the page.
    connect(pJob, &QThread::finished, this, [this] { m_pProvisionJob = nullptr; });
    connect(pJob, &QThread::finished, pJob, &QObject::deleteLater);
    pJob->start();
}

void DlgPrefStems::slotBrowsePython() {
    const QString path = QFileDialog::getOpenFileName(this,
            tr("Select the separation program"),
            startDirectory(lineEditPythonPath->text()));
    if (!path.isEmpty()) {
        lineEditPythonPath->setText(path);
    }
}

void DlgPrefStems::slotBrowseScript() {
    const QString path = QFileDialog::getOpenFileName(this,
            tr("Select the separation script"),
            startDirectory(lineEditScriptPath->text()));
    if (!path.isEmpty()) {
        lineEditScriptPath->setText(path);
    }
}

void DlgPrefStems::slotBrowseOutputDir() {
    const QString path = QFileDialog::getExistingDirectory(this,
            tr("Select where separated stems are stored"),
            startDirectory(lineEditOutputDir->text()));
    if (!path.isEmpty()) {
        lineEditOutputDir->setText(path);
    }
}
