#include "Hotkeys.h"
#include "ClipIt.h"
#include "Log.h"
#include "Recorder.h"
#include "SceneManager.h"
#include "Settings.h"
#include "Streamer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <util/dstr.h>

namespace sx {

Hotkeys &Hotkeys::get()
{
	static Hotkeys h;
	return h;
}

void Hotkeys::trigger(void *data, obs_hotkey_id id, obs_hotkey_t *, bool pressed)
{
	if (!pressed)
		return;
	auto *self = static_cast<Hotkeys *>(data);
	// o libobs chama de uma thread própria: executa a ação na thread da interface
	QMetaObject::invokeMethod(self, [self, id]() {
		for (const auto &a : self->actions_)
			if (a.id == id && a.fn) {
				logInfo("Atalho: " + a.label);
				a.fn();
			}
	}, Qt::QueuedConnection);
}

obs_hotkey_id Hotkeys::add(const QString &name, const QString &label, std::function<void()> fn, obs_key_combination_t def)
{
	const obs_hotkey_id id = obs_hotkey_register_frontend(name.toUtf8().constData(), label.toUtf8().constData(), trigger, this);
	if (def.key != OBS_KEY_NONE)
		obs_hotkey_load_bindings(id, &def, 1);
	actions_.append({name, label, id, std::move(fn)});
	return id;
}

void Hotkeys::registerActions()
{
	obs_hotkey_enable_background_press(true); // dispara mesmo com o StreamX fora de foco
	const uint32_t ca = INTERACT_CONTROL_KEY | INTERACT_ALT_KEY;
	add("streamx.record", "Iniciar/parar gravação", [] {
		auto &r = Recorder::get();
		QString e;
		if (r.isActive()) r.stop(); else if (!r.start(&e)) logError(e);
	}, {ca, OBS_KEY_R});
	add("streamx.stream", "Iniciar/parar transmissão", [] {
		auto &s = Streamer::get();
		QString e;
		if (s.isActive()) s.stop(); else if (!s.start(&e)) logError(e);
	}, {ca, OBS_KEY_S});
	add("streamx.clip.save", "Clip It: salvar clipe", [] {
		QString e;
		if (!ClipIt::get().save(&e)) emit ClipIt::get().failed(e);
	}, {ca, OBS_KEY_C});
	add("streamx.clip.toggle", "Clip It: ligar/desligar buffer", [] {
		auto &c = ClipIt::get();
		QString e;
		if (c.isActive()) c.stop(); else if (!c.start(&e)) emit c.failed(e);
	}, {ca, OBS_KEY_B});
	sceneActionsStart_ = int(actions_.size());
	refreshSceneActions();
	loadFromProfile();
}

void Hotkeys::refreshSceneActions()
{
	if (sceneActionsStart_ < 0)
		return;
	saveToProfile();
	while (actions_.size() > sceneActionsStart_) {
		obs_hotkey_unregister(actions_.last().id);
		actions_.removeLast();
	}
	int n = 0;
	for (const QString &scene : SceneManager::get().sceneNames()) {
		// as 9 primeiras cenas recebem Ctrl+Alt+1..9 como padrão
		obs_key_combination_t def = {0, OBS_KEY_NONE};
		if (n < 9)
			def = {INTERACT_CONTROL_KEY | INTERACT_ALT_KEY, obs_key_t(OBS_KEY_1 + n)};
		add("streamx.scene." + scene, "Mudar para a cena: " + scene, [scene] { SceneManager::get().setCurrentScene(scene); }, def);
		n++;
	}
	loadFromProfile();
}

QString Hotkeys::comboText(obs_key_combination_t c)
{
	QStringList parts;
	if (c.modifiers & INTERACT_CONTROL_KEY) parts << "Ctrl";
	if (c.modifiers & INTERACT_ALT_KEY) parts << "Alt";
	if (c.modifiers & INTERACT_SHIFT_KEY) parts << "Shift";
	if (c.modifiers & INTERACT_COMMAND_KEY) parts << "Win";
	if (c.key != OBS_KEY_NONE) {
		struct dstr s = {};
		obs_key_to_str(c.key, &s);
		parts << QString::fromUtf8(s.array ? s.array : "?");
		dstr_free(&s);
	}
	return parts.join(" + ");
}

QStringList Hotkeys::bindingTexts(obs_hotkey_id id) const
{
	struct Ctx { obs_hotkey_id id; QStringList out; } ctx{id, {}};
	obs_enum_hotkey_bindings([](void *p, size_t, obs_hotkey_binding_t *b) {
		auto *c = static_cast<Ctx *>(p);
		if (obs_hotkey_binding_get_hotkey_id(b) == c->id)
			c->out << comboText(obs_hotkey_binding_get_key_combination(b));
		return true;
	}, &ctx);
	return ctx.out;
}

void Hotkeys::setBinding(obs_hotkey_id id, const QList<obs_key_combination_t> &combos)
{
	std::vector<obs_key_combination_t> v(combos.begin(), combos.end());
	obs_hotkey_load_bindings(id, v.data(), v.size());
	saveToProfile();
}

void Hotkeys::saveToProfile()
{
	auto &p = Settings::get().p();
	for (const auto &a : actions_) {
		OBSDataArrayAutoRelease arr = obs_hotkey_save(a.id);
		OBSDataAutoRelease wrap = obs_data_create();
		obs_data_set_array(wrap, "b", arr);
		const QJsonObject o = QJsonDocument::fromJson(obs_data_get_json(wrap)).object();
		p.hotkeys[a.name] = o["b"].toArray();
	}
}

void Hotkeys::loadFromProfile()
{
	const auto &h = Settings::get().p().hotkeys;
	for (const auto &a : actions_) {
		if (!h.contains(a.name))
			continue;
		const QByteArray json = QJsonDocument(QJsonObject{{"b", h[a.name]}}).toJson(QJsonDocument::Compact);
		OBSDataAutoRelease wrap = obs_data_create_from_json(json.constData());
		OBSDataArrayAutoRelease arr = obs_data_get_array(wrap, "b");
		obs_hotkey_load(a.id, arr);
	}
}

} // namespace sx
