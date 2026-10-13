#include "preferences/dialog/dlgprefstems.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>

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

    setScrollSafeGuardForAllInputWidgets(this);

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
    for (const QString& path : {pythonPath, scriptPath}) {
        if (!QFileInfo::exists(path)) {
            labelStatus->setText(tr("Not found: %1").arg(path));
            return;
        }
    }

    labelStatus->setText(tr("Ready. Separating a track runs in the background and "
                            "takes a few times the length of the track, so the deck "
                            "keeps playing the original until it finishes."));
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
