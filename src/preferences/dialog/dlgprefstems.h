#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgprefstemsdlg.h"
#include "preferences/stemseparationsettings.h"
#include "preferences/usersettings.h"

class DlgPrefStems : public DlgPreferencePage, public Ui::DlgPrefStemsDlg {
    Q_OBJECT
  public:
    DlgPrefStems(QWidget* parent, UserSettingsPointer pConfig);
    ~DlgPrefStems() override;

  public slots:
    /// Called when the preference dialog is shown. Reads the settings into the
    /// widgets.
    void slotUpdate() override;
    /// Called when the user clicks "Apply". Writes the widgets into the
    /// settings and reports whether the backend is usable.
    void slotApply() override;
    /// Called when the user clicks "Reset to Defaults". Resets the widgets
    /// only; the values are saved on Apply, like the other preference pages.
    void slotResetToDefaults() override;

  private slots:
    void slotBrowsePython();
    void slotBrowseScript();
    void slotBrowseOutputDir();
    /// Named without the QWidget signal argument so it can be connected to
    /// both toggled(bool) and textChanged(const QString&).
    void slotInputChanged();

  private:
    void updateStatus();

    StemSeparationSettings m_settings;
};
