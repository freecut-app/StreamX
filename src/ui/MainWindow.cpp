#include "MainWindow.h"
#include "MixerWidget.h"
#include "Pages.h"
#include "PreviewWidget.h"
#include "SettingsDialog.h"
#include "SourceDialogs.h"
#include "../core/Apply.h"
#include "../core/ClipIt.h"
#include "../core/Engine.h"
#include "../core/GameMode.h"
#include "../core/Hotkeys.h"
#include "../core/Log.h"
#include "../core/Recorder.h"
#include "../core/SceneManager.h"
#include "../core/Settings.h"
#include "../core/Streamer.h"
#include "../integration/FreeCutBridge.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace sx {

static QString hms(qint64 ms)
{
	const qint64 s = ms / 1000;
	return QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}

static QPushButton *toolBtn(const QString &text, const QString &tip)
{
	auto *b = new QPushButton(text);
	b->setObjectName("tool");
	b->setToolTip(tip);
	b->setFixedSize(30, 28);
	return b;
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	setWindowTitle("StreamX");
	setWindowIcon(QIcon(":/icons/streamx.png"));
	resize(1500, 920);
	auto *root = new QWidget(this);
	root->setObjectName("root");
	auto *v = new QVBoxLayout(root);
	v->setContentsMargins(0, 0, 0, 0);
	v->setSpacing(0);
	v->addWidget(buildTopBar());
	auto *h = new QHBoxLayout();
	h->setSpacing(0);
	h->addWidget(buildNav());
	pages_ = new QStackedWidget();
	pages_->addWidget(buildStudio());
	clipsPage_ = new ClipsPage();
	pages_->addWidget(clipsPage_);
	pages_->addWidget(new GameModePage());
	pages_->addWidget(new AssistantPage());
	controlPage_ = new ControlPage();
	pages_->addWidget(controlPage_);
	h->addWidget(pages_, 1);
	connect(pages_, &QStackedWidget::currentChanged, this, [this](int i) { if (auto *b = navGroup_->button(i)) b->setChecked(true); });
	v->addLayout(h, 1);
	setCentralWidget(root);

	recInfo_ = new QLabel();
	streamInfo_ = new QLabel();
	clipInfo_ = new QLabel();
	toast_ = new QLabel();
	toast_->setObjectName("toast");
	statusBar()->addWidget(toast_, 1);
	statusBar()->addPermanentWidget(clipInfo_);
	statusBar()->addPermanentWidget(streamInfo_);
	statusBar()->addPermanentWidget(recInfo_);

	auto &sm = SceneManager::get();
	connect(&sm, &SceneManager::scenesChanged, this, &MainWindow::refreshScenes);
	connect(&sm, &SceneManager::currentSceneChanged, this, &MainWindow::refreshScenes);
	connect(&sm, &SceneManager::itemsChanged, this, &MainWindow::refreshSources);
	connect(&sm, &SceneManager::scenesChanged, &Hotkeys::get(), &Hotkeys::refreshSceneActions);
	connect(&Recorder::get(), &Recorder::stopped, this, [this](const QString &path, int code, const QString &err) {
		updateControls();
		if (code != 0) notify("A gravação parou com erro: " + err, true);
		else notify("Gravação salva: " + QDir::toNativeSeparators(path));
	});
	connect(&Recorder::get(), &Recorder::started, this, [this]() { updateControls(); });
	connect(&Streamer::get(), &Streamer::stopped, this, [this](int code, const QString &err) {
		updateControls();
		if (code != 0) notify("A transmissão parou: " + (err.isEmpty() ? QString("verifique o servidor, a chave e a internet") : err), true);
	});
	connect(&Streamer::get(), &Streamer::reconnecting, this, [this](bool on) { if (on) notify("Conexão caiu: reconectando a transmissão...", true); });
	connect(&ClipIt::get(), &ClipIt::activeChanged, this, [this]() { updateControls(); });
	connect(&ClipIt::get(), &ClipIt::saved, this, [this](const QString &p) { notify("🎬 Clipe salvo: " + QDir::toNativeSeparators(p)); });
	connect(&ClipIt::get(), &ClipIt::failed, this, [this](const QString &e) { notify("Clip It: " + e, true); });
	connect(&GameMode::get(), &GameMode::enabledChanged, this, [this](bool on) { gameChip_->setChecked(on); });
	connect(&GameMode::get(), &GameMode::gameChanged, this, [this](const DetectedGame &g) {
		gameChip_->setText(g.exe.isEmpty() ? "🎮 Game Mode" : "🎮 " + (g.preset ? g.preset->name : g.exe));
		if (!g.exe.isEmpty()) notify("Game Mode: " + (g.preset ? g.preset->name : g.exe) + " detectado");
	});
	connect(&PerfMonitor::get(), &PerfMonitor::sample, this, &MainWindow::onPerf);
	connect(&Settings::get(), &Settings::profileChanged, this, [this]() {
		refreshProfiles();
		const QString warn = applyProfile();
		if (!warn.isEmpty()) notify(warn, true);
	});

	auto *t = new QTimer(this);
	connect(t, &QTimer::timeout, this, &MainWindow::updateControls);
	t->start(500);
	refreshScenes();
	refreshSources();
	refreshProfiles();
	updateControls();
}

