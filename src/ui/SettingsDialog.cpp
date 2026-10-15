#include "SettingsDialog.h"
#include "../core/Apply.h"
#include "../core/ClipIt.h"
#include "../core/Engine.h"
#include "../core/Hotkeys.h"
#include "../core/SceneManager.h"
#include "../core/Settings.h"
#include "../core/Streamer.h"
#include "../integration/FreeCutBridge.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace sx {

QString formatRes(int w, int h) { return QString("%1×%2").arg(w).arg(h); }

bool parseRes(const QString &s, int *w, int *h)
{
	const auto m = QRegularExpression(R"((\d+)\s*[x×X]\s*(\d+))").match(s);
	if (!m.hasMatch())
		return false;
	*w = m.captured(1).toInt();
	*h = m.captured(2).toInt();
	return *w >= 128 && *h >= 128 && *w <= 7680 && *h <= 4320;
}

/* ---------------- campo de atalho ---------------- */

HotkeyEdit::HotkeyEdit(QWidget *parent) : QLineEdit(parent)
{
	setReadOnly(true);
	setPlaceholderText("Clique e pressione as teclas");
}

void HotkeyEdit::setCombo(obs_key_combination_t c)
{
	combo_ = c;
	setText(c.key == OBS_KEY_NONE ? QString() : Hotkeys::comboText(c));
}

void HotkeyEdit::keyPressEvent(QKeyEvent *e)
{
	const int k = e->key();
	if (k == Qt::Key_Control || k == Qt::Key_Shift || k == Qt::Key_Alt || k == Qt::Key_Meta)
		return; // espera a tecla principal
	if ((k == Qt::Key_Backspace || k == Qt::Key_Delete) && e->modifiers() == Qt::NoModifier) {
		setCombo({0, OBS_KEY_NONE});
		return;
	}
	obs_key_combination_t c = {0, obs_key_from_virtual_key(int(e->nativeVirtualKey()))};
	if (e->modifiers() & Qt::ControlModifier) c.modifiers |= INTERACT_CONTROL_KEY;
	if (e->modifiers() & Qt::AltModifier) c.modifiers |= INTERACT_ALT_KEY;
	if (e->modifiers() & Qt::ShiftModifier) c.modifiers |= INTERACT_SHIFT_KEY;
	if (c.key != OBS_KEY_NONE)
		setCombo(c);
}

/* ---------------- diálogo ---------------- */

static QWidget *scrollWrap(QWidget *w)
{
	auto *s = new QScrollArea();
	s->setWidgetResizable(true);
	s->setFrameShape(QFrame::NoFrame);
	s->setWidget(w);
	return s;
}

static QLabel *hint(const QString &t)
{
	auto *l = new QLabel(t);
	l->setObjectName("hint");
	l->setWordWrap(true);
	return l;
}

SettingsDialog::SettingsDialog(QWidget *parent, int startTab) : QDialog(parent)
{
	setWindowTitle("Configurações — perfil " + Settings::get().currentProfile());
	resize(760, 620);
	auto *lay = new QVBoxLayout(this);
	tabs_ = new QTabWidget(this);
	tabs_->addTab(scrollWrap(generalTab()), "Geral");
	tabs_->addTab(scrollWrap(videoTab()), "Vídeo");
	tabs_->addTab(scrollWrap(recordTab()), "Gravação");
	tabs_->addTab(scrollWrap(streamTab()), "Transmissão");
	tabs_->addTab(scrollWrap(audioTab()), "Áudio");
	tabs_->addTab(scrollWrap(clipTab()), "Clip It");
	tabs_->addTab(scrollWrap(hotkeysTab()), "Atalhos");
	tabs_->addTab(scrollWrap(controlTab()), "Controle remoto");
	tabs_->setCurrentIndex(startTab);
	lay->addWidget(tabs_, 1);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	bb->button(QDialogButtonBox::Ok)->setText("Salvar");
	bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
	connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
	lay->addWidget(bb);
}

