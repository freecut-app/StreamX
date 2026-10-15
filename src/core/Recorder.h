// StreamX — gravação em arquivo
#pragma once
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <obs.hpp>

namespace sx {

struct RecordSettings {
	QString path;              // arquivo de saída (.mp4, .mkv, .mov)
	QString encoderId;         // vazio = melhor disponível
	int bitrateKbps = 12000;   // usado em CBR
	int quality = 20;          // CQP/CRF (quanto menor, melhor)
	bool constantQuality = true;
	int audioKbps = 192;
};

class Recorder : public QObject {
	Q_OBJECT
public:
	static Recorder &get();

	bool start(const RecordSettings &s, QString *error);
	void stop();
	bool isActive() const;
	QString lastFile() const { return lastFile_; }
	qint64 elapsedMs() const { return isActive() ? timer_.elapsed() : 0; }
	int droppedFrames() const;

signals:
	void started(const QString &path);
	void stopped(const QString &path, int code, const QString &error);

private:
	Recorder() = default;
	static void onStop(void *data, calldata_t *cd);
	OBSOutputAutoRelease output_;
	OBSEncoderAutoRelease venc_, aenc_;
	QString lastFile_;
	QElapsedTimer timer_;
};

// Configurações do encoder de vídeo de acordo com o tipo (NVENC/AMF/QSV/x264)
obs_data_t *videoEncoderSettings(const QString &encoderId, bool constantQuality, int quality, int bitrateKbps);

} // namespace sx
