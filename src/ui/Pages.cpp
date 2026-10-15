#include "Pages.h"
#include "SettingsDialog.h"
#include "../control/ControlServer.h"
#include "../core/Apply.h"
#include "../core/ClipIt.h"
#include "../core/Engine.h"
#include "../core/Hardware.h"
#include "../core/Settings.h"
#include "../integration/FreeCutBridge.h"

#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStorageInfo>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace sx {

static QLabel *title(const QString &t, const QString &sub)
{
	auto *l = new QLabel(QString("<div style='font-size:18pt;font-weight:600'>%1</div><div style='color:#8a93a6'>%2</div>").arg(t, sub));
	l->setWordWrap(true);
	return l;
}

static QString human(qint64 b)
{
	if (b > 1073741824) return QString("%1 GB").arg(double(b) / 1073741824.0, 0, 'f', 1).replace('.', ',');
	return QString("%1 MB").arg(qMax<qint64>(1, b / 1048576));
}

/* ======================= Clipes ======================= */

ClipsPage::ClipsPage(QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(24, 20, 24, 20);
	v->addWidget(title("Clip It", "Replay instantâneo: salve os últimos segundos sem interromper a gravação"));
	auto *bar = new QHBoxLayout();
	toggle_ = new QPushButton();
	toggle_->setObjectName("primary");
	save_ = new QPushButton("💾 Salvar clipe agora");
	auto *cfg = new QPushButton("Configurar...");
	status_ = new QLabel();
	status_->setObjectName("hint");
	bar->addWidget(toggle_);
	bar->addWidget(save_);
	bar->addWidget(cfg);
	bar->addStretch(1);
	v->addLayout(bar);
	v->addWidget(status_);

	list_ = new QTreeWidget();
	list_->setHeaderLabels({"Clipe", "Pasta", "Data", "Tamanho"});
	list_->setRootIsDecorated(false);
	list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
	list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	for (int c = 1; c < 4; c++) list_->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
	v->addWidget(list_, 1);
	auto *actions = new QHBoxLayout();
	auto *open = new QPushButton("▶ Abrir");
	auto *folder = new QPushButton("📂 Mostrar na pasta");
	auto *edit = new QPushButton("✂ Editar no FreeCut");
	edit->setObjectName("accent");
	auto *del = new QPushButton("🗑 Excluir");
	actions->addWidget(open);
	actions->addWidget(folder);
	actions->addWidget(edit);
	actions->addStretch(1);
	actions->addWidget(del);
	v->addLayout(actions);

	auto &clip = ClipIt::get();
	connect(toggle_, &QPushButton::clicked, this, [this, &clip]() {
		QString e;
		if (clip.isActive()) clip.stop();
		else if (!clip.start(&e)) QMessageBox::warning(this, "Clip It", e);
		QTimer::singleShot(300, this, &ClipsPage::updateStatus);
	});
	connect(save_, &QPushButton::clicked, this, [this, &clip]() {
		QString e;
		if (!clip.save(&e)) QMessageBox::warning(this, "Clip It", e);
	});
	connect(cfg, &QPushButton::clicked, this, [this]() { SettingsDialog(window(), 5).exec(); updateStatus(); });
	connect(&clip, &ClipIt::activeChanged, this, &ClipsPage::updateStatus);
	connect(&clip, &ClipIt::saved, this, &ClipsPage::refresh);
	connect(open, &QPushButton::clicked, this, [this]() { for (const auto &f : selectedFiles()) QDesktopServices::openUrl(QUrl::fromLocalFile(f)); });
	connect(list_, &QTreeWidget::itemDoubleClicked, this, [](QTreeWidgetItem *it) { QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(0, Qt::UserRole).toString())); });
	connect(folder, &QPushButton::clicked, this, [this]() {
		const auto f = selectedFiles();
		if (!f.isEmpty()) QProcess::startDetached("explorer.exe", {"/select," + QDir::toNativeSeparators(f.first())});
		else QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::get().clipsDir()));
	});
	connect(edit, &QPushButton::clicked, this, [this]() {
		const auto f = selectedFiles();
		if (f.isEmpty()) return;
		QMessageBox::information(this, "Editar no FreeCut", FreeCutBridge::open(f).message);
	});
	connect(del, &QPushButton::clicked, this, [this]() {
		const auto f = selectedFiles();
		if (f.isEmpty() || QMessageBox::question(this, "Excluir", QString("Mover %1 clipe(s) para a Lixeira?").arg(f.size())) != QMessageBox::Yes)
			return;
		for (const auto &x : f) QFile::moveToTrash(x);
		refresh();
	});
	auto *t = new QTimer(this);
	connect(t, &QTimer::timeout, this, &ClipsPage::updateStatus);
	t->start(2000);
	refresh();
}

