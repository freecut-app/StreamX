#include "GameMode.h"
#include "ClipIt.h"
#include "Engine.h"
#include "Log.h"
#include "SceneManager.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

#include <windows.h>

namespace sx {

GameMode &GameMode::get()
{
	static GameMode g;
	return g;
}

const QList<GamePreset> &GameMode::presets()
{
	static const QList<GamePreset> list = {
		{"fortnite", "Fortnite", {"fortniteclient-win64-shipping.exe"}, 60, 1080, "window",
		 "Use 'Captura de jogo'. Com DirectX 12 ativo, mantenha o modo de tela cheia em 'Janela sem bordas' para a captura mais estável."},
		{"minecraft", "Minecraft", {"javaw.exe", "minecraft.windows.exe", "minecraft.exe"}, 60, 1080, "window",
		 "Na Java Edition, o processo é javaw.exe: a captura usa o título da janela do Minecraft."},
		{"gtav", "GTA V", {"gta5.exe", "gta5_enhanced.exe"}, 60, 1080, "window",
		 "Jogo pesado na GPU: se o FPS cair, reduza a saída para 900p ou limite o jogo a 60 fps."},
		{"valorant", "VALORANT", {"valorant-win64-shipping.exe"}, 60, 1080, "window",
		 "Jogo leve: 60 fps de gravação mantêm a fluidez sem pesar."},
		{"cs2", "Counter-Strike 2", {"cs2.exe"}, 60, 1080, "window",
		 "Prefira 'Janela sem bordas' para alternar entre o jogo e o StreamX sem tela preta."},
		{"lol", "League of Legends", {"league of legends.exe"}, 60, 1080, "window", "Jogo leve, ideal para 1080p60."},
		{"roblox", "Roblox", {"robloxplayerbeta.exe"}, 60, 1080, "window", ""},
		{"apex", "Apex Legends", {"r5apex.exe", "r5apex_dx12.exe"}, 60, 1080, "window",
		 "Alguns anti-cheats bloqueiam a injeção: se a captura ficar preta, use 'Captura de janela'."},
		{"rocketleague", "Rocket League", {"rocketleague.exe"}, 60, 1080, "window", ""},
		{"cod", "Call of Duty", {"cod.exe", "cod24-cod.exe"}, 60, 1080, "window",
		 "Jogo muito pesado: monitore a GPU; acima de 90% reduza a resolução de saída."},
		{"eafc", "EA SPORTS FC", {"fc25.exe", "fc26.exe"}, 60, 1080, "window", ""},
	};
	return list;
}

const GamePreset *GameMode::presetFor(const QString &exe) const
{
	const QString e = exe.toLower();
	for (const auto &p : presets())
		if (p.exes.contains(e))
			return &p;
	return nullptr;
}

void GameMode::loadOverrides()
{
	if (loaded_)
		return;
	loaded_ = true;
	QFile f(Engine::configDir() + "/games.json");
	if (f.open(QIODevice::ReadOnly))
		overrides_ = QJsonDocument::fromJson(f.readAll()).object();
}

QJsonObject GameMode::userOverrides(const QString &exe) const
{
	const_cast<GameMode *>(this)->loadOverrides();
	return overrides_[exe.toLower()].toObject();
}

void GameMode::setUserOverrides(const QString &exe, const QJsonObject &o)
{
	loadOverrides();
	overrides_[exe.toLower()] = o;
	QFile f(Engine::configDir() + "/games.json");
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(overrides_).toJson());
}

// Formato do game_capture: título:classe:executável, com ':' e '#' escapados
static QString escapeSpec(QString s)
{
	s.replace("#", "#22");
	s.replace(":", "#3A");
	return s;
}

DetectedGame GameMode::foreground()
{
	DetectedGame g;
	HWND w = GetForegroundWindow();
	if (!w)
		return g;
	DWORD pid = 0;
	GetWindowThreadProcessId(w, &pid);
	if (!pid || pid == GetCurrentProcessId())
		return g;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h)
		return g;
	wchar_t buf[MAX_PATH];
	DWORD len = MAX_PATH;
	if (QueryFullProcessImageNameW(h, 0, buf, &len))
		g.path = QString::fromWCharArray(buf, int(len));
	CloseHandle(h);
	g.exe = QFileInfo(g.path).fileName();
	wchar_t title[512] = {}, cls[256] = {};
	GetWindowTextW(w, title, 512);
	GetClassNameW(w, cls, 256);
	g.title = QString::fromWCharArray(title);
	g.windowSpec = escapeSpec(g.title) + ":" + escapeSpec(QString::fromWCharArray(cls)) + ":" + escapeSpec(g.exe);
	RECT wr;
	MONITORINFO mi = {sizeof(mi)};
	if (GetWindowRect(w, &wr) && GetMonitorInfoW(MonitorFromWindow(w, MONITOR_DEFAULTTONEAREST), &mi))
		g.fullscreen = wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top && wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
	return g;
}