/* ======================= estrutura ======================= */

QWidget *MainWindow::buildTopBar()
{
	auto *bar = new QWidget();
	bar->setObjectName("topBar");
	auto *h = new QHBoxLayout(bar);
	h->setContentsMargins(16, 8, 16, 8);
	auto *logo = new QLabel("<span style='font-size:15pt;font-weight:800;color:#e6e9f0'>Stream</span><span style='font-size:15pt;font-weight:800;color:#1f8bff'>X</span>");
	h->addWidget(logo);
	h->addSpacing(18);
	h->addWidget(new QLabel("Perfil"));
	profile_ = new QComboBox();
	profile_->setMinimumWidth(170);
	h->addWidget(profile_);
	auto *pm = toolBtn("⋯", "Novo, duplicar, renomear ou excluir perfil");
	h->addWidget(pm);
	connect(pm, &QPushButton::clicked, this, &MainWindow::profileMenu);
	connect(profile_, &QComboBox::activated, this, [this]() {
		const QString n = profile_->currentText();
		if (n == Settings::get().currentProfile()) return;
		if (anyOutputActive()) {
			notify("Pare a gravação, a transmissão e o Clip It antes de trocar de perfil.", true);
			refreshProfiles();
			return;
		}
		Hotkeys::get().saveToProfile();
		Settings::get().switchProfile(n);
	});
	h->addStretch(1);
	for (QLabel **l : {&cpu_, &gpu_, &ram_, &fps_}) {
		*l = new QLabel("—");
		(*l)->setObjectName("chip");
		h->addWidget(*l);
	}
	gameChip_ = new QPushButton("🎮 Game Mode");
	gameChip_->setObjectName("gameChip");
	gameChip_->setCheckable(true);
	gameChip_->setToolTip("Detecta o jogo em primeiro plano e otimiza a captura");
	connect(gameChip_, &QPushButton::clicked, this, [](bool on) { GameMode::get().setEnabled(on); });
	h->addSpacing(8);
	h->addWidget(gameChip_);
	return bar;
}

QWidget *MainWindow::buildNav()
{
	auto *nav = new QWidget();
	nav->setObjectName("nav");
	nav->setFixedWidth(86);
	auto *v = new QVBoxLayout(nav);
	v->setContentsMargins(8, 12, 8, 12);
	v->setSpacing(4);
	navGroup_ = new QButtonGroup(this);
	const QList<QPair<QString, QString>> items = {{"◉", "Estúdio"}, {"🎬", "Clipes"}, {"🎮", "Game Mode"}, {"✦", "Assistente"}, {"📡", "Controle"}};
	for (int i = 0; i < items.size(); i++) {
		auto *b = new QPushButton(items[i].first + "\n" + items[i].second);
		b->setObjectName("navBtn");
		b->setCheckable(true);
		b->setChecked(i == 0);
		navGroup_->addButton(b, i);
		v->addWidget(b);
	}
	connect(navGroup_, &QButtonGroup::idClicked, this, [this](int id) {
		pages_->setCurrentIndex(id);
		if (id == 1) clipsPage_->refresh();
		if (id == 4) controlPage_->refresh();
	});
	v->addStretch(1);
	auto *cfg = new QPushButton("⚙\nAjustes");
	cfg->setObjectName("navBtn");
	connect(cfg, &QPushButton::clicked, this, [this]() { SettingsDialog(this).exec(); controlPage_->refresh(); });
	v->addWidget(cfg);
	return nav;
}