QStringList ClipsPage::selectedFiles() const
{
	QStringList out;
	for (auto *it : list_->selectedItems())
		out << it->data(0, Qt::UserRole).toString();
	return out;
}

void ClipsPage::refresh()
{
	list_->clear();
	for (const auto &fi : ClipIt::get().clips()) {
		auto *it = new QTreeWidgetItem({fi.completeBaseName(), fi.dir().dirName(), fi.lastModified().toString("dd/MM/yyyy HH:mm"), human(fi.size())});
		it->setData(0, Qt::UserRole, fi.absoluteFilePath());
		list_->addTopLevelItem(it);
	}
	updateStatus();
}

void ClipsPage::updateStatus()
{
	auto &clip = ClipIt::get();
	const bool on = clip.isActive();
	toggle_->setText(on ? "■ Desligar Clip It" : "● Ligar Clip It");
	save_->setEnabled(on);
	const QStorageInfo disk(Settings::get().clipsDir());
	status_->setText(QString("%1 · Buffer de %2 s · memória estimada ~%3 MB · %4 livres no disco · %5 clipe(s) · atalho para salvar: Ctrl + Alt + C")
				 .arg(on ? "🟢 Ativo" : "⚪ Desligado").arg(clip.seconds()).arg(clip.estimatedMemoryMB())
				 .arg(human(disk.bytesAvailable())).arg(list_->topLevelItemCount()));
}

/* ======================= Game Mode ======================= */

static QProgressBar *bar(const QString &fmt)
{
	auto *b = new QProgressBar();
	b->setRange(0, 100);
	b->setFormat(fmt);
	b->setTextVisible(true);
	return b;
}