QWidget *SettingsDialog::generalTab()
{
	const auto &p = Settings::get().p();
	auto *w = new QWidget();
	auto *f = new QFormLayout(w);
	auto dirRow = [this, f](QLineEdit *&edit, const QString &label, const QString &value, const QString &placeholder) {
		auto *box = new QWidget();
		auto *h = new QHBoxLayout(box);
		h->setContentsMargins(0, 0, 0, 0);
		edit = new QLineEdit(value);
		edit->setPlaceholderText(placeholder);
		auto *b = new QPushButton("Procurar...");
		QLineEdit *e = edit;
		connect(b, &QPushButton::clicked, this, [this, e]() {
			const QString d = QFileDialog::getExistingDirectory(this, "Escolher pasta", e->text().isEmpty() ? e->placeholderText() : e->text());
			if (!d.isEmpty()) e->setText(QDir::toNativeSeparators(d));
		});
		h->addWidget(edit, 1);
		h->addWidget(b);
		f->addRow(label, box);
	};
	dirRow(recDir_, "Pasta das gravações", p.recDir, QDir::toNativeSeparators(Engine::defaultOutputDir()));
	dirRow(clipDir_, "Pasta dos clipes", p.clipDir, QDir::toNativeSeparators(Engine::defaultOutputDir() + "/Clipes"));
	auto *fcBox = new QWidget();
	auto *fh = new QHBoxLayout(fcBox);
	fh->setContentsMargins(0, 0, 0, 0);
	freecut_ = new QLineEdit(FreeCutBridge::customPath());
	const QString found = FreeCutBridge::findExecutable();
	freecut_->setPlaceholderText(found.isEmpty() ? "FreeCut não encontrado" : found);
	auto *fb = new QPushButton("Procurar...");
	connect(fb, &QPushButton::clicked, this, [this]() {
		const QString f = QFileDialog::getOpenFileName(this, "Localizar FreeCut.exe", QString(), "FreeCut (FreeCut.exe)");
		if (!f.isEmpty()) freecut_->setText(QDir::toNativeSeparators(f));
	});
	fh->addWidget(freecut_, 1);
	fh->addWidget(fb);
	f->addRow("FreeCut (editor)", fcBox);
	f->addRow(hint("Deixe em branco para detectar automaticamente. O botão “Editar no FreeCut” envia gravações e clipes para o editor."));
	f->addRow(hint("Os perfis (conjuntos de configurações) são criados e trocados no seletor de perfil, no topo da janela."));
	return w;
}

QWidget *SettingsDialog::videoTab()
{
	const auto &p = Settings::get().p();
	auto *w = new QWidget();
	auto *f = new QFormLayout(w);
	QSize screen(1920, 1080);
	if (QScreen *s = QGuiApplication::primaryScreen())
		screen = s->size() * s->devicePixelRatio();
	baseRes_ = new QComboBox();
	baseRes_->setEditable(true);
	baseRes_->addItem("Monitor principal (" + formatRes(screen.width(), screen.height()) + ")", "");
	for (const auto &r : {QSize(3840, 2160), QSize(2560, 1440), QSize(2560, 1080), QSize(1920, 1080), QSize(1280, 720)})
		baseRes_->addItem(formatRes(r.width(), r.height()), formatRes(r.width(), r.height()));
	if (p.baseW > 0) baseRes_->setCurrentText(formatRes(p.baseW, p.baseH));
	outRes_ = new QComboBox();
	outRes_->setEditable(true);
	outRes_->addItem("Automática (até 1080p, mesma proporção)", "");
	const double ar = double(screen.width()) / screen.height();
	for (int h : {2160, 1440, 1080, 900, 720, 540}) {
		const int wv = int(h * ar) & ~1;
		outRes_->addItem(formatRes(wv, h) + (h == 1080 ? "  (Full HD)" : h == 720 ? "  (HD)" : ""), formatRes(wv, h));
	}
	if (p.outW > 0) outRes_->setCurrentText(formatRes(p.outW, p.outH));
	fps_ = new QComboBox();
	for (int v : {24, 25, 30, 48, 50, 60, 120})
		fps_->addItem(QString::number(v) + " fps", v);
	fps_->setCurrentIndex(qMax(0, fps_->findData(p.fps)));
	f->addRow("Resolução base (tela)", baseRes_);
	f->addRow("Resolução de saída", outRes_);
	f->addRow("Taxa de quadros", fps_);
	f->addRow(hint("A base é a área onde as fontes são montadas. A saída é o que vai para a gravação e a transmissão; a redução de tamanho é feita na GPU."));
	return w;
}