QWidget *MainWindow::panel(const QString &title, QWidget *content, QWidget *toolbar)
{
	auto *p = new QWidget();
	p->setObjectName("panel");
	auto *v = new QVBoxLayout(p);
	v->setContentsMargins(10, 8, 10, 10);
	v->setSpacing(6);
	auto *head = new QHBoxLayout();
	auto *t = new QLabel(title.toUpper());
	t->setObjectName("panelTitle");
	head->addWidget(t);
	head->addStretch(1);
	if (toolbar)
		head->addWidget(toolbar);
	v->addLayout(head);
	v->addWidget(content, 1);
	return p;
}

QWidget *MainWindow::buildStudio()
{
	auto *split = new QSplitter(Qt::Vertical);
	split->setObjectName("studioSplit");

	auto *top = new QWidget();
	auto *tv = new QVBoxLayout(top);
	tv->setContentsMargins(14, 12, 14, 4);
	preview_ = new PreviewWidget();
	tv->addWidget(preview_, 1);
	split->addWidget(top);

	auto *bottom = new QWidget();
	auto *bh = new QHBoxLayout(bottom);
	bh->setContentsMargins(14, 6, 14, 12);
	bh->setSpacing(10);

	// ---- cenas
	scenes_ = new QListWidget();
	scenes_->setObjectName("list");
	auto *stb = new QWidget();
	auto *sth = new QHBoxLayout(stb);
	sth->setContentsMargins(0, 0, 0, 0);
	sth->setSpacing(2);
	auto *addScene = toolBtn("＋", "Nova cena");
	auto *delScene = toolBtn("−", "Excluir cena");
	sth->addWidget(addScene);
	sth->addWidget(delScene);
	connect(addScene, &QPushButton::clicked, this, [this]() {
		bool ok = false;
		const QString n = QInputDialog::getText(this, "Nova cena", "Nome da cena:", QLineEdit::Normal, SceneManager::get().uniqueSourceName("Cena"), &ok);
		if (ok && !n.trimmed().isEmpty()) {
			SceneManager::get().addScene(n);
			SceneManager::get().setCurrentScene(SceneManager::get().sceneNames().last());
		}
	});
	connect(delScene, &QPushButton::clicked, this, [this]() {
		auto *it = scenes_->currentItem();
		if (!it) return;
		if (SceneManager::get().sceneNames().size() <= 1) { notify("É preciso ter pelo menos uma cena.", true); return; }
		if (QMessageBox::question(this, "Excluir cena", "Excluir a cena “" + it->text() + "”?") == QMessageBox::Yes)
			SceneManager::get().removeScene(it->text());
	});
	connect(scenes_, &QListWidget::currentTextChanged, this, [this](const QString &n) {
		if (!updatingLists_ && !n.isEmpty() && n != SceneManager::get().currentSceneName())
			SceneManager::get().setCurrentScene(n);
	});
	scenes_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(scenes_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		auto *it = scenes_->itemAt(pos);
		if (!it) return;
		QMenu m;
		m.addAction("Renomear", this, [this, it]() {
			bool ok = false;
			const QString old = it->text();
			const QString n = QInputDialog::getText(this, "Renomear cena", "Novo nome:", QLineEdit::Normal, old, &ok);
			if (ok && !SceneManager::get().renameScene(old, n)) notify("Nome inválido ou já usado.", true);
		});
		m.addAction("Excluir", this, [this, it]() { SceneManager::get().removeScene(it->text()); });
		m.exec(scenes_->mapToGlobal(pos));
	});
	bh->addWidget(panel("Cenas", scenes_, stb), 2);

	// ---- fontes
	sources_ = new QListWidget();
	sources_->setObjectName("list");
	sources_->setContextMenuPolicy(Qt::CustomContextMenu);
	auto *ftb = new QWidget();
	auto *fth = new QHBoxLayout(ftb);
	fth->setContentsMargins(0, 0, 0, 0);
	fth->setSpacing(2);
	auto *addSrc = toolBtn("＋", "Adicionar fonte");
	auto *delSrc = toolBtn("−", "Remover fonte");
	auto *props = toolBtn("⚙", "Propriedades");
	auto *filt = toolBtn("✧", "Filtros");
	auto *up = toolBtn("▲", "Subir");
	auto *down = toolBtn("▼", "Descer");
	for (auto *b : {addSrc, delSrc, props, filt, up, down}) fth->addWidget(b);
	connect(addSrc, &QPushButton::clicked, this, [this]() { showAddSourceDialog(this); });
	connect(delSrc, &QPushButton::clicked, this, [this]() {
		const int64_t id = currentItemId();
		if (id && QMessageBox::question(this, "Remover fonte", "Remover “" + sources_->currentItem()->text().trimmed() + "” desta cena?") == QMessageBox::Yes)
			SceneManager::get().removeItem(id);
	});
	auto withSource = [this](auto fn) {
		if (obs_sceneitem_t *it = SceneManager::get().item(currentItemId()))
			fn(obs_sceneitem_get_source(it));
	};
	connect(props, &QPushButton::clicked, this, [this, withSource]() { withSource([this](obs_source_t *s) { PropertiesDialog(s, this).exec(); }); });
	connect(filt, &QPushButton::clicked, this, [this, withSource]() { withSource([this](obs_source_t *s) { FiltersDialog(s, this).exec(); }); });
	connect(up, &QPushButton::clicked, this, [this]() { SceneManager::get().moveItem(currentItemId(), true); });
	connect(down, &QPushButton::clicked, this, [this]() { SceneManager::get().moveItem(currentItemId(), false); });
	connect(sources_, &QListWidget::customContextMenuRequested, this, &MainWindow::sourceMenu);
	connect(sources_, &QListWidget::itemDoubleClicked, this, [this, withSource]() { withSource([this](obs_source_t *s) { PropertiesDialog(s, this).exec(); }); });
	connect(sources_, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
		if (!updatingLists_)
			SceneManager::get().setItemVisible(it->data(Qt::UserRole).toLongLong(), it->checkState() == Qt::Checked);
	});
	connect(sources_, &QListWidget::currentRowChanged, this, [this]() { if (!updatingLists_) preview_->setSelected(currentItemId()); });
	connect(preview_, &PreviewWidget::itemSelected, this, [this](int64_t id) {
		for (int i = 0; i < sources_->count(); i++)
			if (sources_->item(i)->data(Qt::UserRole).toLongLong() == id) { sources_->setCurrentRow(i); return; }
		sources_->clearSelection();
	});
	bh->addWidget(panel("Fontes", sources_, ftb), 3);

	// ---- mixer
	mixer_ = new MixerWidget();
	bh->addWidget(panel("Mixer de áudio", mixer_), 4);

	// ---- transição + controles
	auto *ctl = new QWidget();
	auto *cv = new QVBoxLayout(ctl);
	cv->setContentsMargins(0, 0, 0, 0);
	cv->setSpacing(6);
	auto *trRow = new QHBoxLayout();
	transition_ = new QComboBox();
	for (const auto &t : SceneManager::transitionTypes())
		if (t.id != "obs_stinger_transition") transition_->addItem(t.name, t.id);
	transitionMs_ = new QSpinBox();
	transitionMs_->setRange(50, 5000);
	transitionMs_->setSingleStep(50);
	transitionMs_->setSuffix(" ms");
	transitionMs_->setValue(SceneManager::get().transitionDuration());
	transition_->setCurrentIndex(qMax(0, transition_->findData(SceneManager::get().transitionType())));
	auto applyTr = [this]() { SceneManager::get().setTransition(transition_->currentData().toString(), transitionMs_->value()); };
	connect(transition_, &QComboBox::currentIndexChanged, this, applyTr);
	connect(transitionMs_, &QSpinBox::valueChanged, this, applyTr);
	trRow->addWidget(transition_, 1);
	trRow->addWidget(transitionMs_);
	cv->addLayout(trRow);
	recBtn_ = new QPushButton();
	recBtn_->setObjectName("recordButton");
	streamBtn_ = new QPushButton();
	streamBtn_->setObjectName("streamButton");
	clipBtn_ = new QPushButton();
	clipBtn_->setObjectName("clipButton");
	clipSave_ = new QPushButton("💾 Salvar clipe");
	editBtn_ = new QPushButton("✂ Editar no FreeCut");
	auto *folder = new QPushButton("📂 Gravações");
	for (auto *b : {recBtn_, streamBtn_}) b->setMinimumHeight(42);
	cv->addWidget(recBtn_);
	cv->addWidget(streamBtn_);
	auto *clipRow = new QHBoxLayout();
	clipRow->addWidget(clipBtn_, 1);
	clipRow->addWidget(clipSave_, 1);
	cv->addLayout(clipRow);
	auto *misc = new QHBoxLayout();
	misc->addWidget(editBtn_, 1);
	misc->addWidget(folder);
	cv->addLayout(misc);
	cv->addStretch(1);
	connect(recBtn_, &QPushButton::clicked, this, &MainWindow::toggleRecording);
	connect(streamBtn_, &QPushButton::clicked, this, &MainWindow::toggleStreaming);
	connect(clipBtn_, &QPushButton::clicked, this, &MainWindow::toggleClipIt);
	connect(clipSave_, &QPushButton::clicked, this, &MainWindow::saveClip);
	connect(editBtn_, &QPushButton::clicked, this, [this]() {
		const QString f = Recorder::get().lastFile();
		if (f.isEmpty() || Recorder::get().isActive()) { notify("Grave algo primeiro (e pare a gravação) para editar no FreeCut.", true); return; }
		const auto r = FreeCutBridge::open({f});
		notify(r.message, !r.launched);
	});
	connect(folder, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl::fromLocalFile(Settings::get().recordingDir())); });
	bh->addWidget(panel("Transição e controles", ctl), 3);

	split->addWidget(bottom);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	split->setSizes({560, 300});
	return split;
}

