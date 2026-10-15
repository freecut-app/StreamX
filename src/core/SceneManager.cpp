#include "SceneManager.h"
#include "Log.h"

namespace sx {

SceneManager &SceneManager::get()
{
	static SceneManager s;
	return s;
}

obs_source_t *SceneManager::addSource(const char *typeId, const QString &name, obs_data_t *settings)
{
	if (!current_)
		return nullptr;
	OBSSourceAutoRelease src = obs_source_create(typeId, name.toUtf8().constData(), settings, nullptr);
	if (!src) {
		logError(QString("Não foi possível criar a fonte %1 (%2)").arg(name, typeId));
		return nullptr;
	}
	obs_sceneitem_t *item = obs_scene_add(current_, src);
	if (item) {
		// Ajusta a fonte para preencher a tela, mantendo a proporção
		obs_video_info ovi;
		obs_get_video_info(&ovi);
		obs_transform_info ti = {};
		obs_sceneitem_get_info2(item, &ti);
		ti.bounds_type = OBS_BOUNDS_SCALE_INNER;
		ti.bounds_alignment = OBS_ALIGN_CENTER;
		ti.bounds = {float(ovi.base_width), float(ovi.base_height)};
		ti.pos = {0.0f, 0.0f};
		ti.scale = {1.0f, 1.0f};
		obs_sceneitem_set_info2(item, &ti);
	}
	emit changed();
	return src; // a cena mantém sua própria referência
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

void SceneManager::createDefault()
{
	scene_ = obs_scene_create("Cena 1");
	current_ = scene_;
	obs_set_output_source(0, obs_scene_get_source(current_));

	// Escolhe o primeiro monitor real da lista do plugin (o padrão do plugin é "nenhum")
	OBSDataAutoRelease mon = obs_data_create();
	const auto monitors = listOptions("monitor_capture", "monitor_id");
	for (const auto &m : monitors)
		logInfo("Monitor disponível: " + m.name);
	if (!monitors.isEmpty())
		obs_data_set_string(mon, "monitor_id", monitors.first().value.toUtf8().constData());
	else
		logWarn("Nenhum monitor encontrado para captura");
	addSource("monitor_capture", "Tela inteira", mon);

	// Canais de áudio globais, como no OBS: 1 = áudio do PC, 3 = microfone
	desktopAudio_ = obs_source_create("wasapi_output_capture", "Áudio do PC", nullptr, nullptr);
	if (desktopAudio_)
		obs_set_output_source(1, desktopAudio_);
	micAudio_ = obs_source_create("wasapi_input_capture", "Microfone", nullptr, nullptr);
	if (micAudio_)
		obs_set_output_source(3, micAudio_);
	logInfo("Cena padrão criada (tela inteira, áudio do PC, microfone)");
	emit changed();
}

void SceneManager::clear()
{
	for (int ch = 0; ch < MAX_CHANNELS; ch++)
		obs_set_output_source(ch, nullptr);
	desktopAudio_ = nullptr;
	micAudio_ = nullptr;
	current_ = nullptr;
	scene_ = nullptr;
}

} // namespace sx
