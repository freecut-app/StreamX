// StreamX — configurações (perfil atual)
#pragma once
#include <QDialog>
#include <QLineEdit>
#include <obs.hpp>

class QTabWidget;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLabel;

namespace sx {

// Campo que captura uma combinação de teclas (ex.: Ctrl + Alt + R)
class HotkeyEdit : public QLineEdit {
	Q_OBJECT
public:
	explicit HotkeyEdit(QWidget *parent = nullptr);
	obs_key_combination_t combo() const { return combo_; }
	void setCombo(obs_key_combination_t c);

protected:
	void keyPressEvent(QKeyEvent *e) override;

private:
	obs_key_combination_t combo_ = {0, OBS_KEY_NONE};
};

class SettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit SettingsDialog(QWidget *parent = nullptr, int startTab = 0);
	void accept() override;

private:
	QWidget *generalTab();
	QWidget *videoTab();
	QWidget *recordTab();
	QWidget *streamTab();
	QWidget *audioTab();
	QWidget *clipTab();
	QWidget *hotkeysTab();
	QWidget *controlTab();
	void reloadServers();

	QTabWidget *tabs_ = nullptr;
	// geral
	QComboBox *profile_ = nullptr;
	QLineEdit *recDir_ = nullptr, *clipDir_ = nullptr, *freecut_ = nullptr;
	// vídeo
	QComboBox *baseRes_ = nullptr, *outRes_ = nullptr, *fps_ = nullptr;
	// gravação
	QComboBox *recFormat_ = nullptr, *recEncoder_ = nullptr, *recMode_ = nullptr;
	QSpinBox *recQuality_ = nullptr, *recBitrate_ = nullptr;
	QList<QCheckBox *> tracks_;
	// transmissão
	QComboBox *service_ = nullptr, *server_ = nullptr, *streamEncoder_ = nullptr;
	QLineEdit *key_ = nullptr;
	QSpinBox *streamBitrate_ = nullptr, *audioBitrate_ = nullptr;
	// áudio
	QComboBox *desktopDev_ = nullptr, *micDev_ = nullptr;
	// clip it
	QComboBox *clipSeconds_ = nullptr;
	QSpinBox *clipMaxMB_ = nullptr;
	QCheckBox *clipOnStart_ = nullptr;
	// atalhos
	QList<QPair<obs_hotkey_id, HotkeyEdit *>> hotkeys_;
	// controle
	QCheckBox *ctlEnabled_ = nullptr, *ctlLan_ = nullptr;
	QSpinBox *ctlPort_ = nullptr;
	QLineEdit *ctlToken_ = nullptr;
};

QString formatRes(int w, int h);
bool parseRes(const QString &s, int *w, int *h);

} // namespace sx
