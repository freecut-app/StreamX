#include "Engine.h"
#include "Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>

namespace sx {

Engine &Engine::get()
{
	static Engine e;
	return e;
}

QString Engine::configDir()
{
	QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	if (d.isEmpty())
		d = QDir::homePath() + "/AppData/Roaming/StreamX";
	QDir().mkpath(d);
	return d;
}

QString Engine::defaultOutputDir()
{
	QString d = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/StreamX";
	QDir().mkpath(d);
	return d;
}

bool Engine::startup(QString *error)
{
	if (running_)
		return true;
	// O libobs procura dados e plugins em caminhos relativos ao executável
	// (../../data/libobs, ../../obs-plugins/64bit), igual ao OBS Studio.
	QDir::setCurrent(QCoreApplication::applicationDirPath());

	const QString moduleCfg = configDir() + "/plugin_config";
	QDir().mkpath(moduleCfg);
	if (!obs_startup("pt-BR", QDir::toNativeSeparators(moduleCfg).toUtf8().constData(), nullptr)) {
		if (error)
			*error = "Falha ao iniciar o libobs (obs_startup).";
		return false;
	}

	// Resolução base = monitor principal em pixels físicos
	if (QScreen *s = QGuiApplication::primaryScreen()) {
		const QSize px = s->size() * s->devicePixelRatio();
		video_.baseWidth = uint32_t(px.width()) & ~1u;
		video_.baseHeight = uint32_t(px.height()) & ~1u;
		video_.outWidth = video_.baseWidth;
		video_.outHeight = video_.baseHeight;
		if (video_.outHeight > 1080) { // reduz para 1080p mantendo a proporção
			video_.outWidth = (video_.baseWidth * 1080 / video_.baseHeight) & ~1u;
			video_.outHeight = 1080;
		}
	}
	if (!resetVideo(video_, error))
		return false;

	obs_audio_info2 ai = {};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	ai.max_buffering_ms = 1000;
	ai.fixed_buffering = false;
	if (!obs_reset_audio2(&ai)) {
		if (error)
			*error = "Falha ao iniciar o áudio (obs_reset_audio2).";
		return false;
	}

	obs_module_failure_info mfi = {};
	obs_load_all_modules2(&mfi);
	for (size_t i = 0; i < mfi.count; i++)
		logWarn(QString("Plugin não carregou: %1").arg(mfi.failed_modules[i]));
	obs_module_failure_info_free(&mfi);
	obs_post_load_modules();

	running_ = true;
	logInfo(QString("libobs %1 iniciado: base %2x%3, saída %4x%5 @ %6 fps")
			.arg(obs_get_version_string())
			.arg(video_.baseWidth).arg(video_.baseHeight)
			.arg(video_.outWidth).arg(video_.outHeight).arg(video_.fps));
	return true;
}

bool Engine::resetVideo(const VideoSettings &vs, QString *error)
{
	obs_video_info ovi = {};
	ovi.graphics_module = "libobs-d3d11";
	ovi.fps_num = vs.fps;
	ovi.fps_den = 1;
	ovi.base_width = vs.baseWidth;
	ovi.base_height = vs.baseHeight;
	ovi.output_width = vs.outWidth;
	ovi.output_height = vs.outHeight;
	ovi.output_format = VIDEO_FORMAT_NV12;
	ovi.adapter = 0;
	ovi.gpu_conversion = true;
	ovi.colorspace = VIDEO_CS_709;
	ovi.range = VIDEO_RANGE_PARTIAL;
	ovi.scale_type = OBS_SCALE_BICUBIC;
	const int r = obs_reset_video(&ovi);
	if (r != OBS_VIDEO_SUCCESS) {
		if (error)
			*error = r == OBS_VIDEO_CURRENTLY_ACTIVE
					 ? "Pare a gravação/transmissão antes de mudar o vídeo."
					 : QString("Falha ao iniciar o vídeo (código %1). Verifique o driver da placa de vídeo.").arg(r);
		return false;
	}
	video_ = vs;
	return true;
}

QList<EncoderInfo> Engine::videoEncoders() const
{
	// Ordem de preferência: GPU (textura direta) primeiro, CPU por último
	static const struct { const char *id; const char *name; bool hw; } known[] = {
		{"obs_nvenc_h264_tex", "NVIDIA NVENC H.264", true},
		{"obs_nvenc_hevc_tex", "NVIDIA NVENC HEVC", true},
		{"obs_nvenc_av1_tex", "NVIDIA NVENC AV1", true},
		{"h264_texture_amf", "AMD AMF H.264", true},
		{"h265_texture_amf", "AMD AMF HEVC", true},
		{"obs_qsv11_v2", "Intel Quick Sync H.264", true},
		{"obs_qsv11_hevc", "Intel Quick Sync HEVC", true},
		{"obs_x264", "x264 (CPU)", false},
	};
	QStringList registered;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++)
		registered << id;
	QList<EncoderInfo> out;
	for (const auto &k : known) {
		if (!registered.contains(k.id))
			continue;
		// Encoders de hardware só se registram quando o plugin detecta o hardware.
		if (obs_get_encoder_caps(k.id) & OBS_ENCODER_CAP_DEPRECATED)
			continue;
		out.append({k.id, k.name, k.hw});
	}
	return out;
}

QString Engine::bestVideoEncoder() const
{
	const auto list = videoEncoders();
	return list.isEmpty() ? QString() : list.first().id;
}

void Engine::shutdown()
{
	if (!running_)
		return;
	running_ = false;
	obs_set_output_source(0, nullptr);
	obs_shutdown();
	logInfo("libobs encerrado. Objetos não liberados: " + QString::number(bnum_allocs()));
}

} // namespace sx