/* ======================= listas ======================= */

void MainWindow::refreshScenes()
{
	updatingLists_ = true;
	const QString cur = SceneManager::get().currentSceneName();
	scenes_->clear();
	for (const auto &n : SceneManager::get().sceneNames()) {
		scenes_->addItem(n);
		if (n == cur) scenes_->setCurrentRow(scenes_->count() - 1);
	}
	updatingLists_ = false;
	refreshSources();
}

void MainWindow::refreshSources()
{
	updatingLists_ = true;
	const int64_t keep = preview_->selected();
	sources_->clear();
	for (const auto &it : SceneManager::get().items()) {
		auto *li = new QListWidgetItem(typeIcon(it.typeId) + "  " + it.name + (it.locked ? "  🔒" : ""));
		li->setData(Qt::UserRole, qlonglong(it.id));
		li->setFlags(li->flags() | Qt::ItemIsUserCheckable);
		li->setCheckState(it.visible ? Qt::Checked : Qt::Unchecked);
		li->setToolTip("Marque/desmarque para mostrar ou ocultar · duplo clique abre as propriedades");
		sources_->addItem(li);
		if (it.id == keep) sources_->setCurrentItem(li);
	}
	if (!sources_->currentItem())
		preview_->setSelected(0);
	updatingLists_ = false;
}

