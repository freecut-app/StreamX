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
#include "core/Apply.h"
#include "core/GameMode.h"
#include "core/Hotkeys.h"
#include "core/PerfMonitor.h"
#include "control/ControlServer.h"
#include "ui/MainWindow.h"
#include "core/Hardware.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QStackedWidget>
#include <QThread>
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

// mantém o app rodando com o StreamX Control ligado (o teste externo usa curl)
static void selfTestControl(QApplication &app, quint16 port, const QString &token, int seconds)
{
	QString err;
	if (!ControlServer::get().start(port, false, token, &err))
		return finish(app, 3, err);
	QObject::connect(&ControlServer::get(), &ControlServer::request, [](const QString &peer, const QString &line, int st) {
		logInfo(QString("AUTOTESTE: controle %1 %2 -> %3").arg(peer, line).arg(st));
	});
	QTimer::singleShot(seconds * 1000, &app, [&app]() {
		Recorder::get().stop();
		QTimer::singleShot(1500, &app, [&app]() { finish(app, 0, "controle encerrado"); });
	});
}

// grava com o microfone só na faixa 2 e o áudio do PC só na faixa 3 (faixa 1 = tudo)
static void selfTestTracks(QApplication &app, int seconds, const QString &path)
{
	Settings::get().p().recTracks = 0b111;
	auto &sm = SceneManager::get();
	if (sm.desktopAudio()) obs_source_set_audio_mixers(sm.desktopAudio(), 0b101);
	if (sm.micAudio()) obs_source_set_audio_mixers(sm.micAudio(), 0b011);
	selfTestRecord(app, seconds, path);
}

// pressione Ctrl+Alt+R externamente: deve iniciar e depois parar a gravação
static void selfTestHotkey(QApplication &app, int seconds)
{
	Hotkeys::get().registerActions();
	QObject::connect(&Recorder::get(), &Recorder::started, [](const QString &p) { logInfo("AUTOTESTE: atalho iniciou a gravação " + p); });
	QObject::connect(&Recorder::get(), &Recorder::stopped, &app, [&app](const QString &f, int code, const QString &) {
		finish(app, code == 0 ? 0 : 2, "atalho parou a gravação: " + f);
	});
	QTimer::singleShot(seconds * 1000, &app, [&app]() { finish(app, 5, "nenhum atalho recebido"); });
}

