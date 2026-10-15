#include "Recorder.h"
#include "Engine.h"
#include "Log.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

namespace sx {

Recorder &Recorder::get()
{
	static Recorder r;
	return r;
}

obs_data_t *videoEncoderSettings(const QString &id, bool cq, int quality, int bitrate)
{
	obs_data_t *s = obs_data_create();
	if (id.startsWith("obs_nvenc")) {
		obs_data_set_string(s, "rate_control", cq ? "CQP" : "CBR");
		obs_data_set_int(s, "cqp", quality);
		obs_data_set_int(s, "bitrate", bitrate);
		obs_data_set_string(s, "preset2", "p5");
		obs_data_set_string(s, "tune", "hq");
		obs_data_set_string(s, "multipass", "qres");
		obs_data_set_int(s, "keyint_sec", 2);
	} else if (id.contains("amf")) {
		obs_data_set_string(s, "rate_control", cq ? "CQP" : "CBR");
		obs_data_set_int(s, "cqp", quality);
		obs_data_set_int(s, "bitrate", bitrate);
		obs_data_set_string(s, "preset", "quality");
		obs_data_set_int(s, "keyint_sec", 2);
	} else if (id.startsWith("obs_qsv")) {
		obs_data_set_string(s, "rate_control", cq ? "CQP" : "CBR");
		obs_data_set_int(s, "cqp", quality);
		obs_data_set_int(s, "bitrate", bitrate);
		obs_data_set_int(s, "keyint_sec", 2);
	} else { // x264
		obs_data_set_string(s, "rate_control", cq ? "CRF" : "CBR");
		obs_data_set_int(s, "crf", quality);
		obs_data_set_int(s, "bitrate", bitrate);
		obs_data_set_string(s, "preset", "veryfast");
		obs_data_set_int(s, "keyint_sec", 2);
	}
	return s;
}

bool Recorder::start(const RecordSettings &rs, QString *error)
{
	if (isActive())
		return true;
	const QString enc = rs.encoderId.isEmpty() ? Engine::get().bestVideoEncoder() : rs.encoderId;
	if (enc.isEmpty()) {
		if (error)
			*error = "Nenhum codificador de vídeo disponível.";
		return false;
	}
	QDir().mkpath(QFileInfo(rs.path).absolutePath());

	OBSDataAutoRelease vs = videoEncoderSettings(enc, rs.constantQuality, rs.quality, rs.bitrateKbps);
	venc_ = obs_video_encoder_create(enc.toUtf8().constData(), "streamx_rec_video", vs, nullptr);
	OBSDataAutoRelease as = obs_data_create();
	obs_data_set_int(as, "bitrate", rs.audioKbps);
	aenc_ = obs_audio_encoder_create("ffmpeg_aac", "streamx_rec_audio", as, 0, nullptr);
	if (!venc_ || !aenc_) {
		if (error)
			*error = "Falha ao criar os codificadores (" + enc + ").";
		return false;
	}
	obs_encoder_set_video(venc_, obs_get_video());
	obs_encoder_set_audio(aenc_, obs_get_audio());

	OBSDataAutoRelease os = obs_data_create();
	obs_data_set_string(os, "path", QDir::toNativeSeparators(rs.path).toUtf8().constData());
	output_ = obs_output_create("ffmpeg_muxer", "streamx_recording", os, nullptr);
	if (!output_) {
		if (error)
			*error = "Falha ao criar a saída de gravação.";
		return false;
	}
	obs_output_set_video_encoder(output_, venc_);
	obs_output_set_audio_encoder(output_, aenc_, 0);
	signal_handler_connect(obs_output_get_signal_handler(output_), "stop", onStop, this);

	if (!obs_output_start(output_)) {
		const char *e = obs_output_get_last_error(output_);
		if (error)
			*error = QString("Não foi possível iniciar a gravação: %1").arg(e ? e : "erro desconhecido");
		signal_handler_disconnect(obs_output_get_signal_handler(output_), "stop", onStop, this);
		output_ = nullptr;
		return false;
	}
	lastFile_ = rs.path;
	timer_.start();
	logInfo(QString("Gravação iniciada: %1 (%2)").arg(rs.path, enc));
	emit started(rs.path);
	return true;
}

void Recorder::stop()
{
	if (output_ && obs_output_active(output_))
		obs_output_stop(output_);
}

bool Recorder::isActive() const { return output_ && obs_output_active(output_); }

int Recorder::droppedFrames() const { return output_ ? obs_output_get_frames_dropped(output_) : 0; }

void Recorder::onStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<Recorder *>(data);
	const int code = int(calldata_int(cd, "code"));
	const char *err = calldata_string(cd, "last_error");
	const QString msg = err ? QString::fromUtf8(err) : QString();
	QMetaObject::invokeMethod(self, [self, code, msg]() {
		if (self->output_)
			signal_handler_disconnect(obs_output_get_signal_handler(self->output_), "stop", onStop, self);
		self->output_ = nullptr;
		self->venc_ = nullptr;
		self->aenc_ = nullptr;
		logInfo(QString("Gravação finalizada (código %1): %2").arg(code).arg(self->lastFile_));
		emit self->stopped(self->lastFile_, code, msg);
	}, Qt::QueuedConnection);
}

} // namespace sx