void MainWindow::refreshProfiles()
{
	profile_->clear();
	profile_->addItems(Settings::get().profiles());
	profile_->setCurrentText(Settings::get().currentProfile());
}

int64_t MainWindow::currentItemId() const
{
	auto *it = sources_->currentItem();
	return it ? it->data(Qt::UserRole).toLongLong() : 0;
}

void MainWindow::sourceMenu(const QPoint &pos)
{
	auto *li = sources_->itemAt(pos);
	if (!li) {
		QMenu m;
		m.addAction("Adicionar fonte...", this, [this]() { showAddSourceDialog(this); });
		m.exec(sources_->mapToGlobal(pos));
		return;
	}
	const int64_t id = li->data(Qt::UserRole).toLongLong();
	obs_sceneitem_t *item = SceneManager::get().item(id);
	if (!item) return;
	obs_source_t *src = obs_sceneitem_get_source(item);
	QMenu m;
	m.addAction("Propriedades", this, [this, src]() { PropertiesDialog(src, this).exec(); });
	m.addAction("Filtros", this, [this, src]() { FiltersDialog(src, this).exec(); });
	m.addSeparator();
	m.addAction("Ajustar à tela", this, [id]() { SceneManager::get().fitItem(id); });
	const bool locked = obs_sceneitem_locked(item);
	m.addAction(locked ? "Desbloquear posição" : "Bloquear posição", this, [id, locked]() { SceneManager::get().setItemLocked(id, !locked); });
	m.addAction("Renomear", this, [this, src]() {
		bool ok = false;
		const QString n = QInputDialog::getText(this, "Renomear fonte", "Novo nome:", QLineEdit::Normal, QString::fromUtf8(obs_source_get_name(src)), &ok);
		if (!ok || n.trimmed().isEmpty()) return;
		OBSSourceAutoRelease ex = obs_get_source_by_name(n.trimmed().toUtf8().constData());
		if (ex) { notify("Já existe uma fonte com esse nome.", true); return; }
		obs_source_set_name(src, n.trimmed().toUtf8().constData());
		refreshSources();
	});
	m.addSeparator();
	m.addAction("Remover desta cena", this, [id]() { SceneManager::get().removeItem(id); });
	m.exec(sources_->mapToGlobal(pos));
}

