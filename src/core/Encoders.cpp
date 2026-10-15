#include "Encoders.h"
#include "Engine.h"
#include "Settings.h"
#include "Log.h"

namespace sx {

EncoderPool &EncoderPool::get()
{
	static EncoderPool p;
	return p;
}

obs_data_t *videoEncoderSettings(const QString &id, bool cq, int quality, int bitrate)
{
	obs_data_t *s = obs_data_create();
	obs_data_set_int(s, "keyint_sec", 2);
	obs_data_set_int(s, "bitrate", bitrate);
	if (id.startsWith("obs_nvenc")) {
		obs_data_set_string(s, "rate_control", cq ? "CQP" : "CBR");
		obs_data_set_int(s, "cqp", quality);
		obs_data_set_string(s, "preset2", "p5");
		obs_data_set_string(s, "tune", "hq");
		obs_data_set_string(s, "multipass", "qres");
	} else if (id.contains("amf")) {
		obs_data_set_string(s, "rate_control", cq ? "CQP" : "CBR");
		obs_data_set_int(s, "cqp", quality);
		obs_data_set_string(s, "preset", "quality");
	} else if (id.startsWith("obs_qsv")) {
		obs_data_set_string(s, "rate_control", cq ? "CQP" : "CBR");
		obs_data_set_int(s, "cqp", quality);
		obs_data_set_string(s, "target_usage", "TU4");
	} else { // x264
		obs_data_set_string(s, "rate_control", cq ? "CRF" : "CBR");
		obs_data_set_int(s, "crf", quality);
		obs_data_set_string(s, "preset", "veryfast");
	}
	return s;
}

bool EncoderPool::inUse() const
{
	if (video_ && obs_encoder_active(video_))
		return true;
	for (const auto &a : audio_)
		if (a && obs_encoder_active(a))
			return true;
	return false;
}

bool EncoderPool::acquireRecording(QString *error)
{
	if (inUse())
		return true; // já em uso pela gravação ou pelo Clip It: reaproveita
	const auto &p = Settings::get().p();
	QString enc = p.recEncoder;
	const auto available = Engine::get().videoEncoders();
	bool found = false;
	for (const auto &e : available)
		found |= e.id == enc;
	if (!found)
		enc = Engine::get().bestVideoEncoder();
	if (enc.isEmpty()) {
		if (error)
			*error = "Nenhum codificador de vídeo disponível.";
		return false;
	}
	OBSDataAutoRelease vs = videoEncoderSettings(enc, p.recConstantQuality, p.recQuality, p.recBitrate);
	video_ = obs_video_encoder_create(enc.toUtf8().constData(), "StreamX: vídeo (gravação)", vs, nullptr);
	if (!video_) {
		if (error)
			*error = "Falha ao criar o codificador " + enc;
		return false;
	}
	obs_encoder_set_video(video_, obs_get_video());
	videoId_ = enc;
	tracks_ = p.recTracks;
	for (int i = 0; i < 6; i++) {
		audio_[i] = nullptr;
		if (!(tracks_ & (1 << i)))
			continue;
		OBSDataAutoRelease as = obs_data_create();
		obs_data_set_int(as, "bitrate", 192);
		audio_[i] = obs_audio_encoder_create("ffmpeg_aac", QString("StreamX: áudio faixa %1").arg(i + 1).toUtf8().constData(), as, size_t(i), nullptr);
		if (!audio_[i]) {
			if (error)
				*error = "Falha ao criar o codificador de áudio.";
			return false;
		}
		obs_encoder_set_audio(audio_[i], obs_get_audio());
	}
	logInfo(QString("Codificadores de gravação: %1, faixas de áudio 0x%2").arg(enc).arg(tracks_, 0, 16));
	return true;
}

void EncoderPool::releaseIfIdle()
{
	if (inUse())
		return;
	video_ = nullptr;
	for (auto &a : audio_)
		a = nullptr;
}

} // namespace sx
