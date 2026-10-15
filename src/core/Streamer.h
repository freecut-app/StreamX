// StreamX — transmissão ao vivo RTMP/RTMPS
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <obs.hpp>

namespace sx {

struct StreamStats {
	double kbps = 0;
	int dropped = 0;
	int total = 0;
	float congestion = 0;
	bool reconnecting = false;
};

class Streamer : public QObject {
	Q_OBJECT
public:
	static Streamer &get();

	bool start(QString *error);
	void stop();
	bool isActive() const;
	qint64 elapsedMs() const { return isActive() ? timer_.elapsed() : 0; }
	StreamStats stats();

	static QStringList services();                         // serviços conhecidos (rtmp-services)
	static QStringList servers(const QString &service);    // "nome|url"

signals:
	void started();
	void stopped(int code, const QString &error);
	void reconnecting(bool on);

private:
	Streamer() = default;
	static void onStop(void *data, calldata_t *cd);
	static void onReconnect(void *data, calldata_t *cd);
	static void onReconnected(void *data, calldata_t *cd);
	OBSOutputAutoRelease output_;
	OBSServiceAutoRelease service_;
	OBSEncoderAutoRelease venc_, aenc_;
	QElapsedTimer timer_;
	uint64_t lastBytes_ = 0;
	qint64 lastMs_ = 0;
	bool reconnecting_ = false;
};

} // namespace sx