void MainWindow::profileMenu()
{
	auto &s = Settings::get();
	QMenu m;
	auto ask = [this](const QString &title, const QString &def) {
		bool ok = false;
		const QString n = QInputDialog::getText(this, title, "Nome do perfil:", QLineEdit::Normal, def, &ok);
		return ok ? n.trimmed() : QString();
	};
	auto guard = [this]() {
		if (!anyOutputActive()) return true;
		notify("Pare a gravação, a transmissão e o Clip It antes de mudar de perfil.", true);
		return false;
	};
	m.addAction("Novo perfil (padrão)", this, [&, ask, guard]() {
		if (!guard()) return;
		const QString n = ask("Novo perfil", "Novo perfil");
		if (!n.isEmpty() && !s.createProfile(n, false)) notify("Já existe um perfil com esse nome.", true);
	});
	m.addAction("Duplicar perfil atual", this, [&, ask, guard]() {
		if (!guard()) return;
		const QString n = ask("Duplicar perfil", s.currentProfile() + " (cópia)");
		if (!n.isEmpty() && !s.createProfile(n, true)) notify("Já existe um perfil com esse nome.", true);
	});
	m.addAction("Renomear perfil atual", this, [&, ask]() {
		const QString n = ask("Renomear perfil", s.currentProfile());
		if (!n.isEmpty() && !s.renameProfile(s.currentProfile(), n)) notify("Nome inválido ou já usado.", true);
	});
	auto *del = m.addMenu("Excluir perfil");
	for (const auto &p : s.profiles())
		if (p != s.currentProfile())
			del->addAction(p, this, [&, p]() {
				if (QMessageBox::question(this, "Excluir perfil", "Excluir o perfil “" + p + "”?") == QMessageBox::Yes) {
					s.deleteProfile(p);
					refreshProfiles();
				}
			});
	del->setEnabled(!del->isEmpty());
	m.exec(QCursor::pos());
}

/* ======================= ações ======================= */

void MainWindow::notify(const QString &msg, bool error)
{
	toast_->setText(msg);
	toast_->setProperty("error", error);
	toast_->style()->unpolish(toast_);
	toast_->style()->polish(toast_);
	(error ? logWarn(msg) : logInfo(msg));
	QTimer::singleShot(9000, toast_, [this, msg]() { if (toast_->text() == msg) toast_->clear(); });
}

void MainWindow::toggleRecording()
{
	auto &r = Recorder::get();
	if (r.isActive()) { r.stop(); return; }
	QString e;
	if (!r.start(&e)) QMessageBox::critical(this, "Gravação", e);
	updateControls();
}

