// StreamX — motor de captura (wrapper do libobs)
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <obs.hpp>

namespace sx {

struct VideoSettings {
	uint32_t baseWidth = 1920, baseHeight = 1080;   // área de composição (normalmente a resolução do monitor)
	uint32_t outWidth = 1920, outHeight = 1080;     // resolução gravada/transmitida
	uint32_t fps = 60;
};

struct EncoderInfo {
	QString id;       // ex.: obs_nvenc_h264_tex
	QString name;     // nome para a interface
	bool hardware = false;
};

class Engine : public QObject {
	Q_OBJECT
public:
	static Engine &get();

	bool startup(QString *error);
	void shutdown();
	bool isRunning() const { return running_; }

	bool resetVideo(const VideoSettings &vs, QString *error = nullptr);
	VideoSettings video() const { return video_; }

	// Encoders de vídeo realmente disponíveis nesta máquina, do melhor para o pior
	QList<EncoderInfo> videoEncoders() const;
	QString bestVideoEncoder() const;

	static QString configDir();       // %APPDATA%/StreamX
	static QString defaultOutputDir(); // Vídeos/StreamX

private:
	Engine() = default;
	bool running_ = false;
	VideoSettings video_;
};

} // namespace sx