QWidget *SettingsDialog::recordTab()
{
	const auto &p = Settings::get().p();
	auto *w = new QWidget();
	auto *f = new QFormLayout(w);
	recFormat_ = new QComboBox();
	recFormat_->addItem("MP4 híbrido (recomendado: não corrompe se o PC desligar)", "hybrid_mp4");
	recFormat_->addItem("MP4", "mp4");
	recFormat_->addItem("MKV", "mkv");
	recFormat_->addItem("MOV", "mov");
	recFormat_->setCurrentIndex(qMax(0, recFormat_->findData(p.recFormat)));
	recEncoder_ = new QComboBox();
	for (const auto &e : Engine::get().videoEncoders())
		recEncoder_->addItem(e.name + (e.hardware ? "  — GPU" : "  — CPU"), e.id);
	recEncoder_->setCurrentIndex(qMax(0, recEncoder_->findData(p.recEncoder)));
	recMode_ = new QComboBox();
	recMode_->addItem("Qualidade constante (recomendado)", true);
	recMode_->addItem("Taxa de bits constante", false);
	recMode_->setCurrentIndex(p.recConstantQuality ? 0 : 1);
	recQuality_ = new QSpinBox();
	recQuality_->setRange(1, 51);
	recQuality_->setValue(p.recQuality);
	recBitrate_ = new QSpinBox();
	recBitrate_->setRange(1000, 150000);
	recBitrate_->setSingleStep(1000);
	recBitrate_->setSuffix(" kbps");
	recBitrate_->setValue(p.recBitrate);
	auto sync = [this]() {
		const bool cq = recMode_->currentData().toBool();
		recQuality_->setEnabled(cq);
		recBitrate_->setEnabled(!cq);
	};
	connect(recMode_, &QComboBox::currentIndexChanged, this, sync);
	sync();
	auto *tr = new QWidget();
	auto *th = new QHBoxLayout(tr);
	th->setContentsMargins(0, 0, 0, 0);
	for (int i = 0; i < 6; i++) {
		auto *c = new QCheckBox(QString::number(i + 1));
		c->setChecked(p.recTracks & (1 << i));
		tracks_ << c;
		th->addWidget(c);
	}
	th->addStretch(1);
	f->addRow("Formato", recFormat_);
	f->addRow("Codificador", recEncoder_);
	f->addRow("Controle de taxa", recMode_);
	f->addRow("Qualidade (1–51)", recQuality_);
	f->addRow("Taxa de bits", recBitrate_);
	f->addRow("Faixas de áudio", tr);
	f->addRow(hint("Faixas: a faixa 1 tem tudo. Para separar, abra o menu “⋯” de cada fonte no mixer e marque, por exemplo, só a faixa 2 no microfone e só a 3 no jogo. Editores como o FreeCut e o DaVinci Resolve mostram cada faixa separada."));
	f->addRow(hint("Qualidade: 18 = quase sem perdas, 20 = excelente (padrão), 23 = arquivos menores."));
	return w;
}

void SettingsDialog::reloadServers()
{
	const QString svc = service_->currentData().toString();
	const QString keep = server_->currentData().toString().isEmpty() ? server_->currentText() : server_->currentData().toString();
	server_->clear();
	if (svc == "custom") {
		server_->setEditable(true);
		server_->setEditText(Settings::get().p().streamService == "custom" ? Settings::get().p().streamServer : QString());
		server_->lineEdit()->setPlaceholderText("rtmp://servidor/app  ou  rtmps://...");
		return;
	}
	server_->setEditable(false);
	for (const QString &s : Streamer::servers(svc)) {
		const auto parts = s.split('|');
		server_->addItem(parts.value(0), parts.value(1));
	}
	const int i = server_->findData(Settings::get().p().streamServer);
	server_->setCurrentIndex(i >= 0 ? i : 0);
	Q_UNUSED(keep);
}

