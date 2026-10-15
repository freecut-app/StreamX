#include "SceneManager.h"
#include "Log.h"

#include <QFile>
#include <QMetaObject>

namespace sx {

SceneManager &SceneManager::get()
{
	static SceneManager s;
	return s;
}

/* ---------------- tipos disponíveis ---------------- */

static bool usableType(const char *id)
{
	const uint32_t f = obs_get_source_output_flags(id);
	return !(f & OBS_SOURCE_CAP_DISABLED) && !(f & OBS_SOURCE_DEPRECATED);
}

QList<SourceType> SceneManager::inputTypes()
{
	QList<SourceType> out;
	const char *id = nullptr, *unv = nullptr;
	for (size_t i = 0; obs_enum_input_types2(i, &id, &unv); i++) {
		if (!usableType(id) || !strcmp(id, "scene") || !strcmp(id, "group"))
			continue;
		const uint32_t f = obs_get_source_output_flags(id);
		out.append({id, QString::fromUtf8(obs_source_get_display_name(id)), bool(f & OBS_SOURCE_VIDEO), bool(f & OBS_SOURCE_AUDIO)});
	}
	return out;
}

QList<SourceType> SceneManager::filterTypes(bool video, bool audio)
{
	QList<SourceType> out;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_filter_types(i, &id); i++) {
		if (!usableType(id))
			continue;
		const uint32_t f = obs_get_source_output_flags(id);
		const bool v = f & OBS_SOURCE_VIDEO, a = f & OBS_SOURCE_AUDIO;
		if ((v && video) || (a && audio))
			out.append({id, QString::fromUtf8(obs_source_get_display_name(id)), v, a});
	}
	return out;
}

QList<SourceType> SceneManager::transitionTypes()
{
	QList<SourceType> out;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_transition_types(i, &id); i++)
		if (usableType(id))
			out.append({id, QString::fromUtf8(obs_source_get_display_name(id)), true, false});
	return out;
}

QList<SceneManager::Option> SceneManager::listOptions(const char *typeId, const char *property)
{
	QList<Option> out;
	obs_properties_t *props = obs_get_source_properties(typeId);
	if (!props)
		return out;
	obs_property_t *p = obs_properties_get(props, property);
	if (p && obs_property_get_type(p) == OBS_PROPERTY_LIST) {
		const auto fmt = obs_property_list_format(p);
		for (size_t i = 0; i < obs_property_list_item_count(p); i++) {
			if (obs_property_list_item_disabled(p, i))
				continue;
			QString value;
			if (fmt == OBS_COMBO_FORMAT_STRING)
				value = QString::fromUtf8(obs_property_list_item_string(p, i));
			else if (fmt == OBS_COMBO_FORMAT_INT)
				value = QString::number(obs_property_list_item_int(p, i));
			out.append({QString::fromUtf8(obs_property_list_item_name(p, i)), value});
		}
	}
	obs_properties_destroy(props);
	return out;
}

/* ---------------- cenas ---------------- */

void SceneManager::onSceneSignal(void *data, calldata_t *)
{
	auto *self = static_cast<SceneManager *>(data);
	QMetaObject::invokeMethod(self, [self]() {
		emit self->itemsChanged();
		emit self->audioSourcesChanged();
	}, Qt::QueuedConnection);
}

void SceneManager::connectScene(obs_source_t *scene)
{
	signal_handler_t *sh = obs_source_get_signal_handler(scene);
	for (const char *sig : {"item_add", "item_remove", "reorder", "item_visible", "item_locked", "refresh"})
		signal_handler_connect(sh, sig, onSceneSignal, this);
}

QStringList SceneManager::sceneNames() const
{
	QStringList out;
	for (const auto &s : scenes_)
		out << QString::fromUtf8(obs_source_get_name(s));
	return out;
}

QString SceneManager::currentSceneName() const { return current_ ? QString::fromUtf8(obs_source_get_name(current_)) : QString(); }

obs_scene_t *SceneManager::currentScene() const { return current_ ? obs_scene_from_source(current_) : nullptr; }

obs_source_t *SceneManager::sceneSource(const QString &name) const
{
	for (const auto &s : scenes_)
		if (name == QString::fromUtf8(obs_source_get_name(s)))
			return s;
	return nullptr;
}

QString SceneManager::uniqueSourceName(const QString &base) const
{
	QString name = base;
	for (int i = 2;; i++) {
		OBSSourceAutoRelease ex = obs_get_source_by_name(name.toUtf8().constData());
		if (!ex)
			return name;
		name = QString("%1 %2").arg(base).arg(i);
	}
}

