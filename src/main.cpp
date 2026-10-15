// StreamX — ponto de entrada
// Uso normal: StreamX.exe
// Autotestes (sem interface, saem com código 0 em caso de sucesso):
//   --selftest-record <segundos> <arquivo>       grava a tela de verdade
//   --selftest-clip <segundos-antes-de-salvar>    liga o Clip It e salva um clipe
//   --selftest-stream <url-rtmp> <segundos>       transmite para um servidor RTMP
#include "core/ClipIt.h"
#include "core/Engine.h"
#include "core/Log.h"
#include "core/Recorder.h"
#include "core/SceneManager.h"
#include "core/Settings.h"
#include "core/Streamer.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QTimer>

using namespace sx;

static int g_result = 1;

static void finish(QApplication &app, int code, const QString &msg)
{
	logInfo(QString("AUTOTESTE: %1 (código %2)").arg(msg).arg(code));
	g_result = code;
	app.quit();
}

static void selfTestRecord(QApplication &app, int seconds, const QString &path)
{
	auto &rec = Recorder::get();
	QObject::connect(&rec, &Recorder::stopped, &app, [&app](const QString &f, int code, const QString &err) {
		finish(app, code == 0 ? 0 : 2, "gravação parou: " + f + " " + err);
	});
	QTimer::singleShot(1500, &app, [&app, &rec, seconds, path]() {
		QString err;
		if (!rec.start(&err, path))
			return finish(app, 3, err);
		QTimer::singleShot(seconds * 1000, &app, [&rec]() { rec.stop(); });
	});
}

static void selfTestClip(QApplication &app, int seconds)
{
	auto &clip = ClipIt::get();
	QObject::connect(&clip, &ClipIt::saved, &app, [&app, &clip](const QString &path) {
		logInfo("AUTOTESTE: clipe em " + path);
		clip.stop();
		QTimer::singleShot(1500, &app, [&app]() { finish(app, 0, "clipe salvo"); });
	});
	QObject::connect(&clip, &ClipIt::failed, &app, [&app](const QString &e) { finish(app, 2, e); });
	QTimer::singleShot(1000, &app, [&app, &clip, seconds]() {
		QString err;
		if (!clip.start(&err))
			return finish(app, 3, err);
		QTimer::singleShot(seconds * 1000, &app, [&app, &clip]() {
			QString e;
			if (!clip.save(&e))
				finish(app, 4, e);
		});
	});
}

static void selfTestStream(QApplication &app, const QString &url, int seconds)
{
	auto &p = Settings::get().p();
	const int slash = url.lastIndexOf('/');
	p.streamService = "custom";
	p.streamServer = url.left(slash);
	p.streamKey = url.mid(slash + 1);
	auto &st = Streamer::get();
	QObject::connect(&st, &Streamer::stopped, &app, [&app](int code, const QString &err) {
		finish(app, code == 0 ? 0 : 2, "transmissão parou " + err);
	});
	QTimer::singleShot(1000, &app, [&app, &st, seconds]() {
		QString err;
		if (!st.start(&err))
			return finish(app, 3, err);
		QTimer::singleShot(seconds * 1000, &app, [&st]() {
			const auto s = st.stats();
			logInfo(QString("AUTOTESTE: stream %1 kbps, %2/%3 quadros perdidos").arg(s.kbps, 0, 'f', 0).arg(s.dropped).arg(s.total));
			st.stop();
		});
	});
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

	const QStringList args = app.arguments();
	const bool selfTest = args.filter("--selftest-").size() > 0;
	Settings::get().load();
	if (selfTest) // autotestes não alteram o perfil do usuário em disco
		Settings::get().p() = ProfileSettings::fromJson(Settings::get().p().toJson());

	QString err;
	if (!Engine::get().startup(&err)) {
		logError(err);
		return 10;
	}
	auto &scenes = SceneManager::get();
	if (selfTest)
		scenes.createDefault();
	else
		scenes.load(Settings::scenesFile());

	int code = 0;
	auto at = [&](const char *opt) { return args.indexOf(opt); };
	if (selfTest) {
		if (int i = at("--selftest-record"); i >= 0 && args.size() > i + 2)
			selfTestRecord(app, args[i + 1].toInt(), args[i + 2]);
		else if (int i = at("--selftest-clip"); i >= 0 && args.size() > i + 1)
			selfTestClip(app, args[i + 1].toInt());
		else if (int i = at("--selftest-stream"); i >= 0 && args.size() > i + 2)
			selfTestStream(app, args[i + 1], args[i + 2].toInt());
		QTimer::singleShot(180000, &app, [&app]() { finish(app, 9, "tempo esgotado"); });
		app.exec();
		code = g_result;
	} else {
		QFile qss(":/theme/dark.qss");
		if (qss.open(QIODevice::ReadOnly))
			app.setStyleSheet(QString::fromUtf8(qss.readAll()));
		MainWindow w;
		w.show();
		code = app.exec();
		scenes.save(Settings::scenesFile());
		Settings::get().save();
	}
	Recorder::get().stop();
	ClipIt::get().stop();
	Streamer::get().stop();
	scenes.clear();
	Engine::get().shutdown();
	logInfo(QString("StreamX encerrado (código %1)").arg(code));
	return code;
}