QWidget *SettingsDialog::streamTab()
{
	const auto &p = Settings::get().p();
	auto *w = new QWidget();
	auto *f = new QFormLayout(w);
	service_ = new QComboBox();
	service_->addItem("Personalizado (RTMP/RTMPS)", "custom");
	const QStringList known = Streamer::services();
	for (const QString &s : {"Twitch", "YouTube - RTMPS", "Kick", "Facebook Live", "TikTok", "Trovo"})
		if (known.contains(s)) service_->addItem(s, s);
	for (const QString &s : known)
		if (service_->findData(s) < 0) service_->addItem(s, s);
	service_->setCurrentIndex(qMax(0, service_->findData(p.streamService)));
	server_ = new QComboBox();
	connect(service_, &QComboBox::currentIndexChanged, this, &SettingsDialog::reloadServers);
	reloadServers();
	auto *keyBox = new QWidget();
	auto *kh = new QHBoxLayout(keyBox);
	kh->setContentsMargins(0, 0, 0, 0);
	key_ = new QLineEdit(p.streamKey);
	key_->setEchoMode(QLineEdit::Password);
	auto *show = new QPushButton("Mostrar");
	show->setCheckable(true);
	connect(show, &QPushButton::toggled, this, [this, show](bool on) { key_->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); show->setText(on ? "Ocultar" : "Mostrar"); });
	kh->addWidget(key_, 1);
	kh->addWidget(show);
	streamEncoder_ = new QComboBox();
	for (const auto &e : Engine::get().videoEncoders())
		streamEncoder_->addItem(e.name + (e.hardware ? "  — GPU" : "  — CPU"), e.id);
	streamEncoder_->setCurrentIndex(qMax(0, streamEncoder_->findData(p.streamEncoder)));
	streamBitrate_ = new QSpinBox();
	streamBitrate_->setRange(500, 50000);
	streamBitrate_->setSingleStep(500);
	streamBitrate_->setSuffix(" kbps");
	streamBitrate_->setValue(p.streamBitrate);
	audioBitrate_ = new QSpinBox();
	audioBitrate_->setRange(64, 320);
	audioBitrate_->setSingleStep(32);
	audioBitrate_->setSuffix(" kbps");
	audioBitrate_->setValue(p.streamAudioBitrate);
	f->addRow("Serviço", service_);
	f->addRow("Servidor", server_);
	f->addRow("Chave de transmissão", keyBox);
	f->addRow("Codificador", streamEncoder_);
	f->addRow("Taxa de bits de vídeo", streamBitrate_);
	f->addRow("Taxa de bits de áudio", audioBitrate_);
	f->addRow(hint("A chave fica salva só neste computador, no arquivo do perfil. Nunca compartilhe a chave: quem tiver ela pode transmitir no seu canal."));
	return w;
}

QWidget *SettingsDialog::audioTab()
{
	const auto &p = Settings::get().p();
	auto *w = new QWidget();
	auto *f = new QFormLayout(w);
	auto fill = [](QComboBox *c, const char *type, const QString &cur) {
		c->addItem("Desativado", "disabled");
		for (const auto &o : SceneManager::listOptions(type, "device_id"))
			c->addItem(o.value == "default" ? "Padrão do Windows" : o.name, o.value);
		c->setCurrentIndex(qMax(0, c->findData(cur)));
	};
	desktopDev_ = new QComboBox();
	fill(desktopDev_, "wasapi_output_capture", p.desktopDevice);
	micDev_ = new QComboBox();
	fill(micDev_, "wasapi_input_capture", p.micDevice);
	f->addRow("Áudio do PC", desktopDev_);
	f->addRow("Microfone", micDev_);
	f->addRow(hint("Para separar o som de um jogo ou do Discord, adicione a fonte “Áudio de um aplicativo” na cena e escolha o programa. Para tirar ruído do microfone: mixer → ⋯ → Filtros → Supressão de ruído."));
	return w;
}