GameModePage::GameModePage(QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(24, 20, 24, 20);
	v->addWidget(title("Game Mode", "Detecta o jogo, aplica a captura certa e mantém a gravação estável sem pesar no FPS"));
	auto *top = new QHBoxLayout();
	toggle_ = new QPushButton();
	toggle_->setObjectName("primary");
	apply_ = new QPushButton("🎮 Capturar este jogo");
	apply_->setEnabled(false);
	top->addWidget(toggle_);
	top->addWidget(apply_);
	top->addStretch(1);
	v->addLayout(top);

	auto *row = new QHBoxLayout();
	auto *gameBox = new QGroupBox("Jogo detectado");
	auto *gv = new QVBoxLayout(gameBox);
	game_ = new QLabel("Nenhum jogo em primeiro plano. Abra o jogo com o Game Mode ligado.");
	game_->setWordWrap(true);
	note_ = new QLabel();
	note_->setObjectName("hint");
	note_->setWordWrap(true);
	auto *ov = new QFormLayout();
	ovFps_ = new QSpinBox();
	ovFps_->setRange(24, 120);
	ovFps_->setValue(60);
	ovHeight_ = new QSpinBox();
	ovHeight_->setRange(480, 2160);
	ovHeight_->setSingleStep(60);
	ovHeight_->setSuffix("p");
	ovHeight_->setValue(1080);
	saveOverride_ = new QPushButton("Salvar perfil deste jogo");
	saveOverride_->setEnabled(false);
	ov->addRow("FPS de gravação", ovFps_);
	ov->addRow("Resolução de saída", ovHeight_);
	gv->addWidget(game_);
	gv->addWidget(note_);
	gv->addLayout(ov);
	gv->addWidget(saveOverride_);
	gv->addStretch(1);
	auto *perfBox = new QGroupBox("Desempenho agora");
	auto *pv = new QVBoxLayout(perfBox);
	cpu_ = bar("CPU %p%");
	gpu_ = bar("GPU %p%");
	ram_ = bar("RAM %p%");
	stats_ = new QLabel();
	stats_->setObjectName("hint");
	tips_ = new QListWidget();
	tips_->setWordWrap(true);
	pv->addWidget(cpu_);
	pv->addWidget(gpu_);
	pv->addWidget(ram_);
	pv->addWidget(stats_);
	pv->addWidget(new QLabel("Sugestões"));
	pv->addWidget(tips_, 1);
	row->addWidget(gameBox, 1);
	row->addWidget(perfBox, 1);
	v->addLayout(row, 1);

	presets_ = new QTableWidget(int(GameMode::presets().size()), 3);
	presets_->setHorizontalHeaderLabels({"Jogo", "Configuração sugerida", "Dica"});
	presets_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	presets_->verticalHeader()->hide();
	presets_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	presets_->setMaximumHeight(200);
	int r = 0;
	for (const auto &p : GameMode::presets()) {
		presets_->setItem(r, 0, new QTableWidgetItem(p.name));
		presets_->setItem(r, 1, new QTableWidgetItem(QString("%1p · %2 fps · captura de jogo").arg(p.outH).arg(p.fps)));
		presets_->setItem(r, 2, new QTableWidgetItem(p.note.isEmpty() ? "—" : p.note));
		r++;
	}
	presets_->resizeColumnsToContents();
	v->addWidget(new QLabel("Presets incluídos"));
	v->addWidget(presets_);

	auto &gm = GameMode::get();
	auto syncToggle = [this](bool on) { toggle_->setText(on ? "■ Desligar Game Mode" : "● Ligar Game Mode"); };
	syncToggle(gm.isEnabled());
	connect(toggle_, &QPushButton::clicked, this, [&gm]() { gm.setEnabled(!gm.isEnabled()); });
	connect(&gm, &GameMode::enabledChanged, this, syncToggle);
	connect(&gm, &GameMode::gameChanged, this, &GameModePage::onGame);
	connect(&PerfMonitor::get(), &PerfMonitor::sample, this, &GameModePage::onPerf);
	connect(apply_, &QPushButton::clicked, this, [this]() {
		QString e;
		if (!GameMode::get().applyToScene(current_, &e)) { QMessageBox::warning(this, "Game Mode", e); return; }
		// aplica FPS/resolução do jogo (preset ou personalizado), se nada estiver ativo
		const QJsonObject o = GameMode::get().userOverrides(current_.exe);
		auto &p = Settings::get().p();
		const int fps = o.contains("fps") ? o["fps"].toInt() : (current_.preset ? current_.preset->fps : p.fps);
		const int h = o.contains("outH") ? o["outH"].toInt() : (current_.preset ? current_.preset->outH : 0);
		p.fps = fps;
		if (h > 0) {
			const auto vs = Engine::get().video();
			p.outH = h;
			p.outW = int(double(vs.baseWidth) * h / vs.baseHeight) & ~1;
		}
		Settings::get().save();
		const QString warn = applyProfile();
		QMessageBox::information(this, "Game Mode", "A cena “Game Mode” agora captura " + current_.exe + "." + (warn.isEmpty() ? "" : "\n\n" + warn));
	});
	connect(saveOverride_, &QPushButton::clicked, this, [this]() {
		GameMode::get().setUserOverrides(current_.exe, QJsonObject{{"fps", ovFps_->value()}, {"outH", ovHeight_->value()}});
		QMessageBox::information(this, "Game Mode", "Perfil salvo para " + current_.exe + ". Ele será usado ao clicar em “Capturar este jogo”.");
	});
	onGame(gm.current());
}

