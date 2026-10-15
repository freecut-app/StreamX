// StreamX — gravação em arquivo (multifaixa, MP4/MKV/MOV/MP4 híbrido)
#pragma once
#include <QObject>
#include <QString>
#include <QElapsedTimer>
#include <obs.hpp>

namespace sx {

class Recorder : public QObject {
	Q_OBJECT
public:
	static Recorder &get();

	// path vazio = gera nome automático na pasta de gravações do perfil
	bool start(QString *error, const QString &path = QString());
	void stop();
	bool isActive() const;
	QString lastFile() const { return lastFile_; }
	qint64 elapsedMs() const { return isActive() ? timer_.elapsed() : 0; }
	int droppedFrames() const;
	static QString nextFileName(const QString &dir, const QString &prefix, const QString &ext);

signals:
	void started(const QString &path);
	void stopped(const QString &path, int code, const QString &error);

private:
	Recorder() = default;
	static void onStop(void *data, calldata_t *cd);
	OBSOutputAutoRelease output_;
	QString lastFile_;
	QElapsedTimer timer_;
};

} // namespace sx