QWidget *SettingsDialog::clipTab()
{
	const auto &p = Settings::get().p();
	auto *w = new QWidget();
	auto *f = new QFormLayout(w);
	clipSeconds_ = new QComboBox();
	for (int s : {15, 30, 60, 120})
		clipSeconds_->addItem(QString("%1 segundos").arg(s), s);
	clipSeconds_->setCurrentIndex(qMax(0, clipSeconds_->findData(p.clipSeconds)));
	clipMaxMB_ = new QSpinBox();
	clipMaxMB_->setRange(64, 8192);
	clipMaxMB_->setSingleStep(128);
	clipMaxMB_->setSuffix(" MB");
	clipMaxMB_->setValue(p.clipMaxMB);
	clipOnStart_ = new QCheckBox("Ligar o Clip It ao abrir o StreamX");
	clipOnStart_->setChecked(p.clipEnabled);
	auto *est = hint("");
	auto upd = [this, est]() {
		const int kbps = recMode_ && recMode_->currentData().toBool() ? 25000 : (recBitrate_ ? recBitrate_->value() : 20000);
		const int mb = qMin(int(qint64(kbps) * clipSeconds_->currentData().toInt() / 8 / 1024), clipMaxMB_->value());
		est->setText(QString("Memória estimada: até ~%1 MB de RAM. O limite em MB protege a memória: se for atingido, o clipe fica mais curto.").arg(mb));
	};
	connect(clipSeconds_, &QComboBox::currentIndexChanged, this, upd);
	connect(clipMaxMB_, &QSpinBox::valueChanged, this, upd);
	upd();
	f->addRow("Duração do clipe", clipSeconds_);
	f->addRow("Limite de memória", clipMaxMB_);
	f->addRow(clipOnStart_);
	f->addRow(est);
	f->addRow(hint("O Clip It guarda os últimos segundos na memória. Ao apertar o atalho (padrão Ctrl + Alt + C), salva o clipe em Clipes/AAAA-MM/, com o nome do jogo quando o Game Mode o detecta, sem interromper a gravação."));
	return w;
}

QWidget *SettingsDialog::hotkeysTab()
{
	auto *w = new QWidget();
	auto *g = new QGridLayout(w);
	int row = 0;
	g->addWidget(hint("Os atalhos funcionam mesmo com o StreamX minimizado ou durante um jogo. Clique no campo e pressione a combinação; Backspace apaga."), row++, 0, 1, 3);
	for (const auto &a : Hotkeys::get().actions()) {
		auto *edit = new HotkeyEdit();
		struct Ctx { obs_hotkey_id id; obs_key_combination_t c; bool found; } ctx{a.id, {0, OBS_KEY_NONE}, false};
		obs_enum_hotkey_bindings([](void *p, size_t, obs_hotkey_binding_t *b) {
			auto *c = static_cast<Ctx *>(p);
			if (!c->found && obs_hotkey_binding_get_hotkey_id(b) == c->id) {
				c->c = obs_hotkey_binding_get_key_combination(b);
				c->found = true;
			}
			return true;
		}, &ctx);
		edit->setCombo(ctx.c);
		auto *clear = new QPushButton("Limpar");
		connect(clear, &QPushButton::clicked, edit, [edit]() { edit->setCombo({0, OBS_KEY_NONE}); });
		g->addWidget(new QLabel(a.label), row, 0);
		g->addWidget(edit, row, 1);
		g->addWidget(clear, row, 2);
		hotkeys_.append({a.id, edit});
		row++;
	}
	g->setRowStretch(row, 1);
	g->setColumnStretch(1, 1);
	return w;
}