bool SceneManager::addScene(const QString &name)
{
	const QString n = uniqueSourceName(name.trimmed().isEmpty() ? "Cena" : name.trimmed());
	OBSSceneAutoRelease sc = obs_scene_create(n.toUtf8().constData());
	if (!sc)
		return false;
	obs_source_t *src = obs_scene_get_source(sc);
	scenes_.append(OBSSource(src));
	connectScene(src);
	emit scenesChanged();
	if (!current_)
		setCurrentScene(n, false);
	return true;
}

bool SceneManager::removeScene(const QString &name)
{
	if (scenes_.size() <= 1)
		return false;
	for (int i = 0; i < scenes_.size(); i++) {
		if (name != QString::fromUtf8(obs_source_get_name(scenes_[i])))
			continue;
		OBSSource src = scenes_[i];
		const bool wasCurrent = src == current_;
		scenes_.removeAt(i);
		if (wasCurrent)
			setCurrentScene(QString::fromUtf8(obs_source_get_name(scenes_[qMax(0, i - 1)])), false);
		obs_source_remove(src);
		emit scenesChanged();
		return true;
	}
	return false;
}

bool SceneManager::renameScene(const QString &oldName, const QString &newName)
{
	obs_source_t *src = sceneSource(oldName);
	const QString n = newName.trimmed();
	if (!src || n.isEmpty())
		return false;
	OBSSourceAutoRelease ex = obs_get_source_by_name(n.toUtf8().constData());
	if (ex)
		return false;
	obs_source_set_name(src, n.toUtf8().constData());
	emit scenesChanged();
	if (src == current_)
		emit currentSceneChanged();
	return true;
}

bool SceneManager::setCurrentScene(const QString &name, bool useTransition)
{
	obs_source_t *src = sceneSource(name);
	if (!src)
		return false;
	current_ = src;
	if (transition_) {
		if (useTransition)
			obs_transition_start(transition_, OBS_TRANSITION_MODE_AUTO, uint32_t(transitionMs_), src);
		else
			obs_transition_set(transition_, src);
	} else {
		obs_set_output_source(0, src);
	}
	emit currentSceneChanged();
	emit itemsChanged();
	emit audioSourcesChanged();
	return true;
}

/* ---------------- transições ---------------- */

bool SceneManager::setTransition(const QString &typeId, int durationMs)
{
	transitionMs_ = qBound(50, durationMs, 10000);
	if (transition_ && typeId == QString::fromUtf8(obs_source_get_id(transition_)))
		return true;
	OBSSourceAutoRelease t = obs_source_create_private(typeId.toUtf8().constData(), "Transição StreamX", nullptr);
	if (!t)
		return false;
	if (current_)
		obs_transition_set(t, current_);
	obs_set_output_source(0, t);
	transition_ = std::move(t);
	return true;
}

QString SceneManager::transitionType() const { return transition_ ? QString::fromUtf8(obs_source_get_id(transition_)) : QString(); }

/* ---------------- itens da cena atual ---------------- */

QList<ItemInfo> SceneManager::items() const
{
	QList<ItemInfo> out;
	obs_scene_t *sc = currentScene();
	if (!sc)
		return out;
	obs_scene_enum_items(sc, [](obs_scene_t *, obs_sceneitem_t *it, void *p) {
		auto *list = static_cast<QList<ItemInfo> *>(p);
		obs_source_t *s = obs_sceneitem_get_source(it);
		list->prepend({obs_sceneitem_get_id(it), QString::fromUtf8(obs_source_get_name(s)), QString::fromUtf8(obs_source_get_id(s)),
			       obs_sceneitem_visible(it), obs_sceneitem_locked(it)});
		return true;
	}, &out);
	return out;
}

obs_sceneitem_t *SceneManager::item(int64_t id) const
{
	obs_scene_t *sc = currentScene();
	return sc ? obs_scene_find_sceneitem_by_id(sc, id) : nullptr;
}

void SceneManager::fitItem(int64_t id)
{
	obs_sceneitem_t *it = item(id);
	if (!it)
		return;
	obs_video_info ovi;
	obs_get_video_info(&ovi);
	obs_transform_info ti = {};
	obs_sceneitem_get_info2(it, &ti);
	ti.bounds_type = OBS_BOUNDS_SCALE_INNER;
	ti.bounds_alignment = OBS_ALIGN_CENTER;
	ti.bounds = {float(ovi.base_width), float(ovi.base_height)};
	ti.pos = {0.0f, 0.0f};
	ti.scale = {1.0f, 1.0f};
	ti.rot = 0.0f;
	obs_sceneitem_set_info2(it, &ti);
}

