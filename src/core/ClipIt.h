// StreamX — Clip It: replay instantâneo (buffer na memória, salva sem parar a gravação)
#pragma once
#include <QObject>
#include <QString>
#include <QFileInfoList>
#include <obs.hpp>

namespace sx {

class ClipIt : public QObject {
	Q_OBJECT
public:
	static ClipIt &get();

	bool start(QString *error);   // liga o buffer
	void stop();
	bool isActive() const;
	bool save(QString *error);    // grava os últimos N segundos em disco
	int seconds() const;
	qint64 estimatedMemoryMB() const;

	// Clipes salvos (mais recentes primeiro)
	QFileInfoList clips() const;
	void setGameTag(const QString &tag) { gameTag_ = tag; }

signals:
	void activeChanged(bool active);
	void saved(const QString &path);
	void failed(const QString &error);

private:
	ClipIt() = default;
	static void onSaved(void *data, calldata_t *cd);
	static void onStop(void *data, calldata_t *cd);
	void organize(const QString &rawPath);
	OBSOutputAutoRelease output_;
	QString gameTag_;
	bool saving_ = false;
};

} // namespace sx
