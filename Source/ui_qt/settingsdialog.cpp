#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "PS2VM_Preferences.h"
#include "PreferenceDefs.h"
#include "../gs/GSH_OpenGL/GSH_OpenGL.h"
#include <cassert>
#include <cmath>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
	ui->setupUi(this);

	//Not needed, as it can be set in the ui editor, but left for ease of ui edit.
	ui->stackedWidget->setCurrentIndex(0);

	ui->comboBox_gs_selection->blockSignals(true);
	ui->comboBox_gs_selection->insertItem(SettingsDialog::GS_HANDLERS::OPENGL, "OpenGL");
#ifdef HAS_GSH_VULKAN
	ui->comboBox_gs_selection->insertItem(SettingsDialog::GS_HANDLERS::VULKAN, "Vulkan");
#else
	ui->gs_option_widget->hide();
#endif
	ui->comboBox_gs_selection->blockSignals(false);

	// this assert is to ensure no one adds an item to the combobox through qt creator by accident
	assert(ui->comboBox_gs_selection->count() == SettingsDialog::GS_HANDLERS::MAX_HANDLER);

	LoadPreferences();
	connect(ui->listWidget, &QListWidget::currentItemChanged, this, &SettingsDialog::changePage);
}

SettingsDialog::~SettingsDialog()
{
	CAppConfig::GetInstance().Save();
	delete ui;
}

void SettingsDialog::changePage(QListWidgetItem* current, QListWidgetItem* previous)
{
	if(!current)
		current = previous;

	ui->stackedWidget->setCurrentIndex(ui->listWidget->row(current));
}

void SettingsDialog::LoadPreferences()
{
	int factor = CAppConfig::GetInstance().GetPreferenceInteger(PREF_CGSH_OPENGL_RESOLUTION_FACTOR);
	int factor_index = std::log2(factor);
	ui->comboBox_res_multiplyer->setCurrentIndex(factor_index);
	ui->checkBox_force_bilinear_filtering->setChecked(CAppConfig::GetInstance().GetPreferenceBoolean(PREF_CGSH_OPENGL_FORCEBILINEARTEXTURES));
	ui->comboBox_gs_selection->setCurrentIndex(CAppConfig::GetInstance().GetPreferenceInteger(PREF_VIDEO_GS_HANDLER));

	ui->checkBox_enable_audio->setChecked(CAppConfig::GetInstance().GetPreferenceBoolean(PREFERENCE_AUDIO_ENABLEOUTPUT));
	ui->spinBox_spuBlockCount->setValue(CAppConfig::GetInstance().GetPreferenceInteger(PREF_AUDIO_SPUBLOCKCOUNT));
	ui->comboBox_presentation_mode->setCurrentIndex(CAppConfig::GetInstance().GetPreferenceInteger(PREF_CGSHANDLER_PRESENTATION_MODE));
}

void SettingsDialog::on_checkBox_force_bilinear_filtering_clicked(bool checked)
{
	CAppConfig::GetInstance().SetPreferenceBoolean(PREF_CGSH_OPENGL_FORCEBILINEARTEXTURES, checked);
}

void SettingsDialog::on_comboBox_gs_selection_currentIndexChanged(int index)
{
	CAppConfig::GetInstance().SetPreferenceInteger(PREF_VIDEO_GS_HANDLER, index);
}

void SettingsDialog::on_checkBox_enable_audio_clicked(bool checked)
{
	CAppConfig::GetInstance().SetPreferenceBoolean(PREFERENCE_AUDIO_ENABLEOUTPUT, checked);
}

void SettingsDialog::on_comboBox_presentation_mode_currentIndexChanged(int index)
{
	CAppConfig::GetInstance().SetPreferenceInteger(PREF_CGSHANDLER_PRESENTATION_MODE, index);
}

void SettingsDialog::on_comboBox_res_multiplyer_currentIndexChanged(int index)
{
	int factor = pow(2, index);
	CAppConfig::GetInstance().SetPreferenceInteger(PREF_CGSH_OPENGL_RESOLUTION_FACTOR, factor);
}

void SettingsDialog::on_spinBox_spuBlockCount_valueChanged(int value)
{
	CAppConfig::GetInstance().SetPreferenceInteger(PREF_AUDIO_SPUBLOCKCOUNT, value);
}