obs_source_t *SceneManager::addSource(const char *typeId, const QString &name, obs_data_t *settings)
{
	obs_scene_t *sc = currentScene();
	if (!sc)
		return nullptr;
	const QString n = uniqueSourceName(name);
	OBSSourceAutoRelease src = obs_source_create(typeId, n.toUtf8().constData(), settings, nullptr);
	if (!src) {
		logError(QString("Não foi possível criar a fonte %1 (%2)").arg(n, typeId));
		return nullptr;
	}
	obs_sceneitem_t *it = obs_scene_add(sc, src);
	const uint32_t flags = obs_source_get_output_flags(src);
	if (it && (flags & OBS_SOURCE_VIDEO)) {
		const char *t = typeId;
		// capturas ocupam a tela inteira; demais fontes entram centralizadas no tamanho original
		if (!strcmp(t, "monitor_capture") || !strcmp(t, "window_capture") || !strcmp(t, "game_capture") || !strcmp(t, "dshow_input"))
			fitItem(obs_sceneitem_get_id(it));
	}
	logInfo(QString("Fonte adicionada: %1 (%2)").arg(n, typeId));
	return src;
}

bool SceneManager::addExistingSource(const QString &sourceName)
{
	obs_scene_t *sc = currentScene();
	OBSSourceAutoRelease src = obs_get_source_by_name(sourceName.toUtf8().constData());
	return sc && src && obs_scene_add(sc, src);
}

void SceneManager::removeItem(int64_t id)
{
	if (obs_sceneitem_t *it = item(id))
		obs_sceneitem_remove(it);
}

void SceneManager::setItemVisible(int64_t id, bool v)
{
	if (obs_sceneitem_t *it = item(id))
		obs_sceneitem_set_visible(it, v);
}

void SceneManager::setItemLocked(int64_t id, bool l)
{
	if (obs_sceneitem_t *it = item(id))
		obs_sceneitem_set_locked(it, l);
}

void SceneManager::moveItem(int64_t id, bool up)
{
	if (obs_sceneitem_t *it = item(id))
		obs_sceneitem_set_order(it, up ? OBS_ORDER_MOVE_UP : OBS_ORDER_MOVE_DOWN);
}

/* ---------------- áudio ---------------- */

QList<obs_source_t *> SceneManager::audioSources() const
{
	QList<obs_source_t *> out;
	if (desktopAudio_)
		out << desktopAudio_;
	if (micAudio_)
		out << micAudio_;
	obs_scene_t *sc = currentScene();
	if (sc) {
		obs_scene_enum_items(sc, [](obs_scene_t *, obs_sceneitem_t *it, void *p) {
			auto *list = static_cast<QList<obs_source_t *> *>(p);
			obs_source_t *s = obs_sceneitem_get_source(it);
			if ((obs_source_get_output_flags(s) & OBS_SOURCE_AUDIO) && !list->contains(s))
				list->append(s);
			return true;
		}, &out);
	}
	return out;
}

void SceneManager::setAudioDevices(const QString &desktopId, const QString &micId)
{
	auto apply = [](OBSSourceAutoRelease &src, const char *type, const char *name, const QString &dev, int channel) {
		if (dev == "disabled") {
			obs_set_output_source(channel, nullptr);
			src = nullptr;
			return;
		}
		if (!src) {
			OBSDataAutoRelease s = obs_data_create();
			obs_data_set_string(s, "device_id", dev.toUtf8().constData());
			src = obs_source_create(type, name, s, nullptr);
			obs_set_output_source(channel, src);
			return;
		}
		OBSDataAutoRelease s = obs_source_get_settings(src);
		if (dev != QString::fromUtf8(obs_data_get_string(s, "device_id"))) {
			OBSDataAutoRelease ns = obs_data_create();
			obs_data_set_string(ns, "device_id", dev.toUtf8().constData());
			obs_source_update(src, ns);
		}
	};
	apply(desktopAudio_, "wasapi_output_capture", "Áudio do PC", desktopId, 1);
	apply(micAudio_, "wasapi_input_capture", "Microfone", micId, 3);
	emit audioSourcesChanged();
}

/* ---------------- coleção padrão, salvar e carregar ---------------- */

void SceneManager::createDefault()
{
	clear();
	setTransition("fade_transition", 300);
	addScene("Cena 1");
	OBSDataAutoRelease mon = obs_data_create();
	const auto monitors = listOptions("monitor_capture", "monitor_id");
	for (const auto &m : monitors)
		logInfo("Monitor disponível: " + m.name);
	if (!monitors.isEmpty())
		obs_data_set_string(mon, "monitor_id", monitors.first().value.toUtf8().constData());
	else
		logWarn("Nenhum monitor encontrado para captura");
	addSource("monitor_capture", "Tela inteira", mon);
	setAudioDevices("default", "default");
	logInfo("Coleção de cenas padrão criada");
}