// cria fontes de todos os tipos, aplica filtro, salva e recarrega a coleção
static void selfTestSources(QApplication &app, const QString &file)
{
	auto &sm = SceneManager::get();
	struct Probe { const char *type; const char *prop; };
	const Probe probes[] = {{"monitor_capture", "monitor_id"}, {"window_capture", "window"}, {"game_capture", "window"},
				{"dshow_input", "video_device_id"}, {"wasapi_input_capture", "device_id"},
				{"wasapi_output_capture", "device_id"}, {"wasapi_process_output_capture", "window"}};
	for (const auto &p : probes)
		logInfo(QString("AUTOTESTE: %1.%2 = %3 opção(ões)").arg(p.type, p.prop).arg(SceneManager::listOptions(p.type, p.prop).size()));
	sm.addScene("Teste 2");
	sm.setCurrentScene("Teste 2", false);
	int created = 0;
	for (const auto &t : SceneManager::inputTypes()) {
		if (t.id.startsWith("ffmpeg_source") || t.id.startsWith("vlc") || t.id.startsWith("slideshow"))
			continue; // precisam de arquivo
		OBSDataAutoRelease st = obs_data_create();
		if (t.id.startsWith("text_gdiplus")) obs_data_set_string(st, "text", "StreamX");
		if (sm.addSource(t.id.toUtf8().constData(), t.name, st)) created++;
		else logError("AUTOTESTE: falhou " + t.id);
	}
	logInfo(QString("AUTOTESTE: %1 fontes criadas, %2 itens na cena").arg(created).arg(sm.items().size()));
	if (sm.micAudio()) {
		OBSSourceAutoRelease f = obs_source_create("noise_suppress_filter_v2", "Supressão de ruído", nullptr, nullptr);
		if (f) obs_source_filter_add(sm.micAudio(), f);
		logInfo(QString("AUTOTESTE: filtro de ruído no microfone: %1").arg(f ? "ok" : "FALHOU"));
	}
	QTimer::singleShot(1500, &app, [&app, &sm, file, created]() {
		const QStringList before = sm.sceneNames();
		const int itemsBefore = int(sm.items().size());
		if (!sm.save(file)) return finish(app, 2, "não salvou");
		sm.clear();
		if (!sm.load(file)) return finish(app, 3, "não carregou");
		sm.setCurrentScene("Teste 2", false);
		int filters = 0;
		if (sm.micAudio()) obs_source_enum_filters(sm.micAudio(), [](obs_source_t *, obs_source_t *, void *p) { (*static_cast<int *>(p))++; }, &filters);
		const bool ok = sm.sceneNames() == before && int(sm.items().size()) == itemsBefore && filters == 1;
		logInfo(QString("AUTOTESTE: recarregado cenas=%1 itens=%2/%3 filtros mic=%4").arg(sm.sceneNames().join(",")).arg(sm.items().size()).arg(itemsBefore).arg(filters));
		finish(app, ok && created > 5 ? 0 : 4, "fontes e persistência");
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
		else if (int i = at("--selftest-control"); i >= 0 && args.size() > i + 3)
			selfTestControl(app, quint16(args[i + 1].toInt()), args[i + 2], args[i + 3].toInt());
		else if (int i = at("--selftest-tracks"); i >= 0 && args.size() > i + 2)
			selfTestTracks(app, args[i + 1].toInt(), args[i + 2]);
		else if (int i = at("--selftest-hotkey"); i >= 0 && args.size() > i + 1)
			selfTestHotkey(app, args[i + 1].toInt());
		else if (int i = at("--selftest-sources"); i >= 0 && args.size() > i + 1)
			selfTestSources(app, args[i + 1]);
		else if (at("--selftest-assistant") >= 0) {
			const auto hw = HardwareInfo::detect();
			for (const QString goal : {"gravar", "transmitir", "jogos"}) {
				const auto r = recommend(hw, goal);
				logInfo(QString("AUTOTESTE: assistente [%1] %2x%3@%4 %5 q%6 %7kbps (%8)").arg(goal).arg(r.outW).arg(r.outH).arg(r.fps)
						.arg(r.encoderId).arg(r.recQuality).arg(r.streamBitrate).arg(r.tier));
			}
			QTimer::singleShot(0, &app, [&app]() { finish(app, 0, "assistente ok"); });
		}
		QTimer::singleShot(180000, &app, [&app]() { finish(app, 9, "tempo esgotado"); });
		app.exec();
		code = g_result;
	} else {
		QFile qss(":/theme/dark.qss");
		if (qss.open(QIODevice::ReadOnly))
			app.setStyleSheet(QString::fromUtf8(qss.readAll()));
		app.setWindowIcon(QIcon(":/icons/streamx.png"));
		Hotkeys::get().registerActions();
		PerfMonitor::get().start();
		const QString warn = applyProfile(true);
		if (Settings::get().p().gameModeAuto)
			GameMode::get().setEnabled(true);
		// salva as cenas periodicamente (proteção contra queda de energia)
		QTimer autosave;
		QObject::connect(&autosave, &QTimer::timeout, [&scenes]() { scenes.save(Settings::scenesFile()); });
		autosave.start(30000);
		MainWindow w;
		w.show();
		if (!warn.isEmpty())
			logWarn(warn);
		// --ui-screenshots <pasta>: fotografa cada página da interface e sai (para revisão)
		if (int i = args.indexOf("--ui-screenshots"); i >= 0 && args.size() > i + 1) {
			const QString dir = args[i + 1];
			QDir().mkpath(dir);
			QTimer::singleShot(2500, &app, [&w, dir, &app]() {
				auto *pages = w.findChild<QStackedWidget *>();
				for (int p = 0; pages && p < pages->count(); p++) {
					pages->setCurrentIndex(p);
					QApplication::processEvents();
					QThread::msleep(300);
					QApplication::processEvents();
					w.grab().save(QString("%1/pagina-%2.png").arg(dir).arg(p));
				}
				app.quit();
			});
		}
		code = app.exec();
		Hotkeys::get().saveToProfile();
		scenes.save(Settings::scenesFile());
		Settings::get().save();
		ControlServer::get().stop();
	}
	// encerra as saídas e espera finalizarem (evita arquivo corrompido)
	Recorder::get().stop();
	ClipIt::get().stop();
	Streamer::get().stop();
	QElapsedTimer waitTimer;
	waitTimer.start();
	while (anyOutputActive() && waitTimer.elapsed() < 10000) {
		QApplication::processEvents(QEventLoop::AllEvents, 50);
		QThread::msleep(20);
	}
	QApplication::processEvents();
	scenes.clear();
	Engine::get().shutdown();
	logInfo(QString("StreamX encerrado (código %1)").arg(code));
	return code;
}
