// StreamX — atalhos globais (funcionam com o StreamX em segundo plano, inclusive durante jogos)
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <functional>
#include <obs.hpp>

namespace sx {

struct HotkeyAction {
	QString name;        // identificador salvo no perfil (ex.: streamx.record)
	QString label;       // texto para a interface
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	std::function<void()> fn;
};

class Hotkeys : public QObject {
	Q_OBJECT
public:
	static Hotkeys &get();

	// Registra as ações fixas (gravar, transmitir, clipe...) e as de troca de cena
	void registerActions();
	void refreshSceneActions();
	QList<HotkeyAction> actions() const { return actions_; }

	// Combinações atuais em texto (ex.: "Ctrl + Alt + R")
	QStringList bindingTexts(obs_hotkey_id id) const;
	void setBinding(obs_hotkey_id id, const QList<obs_key_combination_t> &combos);
	static QString comboText(obs_key_combination_t c);

	void loadFromProfile();
	void saveToProfile();

private:
	Hotkeys() = default;
	static void trigger(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed);
	obs_hotkey_id add(const QString &name, const QString &label, std::function<void()> fn, obs_key_combination_t def = {0, OBS_KEY_NONE});
	QList<HotkeyAction> actions_;
	int sceneActionsStart_ = -1;
};

} // namespace sx