void SceneManager::clear()
{
	for (int ch = 0; ch < MAX_CHANNELS; ch++)
		obs_set_output_source(ch, nullptr);
	desktopAudio_ = nullptr;
	micAudio_ = nullptr;
	current_ = nullptr;
	for (auto &s : scenes_)
		obs_source_remove(s);
	scenes_.clear();
	transition_ = nullptr;
	emit scenesChanged();
	emit currentSceneChanged();
	emit itemsChanged();
	emit audioSourcesChanged();
}

bool SceneManager::save(const QString &path) const
{
	OBSDataAutoRelease root = obs_data_create();
	OBSDataArrayAutoRelease sources = obs_save_sources_filtered([](void *p, obs_source_t *s) {
		auto *self = static_cast<const SceneManager *>(p);
		return s != self->desktopAudio_.Get() && s != self->micAudio_.Get();
	}, const_cast<SceneManager *>(this));
	obs_data_set_array(root, "sources", sources);
	OBSDataArrayAutoRelease order = obs_data_array_create();
	for (const auto &s : scenes_) {
		OBSDataAutoRelease e = obs_data_create();
		obs_data_set_string(e, "name", obs_source_get_name(s));
		obs_data_array_push_back(order, e);
	}
	obs_data_set_array(root, "scene_order", order);
	obs_data_set_string(root, "current_scene", currentSceneName().toUtf8().constData());
	obs_data_set_string(root, "transition", transitionType().toUtf8().constData());
	obs_data_set_int(root, "transition_ms", transitionMs_);
	if (desktopAudio_) {
		OBSDataAutoRelease d = obs_save_source(desktopAudio_);
		obs_data_set_obj(root, "desktop_audio", d);
	}
	if (micAudio_) {
		OBSDataAutoRelease m = obs_save_source(micAudio_);
		obs_data_set_obj(root, "mic_audio", m);
	}
	const bool ok = obs_data_save_json_safe(root, path.toUtf8().constData(), "tmp", "bak");
	if (!ok)
		logError("Falha ao salvar as cenas em " + path);
	return ok;
}

bool SceneManager::load(const QString &path)
{
	if (!QFile::exists(path)) {
		createDefault();
		return false;
	}
	OBSDataAutoRelease root = obs_data_create_from_json_file_safe(path.toUtf8().constData(), "bak");
	if (!root) {
		logWarn("Arquivo de cenas inválido, criando padrão");
		createDefault();
		return false;
	}
	clear();
	OBSDataAutoRelease d = obs_data_get_obj(root, "desktop_audio");
	if (d) {
		desktopAudio_ = obs_load_source(d);
		obs_set_output_source(1, desktopAudio_);
	}
	OBSDataAutoRelease m = obs_data_get_obj(root, "mic_audio");
	if (m) {
		micAudio_ = obs_load_source(m);
		obs_set_output_source(3, micAudio_);
	}
	OBSDataArrayAutoRelease sources = obs_data_get_array(root, "sources");
	QList<OBSSource> loaded;
	obs_load_sources(sources, [](void *p, obs_source_t *s) {
		static_cast<QList<OBSSource> *>(p)->append(OBSSource(s));
	}, &loaded);
	// ordem salva das cenas
	OBSDataArrayAutoRelease order = obs_data_get_array(root, "scene_order");
	for (size_t i = 0; i < obs_data_array_count(order); i++) {
		OBSDataAutoRelease e = obs_data_array_item(order, i);
		const QString n = QString::fromUtf8(obs_data_get_string(e, "name"));
		for (const auto &s : loaded)
			if (obs_source_is_scene(s) && n == QString::fromUtf8(obs_source_get_name(s)) && !scenes_.contains(s)) {
				scenes_.append(s);
				connectScene(s);
			}
	}
	for (const auto &s : loaded)
		if (obs_source_is_scene(s) && !scenes_.contains(s)) {
			scenes_.append(s);
			connectScene(s);
		}
	if (scenes_.isEmpty()) {
		createDefault();
		return false;
	}
	QString tr = QString::fromUtf8(obs_data_get_string(root, "transition"));
	int trMs = int(obs_data_get_int(root, "transition_ms"));
	setTransition(tr.isEmpty() ? QStringLiteral("fade_transition") : tr, trMs > 0 ? trMs : 300);
	const QString cur = QString::fromUtf8(obs_data_get_string(root, "current_scene"));
	if (!setCurrentScene(cur, false))
		setCurrentScene(QString::fromUtf8(obs_source_get_name(scenes_.first())), false);
	emit scenesChanged();
	logInfo(QString("Cenas carregadas: %1").arg(sceneNames().join(", ")));
	return true;
}

} // namespace sx