QWidget *SettingsDialog::controlTab()
{
	const auto &p = Settings::get().p();
	auto *w = new QWidget();
	auto *f = new QFormLayout(w);
	ctlEnabled_ = new QCheckBox("Ativar o StreamX Control");
	ctlEnabled_->setChecked(p.controlEnabled);
	ctlPort_ = new QSpinBox();
	ctlPort_->setRange(1024, 65535);
	ctlPort_->setValue(p.controlPort);
	ctlLan_ = new QCheckBox("Permitir outros aparelhos da rede local (celular, outro PC)");
	ctlLan_->setChecked(p.controlLan);
	auto *tb = new QWidget();
	auto *th = new QHBoxLayout(tb);
	th->setContentsMargins(0, 0, 0, 0);
	ctlToken_ = new QLineEdit(p.controlToken);
	ctlToken_->setReadOnly(true);
	auto *copy = new QPushButton("Copiar");
	auto *regen = new QPushButton("Gerar novo");
	connect(copy, &QPushButton::clicked, this, [this]() { QGuiApplication::clipboard()->setText(ctlToken_->text()); });
	connect(regen, &QPushButton::clicked, this, [this]() {
		static const char cs[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
		QString t;
		for (int i = 0; i < 32; i++) t += QChar(cs[QRandomGenerator::system()->bounded(int(sizeof(cs) - 1))]);
		ctlToken_->setText(t);
	});
	th->addWidget(ctlToken_, 1);
	th->addWidget(copy);
	th->addWidget(regen);
	f->addRow(ctlEnabled_);
	f->addRow("Porta", ctlPort_);
	f->addRow(ctlLan_);
	f->addRow("Token de acesso", tb);
	f->addRow(hint("Sem a opção de rede local, só este PC acessa (127.0.0.1). Com ela, só IPs privados (Wi-Fi/cabo de casa) são aceitos; a internet é sempre recusada. Toda chamada exige o token. A página de controle e os endereços aparecem na aba “Controle” da janela principal."));
	return w;
}

void SettingsDialog::accept()
{
	auto &s = Settings::get();
	auto &p = s.p();
	p.recDir = recDir_->text().trimmed();
	p.clipDir = clipDir_->text().trimmed();
	FreeCutBridge::setCustomPath(freecut_->text().trimmed());

	int w = 0, h = 0;
	if (baseRes_->currentIndex() == 0 && baseRes_->currentText() == baseRes_->itemText(0)) { p.baseW = p.baseH = 0; }
	else if (parseRes(baseRes_->currentText(), &w, &h)) { p.baseW = w; p.baseH = h; }
	if (outRes_->currentIndex() == 0 && outRes_->currentText() == outRes_->itemText(0)) { p.outW = p.outH = 0; }
	else if (parseRes(outRes_->currentText(), &w, &h)) { p.outW = w; p.outH = h; }
	p.fps = fps_->currentData().toInt();

	p.recFormat = recFormat_->currentData().toString();
	p.recEncoder = recEncoder_->currentData().toString();
	p.recConstantQuality = recMode_->currentData().toBool();
	p.recQuality = recQuality_->value();
	p.recBitrate = recBitrate_->value();
	int tracks = 0;
	for (int i = 0; i < tracks_.size(); i++)
		if (tracks_[i]->isChecked()) tracks |= 1 << i;
	p.recTracks = tracks ? tracks : 1;

	p.streamService = service_->currentData().toString();
	p.streamServer = p.streamService == "custom" ? server_->currentText().trimmed() : server_->currentData().toString();
	p.streamKey = key_->text().trimmed();
	p.streamEncoder = streamEncoder_->currentData().toString();
	p.streamBitrate = streamBitrate_->value();
	p.streamAudioBitrate = audioBitrate_->value();

	p.desktopDevice = desktopDev_->currentData().toString();
	p.micDevice = micDev_->currentData().toString();

	p.clipSeconds = clipSeconds_->currentData().toInt();
	p.clipMaxMB = clipMaxMB_->value();
	p.clipEnabled = clipOnStart_->isChecked();

	for (const auto &[id, edit] : hotkeys_) {
		QList<obs_key_combination_t> combos;
		if (edit->combo().key != OBS_KEY_NONE)
			combos << edit->combo();
		Hotkeys::get().setBinding(id, combos);
	}

	p.controlEnabled = ctlEnabled_->isChecked();
	p.controlPort = ctlPort_->value();
	p.controlLan = ctlLan_->isChecked();
	p.controlToken = ctlToken_->text();

	s.save();
	const QString warn = applyProfile();
	if (!warn.isEmpty())
		QMessageBox::information(this, "Configurações", warn);
	QDialog::accept();
}

} // namespace sx