void GameMode::setEnabled(bool on)
{
	if (on == enabled_)
		return;
	enabled_ = on;
	static QTimer *timer = nullptr;
	if (!timer) {
		timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, &GameMode::poll);
	}
	if (on) {
		timer->start(2000);
		poll();
	} else {
		timer->stop();
		applyProcessPriority(false);
		game_ = {};
		ClipIt::get().setGameTag(QString());
		emit gameChanged(game_);
	}
	logInfo(on ? "Game Mode ligado" : "Game Mode desligado");
	emit enabledChanged(on);
}

void GameMode::poll()
{
	const DetectedGame g = foreground();
	const GamePreset *p = presetFor(g.exe);
	// "jogo" = preset conhecido, ou app em tela cheia que não seja navegador, player ou app comum
	static const QStringList notGames = {"explorer.exe", "chrome.exe", "msedge.exe", "firefox.exe", "opera.exe", "brave.exe",
					     "vlc.exe", "mpc-hc64.exe", "video.ui.exe", "applicationframehost.exe", "powerpnt.exe",
					     "streamx.exe", "freecut.exe", "obs64.exe", "discord.exe", "spotify.exe", "teams.exe",
					     "zoom.exe", "searchhost.exe", "shellexperiencehost.exe", "lockapp.exe"};
	const bool isGame = p || (g.fullscreen && !g.exe.isEmpty() && !notGames.contains(g.exe.toLower()));
	if (!isGame || g.exe.compare(game_.exe, Qt::CaseInsensitive) == 0)
		return;
	game_ = g;
	game_.preset = p;
	ClipIt::get().setGameTag(p ? p->name : QFileInfo(g.exe).completeBaseName());
	applyProcessPriority(true);
	logInfo(QString("Game Mode: jogo detectado %1 (%2)").arg(p ? p->name : g.exe, g.title));
	emit gameChanged(game_);
}

bool GameMode::applyToScene(const DetectedGame &g, QString *error)
{
	auto &sm = SceneManager::get();
	const QString sceneName = "Game Mode";
	if (!sm.sceneSource(sceneName))
		sm.addScene(sceneName);
	sm.setCurrentScene(sceneName);
	// reaproveita a captura de jogo se já existir na cena; senão, cria
	for (const auto &it : sm.items())
		if (it.typeId == "game_capture") {
			OBSSourceAutoRelease s = obs_get_source_by_name(it.name.toUtf8().constData());
			OBSDataAutoRelease st = obs_data_create();
			obs_data_set_string(st, "capture_mode", "window");
			obs_data_set_string(st, "window", g.windowSpec.toUtf8().constData());
			obs_data_set_int(st, "priority", 2); // executável deve coincidir
			obs_source_update(s, st);
			return true;
		}
	OBSDataAutoRelease st = obs_data_create();
	obs_data_set_string(st, "capture_mode", "window");
	obs_data_set_string(st, "window", g.windowSpec.toUtf8().constData());
	obs_data_set_int(st, "priority", 2);
	obs_data_set_bool(st, "capture_cursor", true);
	if (!sm.addSource("game_capture", "Jogo", st)) {
		if (error)
			*error = "Não foi possível criar a captura de jogo.";
		return false;
	}
	return true;
}

void GameMode::applyProcessPriority(bool gaming)
{
	// Acima do normal evita quedas de quadro da gravação quando o jogo disputa a CPU,
	// sem chegar a "alta", que poderia prejudicar o próprio jogo.
	SetPriorityClass(GetCurrentProcess(), gaming ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
}

QStringList GameMode::suggestions(const PerfSample &s) const
{
	QStringList out;
	const double lagPct = s.totalFrames ? 100.0 * s.laggedFrames / s.totalFrames : 0;
	const double skipPct = s.encodedFrames ? 100.0 * s.skippedFrames / s.encodedFrames : 0;
	if (s.gpu >= 90)
		out << QString("GPU em %1%: reduza a resolução de saída (ex.: 1080p → 900p) ou limite o FPS do jogo.").arg(qRound(s.gpu));
	if (lagPct > 1)
		out << QString("%1% dos quadros atrasaram na renderização: a GPU está no limite. Ative 'Janela sem bordas' no jogo ou reduza o FPS de gravação.").arg(lagPct, 0, 'f', 1);
	if (skipPct > 1)
		out << QString("%1% dos quadros foram pulados na codificação: use um codificador de hardware (NVENC/AMF/QSV) ou um preset mais rápido.").arg(skipPct, 0, 'f', 1);
	if (s.cpuSystem >= 90)
		out << QString("CPU em %1%: feche programas em segundo plano; se estiver usando x264, troque por codificação na GPU.").arg(qRound(s.cpuSystem));
	if (s.ramTotalGB > 0 && s.ramUsedGB / s.ramTotalGB > 0.9)
		out << "Memória quase cheia: diminua a duração do Clip It ou feche o navegador.";
	if (out.isEmpty())
		out << "Tudo estável: a captura não está limitando o desempenho.";
	return out;
}

} // namespace sx