void GameModePage::onGame(const DetectedGame &g)
{
	current_ = g;
	const bool has = !g.exe.isEmpty();
	apply_->setEnabled(has);
	saveOverride_->setEnabled(has);
	if (!has) {
		game_->setText(GameMode::get().isEnabled() ? "Aguardando um jogo em primeiro plano..." : "Ligue o Game Mode e abra um jogo.");
		note_->clear();
		return;
	}
	game_->setText(QString("<b style='font-size:13pt'>%1</b><br>%2<br><span style='color:#8a93a6'>%3%4</span>")
			       .arg(g.preset ? g.preset->name : g.exe, g.title.toHtmlEscaped(), g.exe, g.fullscreen ? " · tela cheia" : ""));
	note_->setText(g.preset && !g.preset->note.isEmpty() ? "Dica: " + g.preset->note : "Jogo sem preset: usando configurações gerais.");
	const QJsonObject o = GameMode::get().userOverrides(g.exe);
	ovFps_->setValue(o.contains("fps") ? o["fps"].toInt() : (g.preset ? g.preset->fps : 60));
	ovHeight_->setValue(o.contains("outH") ? o["outH"].toInt() : (g.preset ? g.preset->outH : 1080));
}

void GameModePage::onPerf(const PerfSample &s)
{
	if (!isVisible())
		return;
	cpu_->setValue(qRound(s.cpuSystem));
	gpu_->setValue(s.gpu >= 0 ? qRound(s.gpu) : 0);
	gpu_->setFormat(s.gpu >= 0 ? "GPU %p%" : "GPU indisponível");
	ram_->setValue(s.ramTotalGB > 0 ? qRound(100 * s.ramUsedGB / s.ramTotalGB) : 0);
	stats_->setText(QString("StreamX: CPU %1% · RAM %2 MB · %3 fps · renderização %4 ms · atrasados %5 · pulados %6")
				.arg(s.cpuApp, 0, 'f', 1).arg(qRound(s.ramAppMB)).arg(s.fps, 0, 'f', 0).arg(s.renderMs, 0, 'f', 2)
				.arg(s.laggedFrames).arg(s.skippedFrames));
	tips_->clear();
	tips_->addItems(GameMode::get().suggestions(s));
}

/* ======================= Assistente ======================= */

AssistantPage::AssistantPage(QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(24, 20, 24, 20);
	v->addWidget(title("Assistente inteligente", "Analisa o seu computador e recomenda resolução, FPS, bitrate e codificador. Funciona offline e sem serviços pagos."));
	auto *row = new QHBoxLayout();
	goal_ = new QComboBox();
	goal_->addItem("Gravar vídeos", "gravar");
	goal_->addItem("Transmitir ao vivo", "transmitir");
	goal_->addItem("Gravar jogos", "jogos");
	auto *go = new QPushButton("🔍 Analisar meu PC");
	go->setObjectName("primary");
	row->addWidget(new QLabel("Objetivo:"));
	row->addWidget(goal_);
	row->addWidget(go);
	row->addStretch(1);
	v->addLayout(row);
	hw_ = new QLabel();
	hw_->setWordWrap(true);
	hw_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	result_ = new QLabel();
	result_->setWordWrap(true);
	reasons_ = new QListWidget();
	reasons_->setWordWrap(true);
	apply_ = new QPushButton("✓ Aplicar recomendações ao perfil");
	apply_->setObjectName("accent");
	apply_->setEnabled(false);
	v->addWidget(hw_);
	v->addWidget(result_);
	v->addWidget(reasons_, 1);
	v->addWidget(apply_, 0, Qt::AlignLeft);
	connect(go, &QPushButton::clicked, this, &AssistantPage::analyze);
}

