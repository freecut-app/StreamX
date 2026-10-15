// StreamX — ponto de entrada
// Uso normal: StreamX.exe
// Autoteste:  StreamX.exe --selftest-record <segundos> <arquivo.mp4> [encoder_id]
//             (grava a tela de verdade, sem interface, e sai com código 0 em caso de sucesso)
#include "core/Engine.h"
#include "core/Log.h"
#include "core/Recorder.h"
#include "core/SceneManager.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QTimer>

using namespace sx;

static int runSelfTest(QApplication &app, int seconds, const QString &path, const QString &encoder)
{
	auto &rec = Recorder::get();
	int result = 1;
	QObject::connect(&rec, &Recorder::stopped, &app, [&](const QString &, int code, const QString &err) {
		logInfo(QString("AUTOTESTE: gravação parou, código %1 %2").arg(code).arg(err));
		result = code == 0 ? 0 : 2;
		app.quit();
	});
	QTimer::singleShot(1500, &app, [&]() { // deixa a captura aquecer
		RecordSettings rs;
		rs.path = path;
		rs.encoderId = encoder;
		QString err;
		if (!rec.start(rs, &err)) {
			logError("AUTOTESTE: " + err);
			result = 3;
			app.quit();
			return;
		}
		QTimer::singleShot(seconds * 1000, &app, [&]() { rec.stop(); });
	});
	QTimer::singleShot((seconds + 30) * 1000, &app, [&]() { logError("AUTOTESTE: tempo esgotado"); result = 4; app.quit(); });
	app.exec();
	return result;
}

int main(int argc, char *argv[])
{
	QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
	QApplication app(argc, argv);
	QApplication::setApplicationName("StreamX");
	QApplication::setOrganizationName("StreamX");
	QApplication::setApplicationVersion(STREAMX_VERSION);
	initLog();
	logInfo("StreamX " STREAMX_VERSION " iniciando");

	QString err;
	if (!Engine::get().startup(&err)) {
		logError(err);
		return 10;
	}
	SceneManager::get().createDefault();

	int code = 0;
	const QStringList args = app.arguments();
	const int st = args.indexOf("--selftest-record");
	if (st >= 0 && args.size() > st + 2) {
		code = runSelfTest(app, args[st + 1].toInt(), args[st + 2], args.value(st + 3));
	} else {
		QFile qss(":/theme/dark.qss");
		if (qss.open(QIODevice::ReadOnly))
			app.setStyleSheet(QString::fromUtf8(qss.readAll()));
		MainWindow w;
		w.show();
		code = app.exec();
	}
	Recorder::get().stop();
	SceneManager::get().clear();
	Engine::get().shutdown();
	logInfo(QString("StreamX encerrado (código %1)").arg(code));
	return code;
}
