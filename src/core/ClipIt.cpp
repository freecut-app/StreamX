#include "ClipIt.h"
#include "Encoders.h"
#include "Settings.h"
#include "Log.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMetaObject>
#include <QStorageInfo>

namespace sx {

ClipIt &ClipIt::get()
{
	static ClipIt c;
	return c;
}

int ClipIt::seconds() const
{
	const int s = Settings::get().p().clipSeconds;
	return (s == 15 || s == 30 || s == 60 || s == 120) ? s : 30;
}

qint64 ClipIt::estimatedMemoryMB() const
{
	// estimativa conservadora: bitrate de gravação (ou ~25 Mbps em qualidade constante) * duração
	const auto &p = Settings::get().p();
	const int kbps = p.recConstantQuality ? 25000 : p.recBitrate;
	return qMin<qint64>(qint64(kbps + 192 * 2) * seconds() / 8 / 1024, p.clipMaxMB);
}

bool ClipIt::start(QString *error)
{
	if (isActive())
		return true;
	if (!EncoderPool::get().acquireRecording(error))
		return false;
	auto &pool = EncoderPool::get();
	const auto &p = Settings::get().p();
	const QString dir = Settings::get().clipsDir() + "/.buffer";
	QDir().mkpath(dir);

	OBSDataAutoRelease s = obs_data_create();
	obs_data_set_string(s, "directory", QDir::toNativeSeparators(dir).toUtf8().constData());
	obs_data_set_string(s, "format", "Clip %CCYY-%MM-%DD %hh-%mm-%ss");
	obs_data_set_string(s, "extension", "mp4");
	obs_data_set_bool(s, "allow_spaces", true);
	obs_data_set_int(s, "max_time_sec", seconds());
	obs_data_set_int(s, "max_size_mb", qBound(64, p.clipMaxMB, 8192));
	output_ = obs_output_create("replay_buffer", "StreamX: Clip It", s, nullptr);
	if (!output_) {
		if (error)
			*error = "Falha ao criar o buffer de replay.";
		return false;
	}
	obs_output_set_video_encoder(output_, pool.video());
	size_t idx = 0;
	for (int t = 0; t < 6; t++)
		if (obs_encoder_t *a = pool.audio(t))
			obs_output_set_audio_encoder(output_, a, idx++);
	signal_handler_t *sh = obs_output_get_signal_handler(output_);
	signal_handler_connect(sh, "saved", onSaved, this);
	signal_handler_connect(sh, "stop", onStop, this);
	if (!obs_output_start(output_)) {
		const char *e = obs_output_get_last_error(output_);
		if (error)
			*error = QString("Não foi possível iniciar o Clip It: %1").arg(e ? e : "erro desconhecido");
		output_ = nullptr;
		pool.releaseIfIdle();
		return false;
	}
	logInfo(QString("Clip It ativo: %1 s, limite %2 MB").arg(seconds()).arg(p.clipMaxMB));
	emit activeChanged(true);
	return true;
}

void ClipIt::stop()
{
	if (output_ && obs_output_active(output_))
		obs_output_stop(output_);
}

bool ClipIt::isActive() const { return output_ && obs_output_active(output_); }

bool ClipIt::save(QString *error)
{
	if (!isActive()) {
		if (error)
			*error = "O Clip It não está ligado.";
		return false;
	}
	if (saving_) {
		if (error)
			*error = "Já há um clipe sendo salvo.";
		return false;
	}
	const QStorageInfo disk(Settings::get().clipsDir());
	const qint64 need = estimatedMemoryMB() * 1024 * 1024 + 200ll * 1024 * 1024;
	if (disk.isValid() && disk.bytesAvailable() < need) {
		if (error)
			*error = QString("Espaço insuficiente para o clipe (%1 MB livres).").arg(disk.bytesAvailable() / 1048576);
		return false;
	}
	proc_handler_t *ph = obs_output_get_proc_handler(output_);
	calldata_t cd = {};
	proc_handler_call(ph, "save", &cd);
	calldata_free(&cd);
	saving_ = true;
	logInfo("Clip It: salvando clipe...");
	return true;
}

void ClipIt::onSaved(void *data, calldata_t *)
{
	auto *self = static_cast<ClipIt *>(data);
	QMetaObject::invokeMethod(self, [self]() {
		self->saving_ = false;
		if (!self->output_)
			return;
		calldata_t cd = {};
		proc_handler_call(obs_output_get_proc_handler(self->output_), "get_last_replay", &cd);
		const char *p = calldata_string(&cd, "path");
		const QString raw = p ? QString::fromUtf8(p) : QString();
		calldata_free(&cd);
		if (raw.isEmpty()) {
			emit self->failed("O replay foi salvo, mas o caminho não foi informado.");
			return;
		}
		self->organize(raw);
	}, Qt::QueuedConnection);
}

// Move o clipe para Clipes/AAAA-MM/[Jogo] Clip ... .mp4
void ClipIt::organize(const QString &raw)
{
	const QString month = QDateTime::currentDateTime().toString("yyyy-MM");
	const QString dir = Settings::get().clipsDir() + "/" + month;
	QDir().mkpath(dir);
	QString name = QFileInfo(raw).fileName();
	if (!gameTag_.isEmpty())
		name = sanitizeFileName(gameTag_) + " - " + name;
	QString dest = dir + "/" + name;
	for (int i = 2; QFile::exists(dest); i++)
		dest = dir + "/" + QFileInfo(name).completeBaseName() + QString(" (%1).mp4").arg(i);
	if (!QFile::rename(raw, dest))
		dest = raw; // se não conseguir mover, mantém onde está
	logInfo("Clip It: clipe salvo em " + dest);
	emit saved(dest);
}

void ClipIt::onStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<ClipIt *>(data);
	const int code = int(calldata_int(cd, "code"));
	QMetaObject::invokeMethod(self, [self, code]() {
		if (self->output_) {
			signal_handler_t *sh = obs_output_get_signal_handler(self->output_);
			signal_handler_disconnect(sh, "saved", onSaved, self);
			signal_handler_disconnect(sh, "stop", onStop, self);
		}
		self->output_ = nullptr;
		self->saving_ = false;
		EncoderPool::get().releaseIfIdle();
		logInfo(QString("Clip It desligado (código %1)").arg(code));
		emit self->activeChanged(false);
	}, Qt::QueuedConnection);
}

QFileInfoList ClipIt::clips() const
{
	QFileInfoList out;
	QDirIterator it(Settings::get().clipsDir(), {"*.mp4", "*.mkv"}, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QFileInfo fi(it.next());
		if (!fi.absolutePath().endsWith("/.buffer"))
			out << fi;
	}
	std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() > b.lastModified(); });
	return out;
}

} // namespace sx
