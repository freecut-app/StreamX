#include "Streamer.h"
#include "Encoders.h"
#include "Engine.h"
#include "Settings.h"
#include "Log.h"

#include <QMetaObject>

namespace sx {

Streamer &Streamer::get()
{
	static Streamer s;
	return s;
}

QStringList Streamer::services()
{
	QStringList out;
	obs_properties_t *props = obs_get_service_properties("rtmp_common");
	if (!props)
		return out;
	if (obs_property_t *p = obs_properties_get(props, "service"))
		for (size_t i = 0; i < obs_property_list_item_count(p); i++)
			out << QString::fromUtf8(obs_property_list_item_string(p, i));
	obs_properties_destroy(props);
	return out;
}

QStringList Streamer::servers(const QString &service)
{
	QStringList out;
	obs_properties_t *props = obs_get_service_properties("rtmp_common");
	if (!props)
		return out;
	OBSDataAutoRelease s = obs_data_create();
	obs_data_set_string(s, "service", service.toUtf8().constData());
	obs_property_t *sp = obs_properties_get(props, "service");
	if (sp)
		obs_property_modified(sp, s); // repovoa a lista de servidores para o serviço escolhido
	if (obs_property_t *p = obs_properties_get(props, "server"))
		for (size_t i = 0; i < obs_property_list_item_count(p); i++)
			out << QString("%1|%2").arg(QString::fromUtf8(obs_property_list_item_name(p, i)), QString::fromUtf8(obs_property_list_item_string(p, i)));
	obs_properties_destroy(props);
	return out;
}

bool Streamer::start(QString *error)
{
	if (isActive())
		return true;
	const auto &p = Settings::get().p();
	if (p.streamServer.trimmed().isEmpty()) {
		if (error)
			*error = "Configure o servidor de transmissão em Configurações → Transmissão.";
		return false;
	}
	const bool custom = p.streamService == "custom";
	OBSDataAutoRelease ss = obs_data_create();
	if (!custom)
		obs_data_set_string(ss, "service", p.streamService.toUtf8().constData());
	obs_data_set_string(ss, "server", p.streamServer.trimmed().toUtf8().constData());
	obs_data_set_string(ss, "key", p.streamKey.trimmed().toUtf8().constData());
	service_ = obs_service_create(custom ? "rtmp_custom" : "rtmp_common", "StreamX: serviço", ss, nullptr);
	if (!service_) {
		if (error)
			*error = "Serviço de transmissão inválido.";
		return false;
	}

	// Encoders próprios da transmissão (taxa constante, exigida pelas plataformas)
	QString enc = p.streamEncoder;
	bool found = false;
	for (const auto &e : Engine::get().videoEncoders())
		found |= e.id == enc;
	if (!found)
		enc = Engine::get().bestVideoEncoder();
	OBSDataAutoRelease vs = videoEncoderSettings(enc, false, 23, p.streamBitrate);
	venc_ = obs_video_encoder_create(enc.toUtf8().constData(), "StreamX: vídeo (transmissão)", vs, nullptr);
	OBSDataAutoRelease as = obs_data_create();
	obs_data_set_int(as, "bitrate", p.streamAudioBitrate);
	aenc_ = obs_audio_encoder_create("ffmpeg_aac", "StreamX: áudio (transmissão)", as, 0, nullptr);
	if (!venc_ || !aenc_) {
		if (error)
			*error = "Falha ao criar os codificadores da transmissão.";
		return false;
	}
	obs_encoder_set_video(venc_, obs_get_video());
	obs_encoder_set_audio(aenc_, obs_get_audio());
	obs_service_apply_encoder_settings(service_, vs, as); // respeita limites do serviço (ex.: bitrate máximo)
	obs_encoder_update(venc_, vs);
	obs_encoder_update(aenc_, as);

	const char *type = obs_service_get_preferred_output_type(service_);
	output_ = obs_output_create(type ? type : "rtmp_output", "StreamX: transmissão", nullptr, nullptr);
	if (!output_) {
		if (error)
			*error = "Falha ao criar a saída de transmissão.";
		return false;
	}
	obs_output_set_service(output_, service_);
	obs_output_set_video_encoder(output_, venc_);
	obs_output_set_audio_encoder(output_, aenc_, 0);
	obs_output_set_reconnect_settings(output_, 20, 3);
	signal_handler_t *sh = obs_output_get_signal_handler(output_);
	signal_handler_connect(sh, "stop", onStop, this);
	signal_handler_connect(sh, "reconnect", onReconnect, this);
	signal_handler_connect(sh, "reconnect_success", onReconnected, this);

	if (!obs_output_start(output_)) {
		const char *e = obs_output_get_last_error(output_);
		if (error)
			*error = QString("Não foi possível iniciar a transmissão: %1").arg(e ? e : "verifique o servidor e a chave");
		output_ = nullptr;
		return false;
	}
	timer_.start();
	lastBytes_ = 0;
	lastMs_ = 0;
	logInfo(QString("Transmissão iniciando para %1 (%2, %3 kbps)").arg(p.streamServer, enc).arg(p.streamBitrate));
	emit started();
	return true;
}

void Streamer::stop()
{
	if (output_ && obs_output_active(output_))
		obs_output_stop(output_);
}

bool Streamer::isActive() const { return output_ && obs_output_active(output_); }

StreamStats Streamer::stats()
{
	StreamStats s;
	if (!output_)
		return s;
	const uint64_t bytes = obs_output_get_total_bytes(output_);
	const qint64 now = timer_.elapsed();
	if (now > lastMs_ && lastMs_ > 0)
		s.kbps = double(bytes - lastBytes_) * 8.0 / double(now - lastMs_);
	lastBytes_ = bytes;
	lastMs_ = now;
	s.dropped = obs_output_get_frames_dropped(output_);
	s.total = obs_output_get_total_frames(output_);
	s.congestion = obs_output_get_congestion(output_);
	s.reconnecting = reconnecting_;
	return s;
}

void Streamer::onStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<Streamer *>(data);
	const int code = int(calldata_int(cd, "code"));
	const char *err = calldata_string(cd, "last_error");
	const QString msg = err ? QString::fromUtf8(err) : QString();
	QMetaObject::invokeMethod(self, [self, code, msg]() {
		if (self->output_) {
			signal_handler_t *sh = obs_output_get_signal_handler(self->output_);
			signal_handler_disconnect(sh, "stop", onStop, self);
			signal_handler_disconnect(sh, "reconnect", onReconnect, self);
			signal_handler_disconnect(sh, "reconnect_success", onReconnected, self);
		}
		self->output_ = nullptr;
		self->service_ = nullptr;
		self->venc_ = nullptr;
		self->aenc_ = nullptr;
		self->reconnecting_ = false;
		logInfo(QString("Transmissão encerrada (código %1) %2").arg(code).arg(msg));
		emit self->stopped(code, msg);
	}, Qt::QueuedConnection);
}

void Streamer::onReconnect(void *data, calldata_t *)
{
	auto *self = static_cast<Streamer *>(data);
	QMetaObject::invokeMethod(self, [self]() { self->reconnecting_ = true; emit self->reconnecting(true); }, Qt::QueuedConnection);
}

void Streamer::onReconnected(void *data, calldata_t *)
{
	auto *self = static_cast<Streamer *>(data);
	QMetaObject::invokeMethod(self, [self]() { self->reconnecting_ = false; emit self->reconnecting(false); }, Qt::QueuedConnection);
}

} // namespace sx
