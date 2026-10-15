#include "Recorder.h"
#include "Encoders.h"
#include "Settings.h"
#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStorageInfo>

namespace sx {

Recorder &Recorder::get()
{
	static Recorder r;
	return r;
}

QString Recorder::nextFileName(const QString &dir, const QString &prefix, const QString &ext)
{
	QDir().mkpath(dir);
	const QString base = dir + "/" + prefix + " " + QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm-ss");
	QString path = base + "." + ext;
	for (int i = 2; QFileInfo::exists(path); i++)
		path = QString("%1 (%2).%3").arg(base).arg(i).arg(ext);
	return path;
}

bool Recorder::start(QString *error, const QString &requested)
{
	if (isActive())
		return true;
	const auto &p = Settings::get().p();
	const bool hybrid = p.recFormat == "hybrid_mp4";
	const QString ext = hybrid ? "mp4" : (QStringList{"mp4", "mkv", "mov"}.contains(p.recFormat) ? p.recFormat : "mp4");
	const QString path = requested.isEmpty() ? nextFileName(Settings::get().recordingDir(), "StreamX", ext) : requested;
	QDir().mkpath(QFileInfo(path).absolutePath());

	const QStorageInfo disk(QFileInfo(path).absolutePath());
	if (disk.isValid() && disk.bytesAvailable() < 500ll * 1024 * 1024) {
		if (error)
			*error = QString("Pouco espaço em disco (%1 MB livres) em %2.").arg(disk.bytesAvailable() / 1048576).arg(disk.rootPath());
		return false;
	}
	if (!EncoderPool::get().acquireRecording(error))
		return false;
	auto &pool = EncoderPool::get();

	OBSDataAutoRelease os = obs_data_create();
	obs_data_set_string(os, "path", QDir::toNativeSeparators(path).toUtf8().constData());
	output_ = obs_output_create(hybrid ? "mp4_output" : "ffmpeg_muxer", "StreamX: gravação", os, nullptr);
	if (!output_) {
		if (error)
			*error = "Falha ao criar a saída de gravação.";
		return false;
	}
	obs_output_set_video_encoder(output_, pool.video());
	size_t idx = 0;
	for (int t = 0; t < 6; t++)
		if (obs_encoder_t *a = pool.audio(t))
			obs_output_set_audio_encoder(output_, a, idx++);
	signal_handler_connect(obs_output_get_signal_handler(output_), "stop", onStop, this);

	if (!obs_output_start(output_)) {
		const char *e = obs_output_get_last_error(output_);
		if (error)
			*error = QString("Não foi possível iniciar a gravação: %1").arg(e ? e : "erro desconhecido");
		signal_handler_disconnect(obs_output_get_signal_handler(output_), "stop", onStop, this);
		output_ = nullptr;
		pool.releaseIfIdle();
		return false;
	}
	lastFile_ = path;
	timer_.start();
	logInfo(QString("Gravação iniciada: %1 (%2, %3 faixa(s) de áudio)").arg(path, pool.videoId()).arg(idx));
	emit started(path);
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
		EncoderPool::get().releaseIfIdle();
		logInfo(QString("Gravação finalizada (código %1): %2").arg(code).arg(self->lastFile_));
		emit self->stopped(self->lastFile_, code, msg);
	}, Qt::QueuedConnection);
}

} // namespace sx