void AssistantPage::analyze()
{
	const HardwareInfo hw = HardwareInfo::detect();
	QStringList gpus;
	for (const auto &g : hw.gpus)
		gpus << QString("%1 (%2 GB)").arg(g.name).arg(g.vramGB, 0, 'f', 1).replace('.', ',');
	hw_->setText(QString("<b>Seu computador</b><br>CPU: %1 (%2 threads) · RAM: %3 GB<br>GPU: %4<br>Monitor: %5×%6 @ %7 Hz · Codificadores: %8")
			     .arg(hw.cpuName).arg(hw.cpuThreads).arg(qRound(hw.ramGB)).arg(gpus.join(", "))
			     .arg(hw.screenW).arg(hw.screenH).arg(hw.refreshHz).arg(hw.encoders.join(", ")));
	const Recommendation r = recommend(hw, goal_->currentData().toString());
	result_->setText(QString("<div style='font-size:12pt;margin-top:8px'><b>Recomendado (equipamento %1):</b> %2×%3 @ %4 fps · %5 · gravação qualidade %6 · transmissão %7 kbps</div>")
				 .arg(r.tier).arg(r.outW).arg(r.outH).arg(r.fps).arg(r.encoderName).arg(r.recQuality).arg(r.streamBitrate));
	reasons_->clear();
	for (const auto &x : r.reasons)
		reasons_->addItem("• " + x);
	apply_->setEnabled(true);
	disconnect(apply_, nullptr, this, nullptr);
	connect(apply_, &QPushButton::clicked, this, [this, r]() {
		auto &p = Settings::get().p();
		p.outW = r.outW; p.outH = r.outH; p.fps = r.fps;
		p.recEncoder = r.encoderId; p.streamEncoder = r.encoderId;
		p.recConstantQuality = true; p.recQuality = r.recQuality;
		p.streamBitrate = r.streamBitrate; p.recFormat = r.recFormat;
		Settings::get().save();
		const QString warn = applyProfile();
		QMessageBox::information(this, "Assistente", "Recomendações aplicadas ao perfil “" + Settings::get().currentProfile() + "”." + (warn.isEmpty() ? "" : "\n\n" + warn));
	});
}

/* ======================= Controle remoto ======================= */

ControlPage::ControlPage(QWidget *parent) : QWidget(parent)
{
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(24, 20, 24, 20);
	v->addWidget(title("StreamX Control", "Controle a gravação, a transmissão, os clipes e as cenas pelo celular ou outro PC da sua rede"));
	state_ = new QLabel();
	state_->setWordWrap(true);
	urls_ = new QLabel();
	urls_->setTextInteractionFlags(Qt::TextBrowserInteraction);
	urls_->setOpenExternalLinks(true);
	urls_->setWordWrap(true);
	auto *cfg = new QPushButton("Configurar (ligar, rede local, token)...");
	log_ = new QPlainTextEdit();
	log_->setReadOnly(true);
	log_->setMaximumBlockCount(300);
	auto *api = new QLabel("<b>API</b> (cabeçalho <code>Authorization: Bearer &lt;token&gt;</code>): GET /api/status · POST /api/record/start|stop|toggle · "
			       "POST /api/stream/start|stop · POST /api/clip/start|stop|save · POST /api/scene {\"name\": \"...\"}");
	api->setWordWrap(true);
	api->setObjectName("hint");
	v->addWidget(state_);
	v->addWidget(urls_);
	v->addWidget(cfg, 0, Qt::AlignLeft);
	v->addWidget(api);
	v->addWidget(new QLabel("Acessos recentes"));
	v->addWidget(log_, 1);
	connect(cfg, &QPushButton::clicked, this, [this]() { SettingsDialog(window(), 7).exec(); refresh(); });
	connect(&ControlServer::get(), &ControlServer::request, this, [this](const QString &peer, const QString &line, int st) {
		log_->appendPlainText(QString("%1  %2  %3  → %4").arg(QDateTime::currentDateTime().toString("HH:mm:ss"), peer, line).arg(st));
	});
	refresh();
}

void ControlPage::refresh()
{
	auto &c = ControlServer::get();
	const auto &p = Settings::get().p();
	if (!c.isRunning()) {
		state_->setText("⚪ Desligado. Ative em Configurações → Controle remoto.");
		urls_->clear();
		return;
	}
	state_->setText(QString("🟢 Ativo na porta %1 · %2 · Token: <code>%3</code>")
				.arg(c.port()).arg(p.controlLan ? "aceita a rede local" : "somente este PC").arg(p.controlToken));
	QStringList links;
	for (const auto &u : c.addresses())
		links << QString("<a href='%1' style='color:#4da3ff'>%1</a>").arg(u);
	urls_->setText("Abra no navegador: " + links.join("  ·  "));
}

} // namespace sx