void MainWindow::toggleStreaming()
{
	auto &s = Streamer::get();
	if (s.isActive()) {
		if (QMessageBox::question(this, "Transmissão", "Encerrar a transmissão ao vivo?") == QMessageBox::Yes) s.stop();
		return;
	}
	QString e;
	if (!s.start(&e)) {
		if (QMessageBox::warning(this, "Transmissão", e + "\n\nAbrir as configurações de transmissão?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
			SettingsDialog(this, 3).exec();
	}
	updateControls();
}

void MainWindow::toggleClipIt()
{
	auto &c = ClipIt::get();
	QString e;
	if (c.isActive()) c.stop();
	else if (!c.start(&e)) QMessageBox::warning(this, "Clip It", e);
	updateControls();
}

void MainWindow::saveClip()
{
	QString e;
	if (!ClipIt::get().save(&e)) notify(e, true);
}

void MainWindow::updateControls()
{
	auto &r = Recorder::get();
	auto &s = Streamer::get();
	auto &c = ClipIt::get();
	const bool rec = r.isActive(), live = s.isActive(), clip = c.isActive();
	recBtn_->setText(rec ? "■  Parar gravação   " + hms(r.elapsedMs()) : "●  Gravar");
	streamBtn_->setText(live ? "■  Encerrar transmissão   " + hms(s.elapsedMs()) : "◉  Transmitir ao vivo");
	clipBtn_->setText(clip ? QString("Clip It: %1 s ●").arg(c.seconds()) : "Clip It: desligado");
	clipSave_->setEnabled(clip);
	editBtn_->setEnabled(!rec && !r.lastFile().isEmpty());
	for (auto *b : {recBtn_, streamBtn_, clipBtn_}) {
		const bool on = (b == recBtn_ && rec) || (b == streamBtn_ && live) || (b == clipBtn_ && clip);
		if (b->property("active").toBool() != on) {
			b->setProperty("active", on);
			b->style()->unpolish(b);
			b->style()->polish(b);
		}
	}
	const QStorageInfo disk(Settings::get().recordingDir());
	recInfo_->setText(rec ? QString("🔴 REC %1 · %2 livres").arg(hms(r.elapsedMs())).arg(QString::number(disk.bytesAvailable() / 1073741824.0, 'f', 1) + " GB")
			      : QString("%1 GB livres").arg(disk.bytesAvailable() / 1073741824.0, 0, 'f', 1));
	if (live) {
		const auto st = s.stats();
		streamInfo_->setText(QString("📡 AO VIVO %1 · %2 kbps · perdidos %3%4")
					     .arg(hms(s.elapsedMs())).arg(qRound(st.kbps)).arg(st.dropped)
					     .arg(st.reconnecting ? " · reconectando..." : ""));
	} else
		streamInfo_->clear();
	clipInfo_->setText(clip ? QString("🎬 Clip It %1 s").arg(c.seconds()) : QString());
}

void MainWindow::onPerf(const PerfSample &s)
{
	cpu_->setText(QString("CPU %1%").arg(qRound(s.cpuSystem)));
	gpu_->setText(s.gpu >= 0 ? QString("GPU %1%").arg(qRound(s.gpu)) : "GPU —");
	ram_->setText(QString("RAM %1/%2 GB").arg(s.ramUsedGB, 0, 'f', 1).arg(qRound(s.ramTotalGB)));
	fps_->setText(QString("%1 fps").arg(s.fps, 0, 'f', 0));
	const auto setWarn = [](QLabel *l, bool w) {
		if (l->property("warn").toBool() == w) return;
		l->setProperty("warn", w);
		l->style()->unpolish(l);
		l->style()->polish(l);
	};
	setWarn(cpu_, s.cpuSystem >= 90);
	setWarn(gpu_, s.gpu >= 92);
	setWarn(ram_, s.ramTotalGB > 0 && s.ramUsedGB / s.ramTotalGB > 0.9);
	setWarn(fps_, s.fps > 0 && s.fps < Engine::get().video().fps * 0.9);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	QStringList active;
	if (Recorder::get().isActive()) active << "gravação";
	if (Streamer::get().isActive()) active << "transmissão";
	if (!active.isEmpty() && QMessageBox::question(this, "StreamX", "Há uma " + active.join(" e uma ") + " em andamento. Parar e sair?") != QMessageBox::Yes) {
		e->ignore();
		return;
	}
	Hotkeys::get().saveToProfile();
	e->accept();
}

} // namespace sx
