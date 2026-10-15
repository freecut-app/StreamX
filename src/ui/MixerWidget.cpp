#include "MixerWidget.h"
#include "SourceDialogs.h"
#include "../core/SceneManager.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMetaObject>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace sx {

/* ---------------- medidor ---------------- */

LevelMeter::LevelMeter(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(10);
	setMaximumHeight(10);
}

void LevelMeter::setLevels(float peakDb, float magDb)
{
	peak_ = std::isfinite(peakDb) ? peakDb : -60.0f;
	mag_ = std::isfinite(magDb) ? magDb : -60.0f;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (peak_ >= hold_ || now - holdAt_ > 1500) {
		hold_ = peak_;
		holdAt_ = now;
	}
	update();
}

void LevelMeter::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	const QRectF r = rect();
	p.fillRect(r, QColor("#161a22"));
	auto x = [&](float db) { return float(r.width()) * std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); };
	QLinearGradient g(r.topLeft(), r.topRight());
	g.setColorAt(0.0, QColor("#1f9cff"));   // azul elétrico
	g.setColorAt(0.75, QColor("#28d7a0"));  // -15 dB
	g.setColorAt(0.9, QColor("#ffcc33"));   // -6 dB
	g.setColorAt(1.0, QColor("#ff3b4f"));   // 0 dB
	p.fillRect(QRectF(0, 0, x(peak_), r.height()), g);
	p.fillRect(QRectF(0, 0, x(mag_), r.height()), QColor(255, 255, 255, 40));
	p.fillRect(QRectF(x(hold_) - 1, 0, 2, r.height()), QColor("#e6e9f0"));
	p.setPen(QColor(0, 0, 0, 120));
	for (int db : {-50, -40, -30, -20, -10, -6, -3})
		p.drawLine(QPointF(x(float(db)), 0), QPointF(x(float(db)), r.height()));
}

/* ---------------- faixa do mixer ---------------- */

AudioStrip::AudioStrip(obs_source_t *source, QWidget *parent) : QWidget(parent), source_(source)
{
	setObjectName("audioStrip");
	auto *v = new QVBoxLayout(this);
	v->setContentsMargins(10, 8, 10, 8);
	v->setSpacing(4);
	auto *top = new QHBoxLayout();
	auto *name = new QLabel(QString::fromUtf8(obs_source_get_name(source)));
	name->setObjectName("stripName");
	db_ = new QLabel("0,0 dB");
	db_->setObjectName("stripDb");
	mute_ = new QPushButton();
	mute_->setObjectName("muteButton");
	mute_->setCheckable(true);
	mute_->setFixedSize(30, 26);
	auto *more = new QPushButton("⋯");
	more->setObjectName("tool");
	more->setFixedSize(30, 26);
	more->setToolTip("Faixas, monitoramento e filtros");
	top->addWidget(name, 1);
	top->addWidget(db_);
	top->addWidget(mute_);
	top->addWidget(more);
	v->addLayout(top);
	meter_ = new LevelMeter(this);
	v->addWidget(meter_);
	slider_ = new QSlider(Qt::Horizontal);
	slider_->setRange(0, 1000);
	v->addWidget(slider_);

	fader_ = obs_fader_create(OBS_FADER_LOG);
	obs_fader_attach_source(fader_, source);
	obs_fader_add_callback(fader_, onFader, this);
	slider_->setValue(int(obs_fader_get_deflection(fader_) * 1000));
	onFader(this, obs_fader_get_db(fader_));
	connect(slider_, &QSlider::valueChanged, this, [this](int v) { obs_fader_set_deflection(fader_, float(v) / 1000.0f); });

	volmeter_ = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_attach_source(volmeter_, source);
	obs_volmeter_add_callback(volmeter_, onLevels, this);

	signal_handler_connect(obs_source_get_signal_handler(source), "mute", onMute, this);
	connect(mute_, &QPushButton::toggled, this, [this](bool m) { obs_source_set_muted(source_, m); syncMute(); });
	syncMute();
	connect(more, &QPushButton::clicked, this, &AudioStrip::showMenu);
}

AudioStrip::~AudioStrip()
{
	signal_handler_disconnect(obs_source_get_signal_handler(source_), "mute", onMute, this);
	obs_volmeter_remove_callback(volmeter_, onLevels, this);
	obs_volmeter_destroy(volmeter_);
	obs_fader_remove_callback(fader_, onFader, this);
	obs_fader_destroy(fader_);
}

void AudioStrip::onLevels(void *data, const float mag[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS], const float *)
{
	// chamado na thread de áudio: só guarda o maior valor entre os canais
	auto *self = static_cast<AudioStrip *>(data);
	float p = -60.0f, m = -60.0f;
	for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		if (std::isfinite(peak[i])) p = std::max(p, peak[i]);
		if (std::isfinite(mag[i])) m = std::max(m, mag[i]);
	}
	self->peak_ = p;
	self->mag_ = m;
}

void AudioStrip::updateMeter() { meter_->setLevels(peak_.load(), mag_.load()); }

void AudioStrip::onFader(void *data, float db)
{
	auto *self = static_cast<AudioStrip *>(data);
	QMetaObject::invokeMethod(self, [self, db]() {
		self->db_->setText(std::isfinite(db) && db > -96 ? QString("%1 dB").arg(double(db), 0, 'f', 1).replace('.', ',') : "-∞ dB");
		const QSignalBlocker b(self->slider_);
		self->slider_->setValue(int(obs_fader_get_deflection(self->fader_) * 1000));
	}, Qt::QueuedConnection);
}

void AudioStrip::onMute(void *data, calldata_t *)
{
	auto *self = static_cast<AudioStrip *>(data);
	QMetaObject::invokeMethod(self, [self]() { self->syncMute(); }, Qt::QueuedConnection);
}

void AudioStrip::syncMute()
{
	const bool m = obs_source_muted(source_);
	const QSignalBlocker b(mute_);
	mute_->setChecked(m);
	mute_->setText(m ? "🔇" : "🔊");
	mute_->setToolTip(m ? "Ativar som" : "Silenciar");
	meter_->setEnabled(!m);
}

void AudioStrip::showMenu()
{
	QMenu menu(this);
	auto *tracks = menu.addMenu("Faixas de gravação");
	const uint32_t mixers = obs_source_get_audio_mixers(source_);
	for (int i = 0; i < 6; i++) {
		QAction *a = tracks->addAction(QString("Faixa %1").arg(i + 1));
		a->setCheckable(true);
		a->setChecked(mixers & (1u << i));
		connect(a, &QAction::toggled, this, [this, i](bool on) {
			uint32_t m = obs_source_get_audio_mixers(source_);
			m = on ? (m | (1u << i)) : (m & ~(1u << i));
			obs_source_set_audio_mixers(source_, m);
		});
	}
	auto *mon = menu.addMenu("Monitoramento (ouvir no fone)");
	const auto cur = obs_source_get_monitoring_type(source_);
	const struct { obs_monitoring_type t; const char *n; } opts[] = {
		{OBS_MONITORING_TYPE_NONE, "Desligado"},
		{OBS_MONITORING_TYPE_MONITOR_ONLY, "Só monitorar (não grava)"},
		{OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT, "Monitorar e gravar"},
	};
	for (const auto &o : opts) {
		QAction *a = mon->addAction(o.n);
		a->setCheckable(true);
		a->setChecked(cur == o.t);
		const auto t = o.t;
		connect(a, &QAction::triggered, this, [this, t]() { obs_source_set_monitoring_type(source_, t); });
	}
	menu.addSeparator();
	menu.addAction("Filtros (redução de ruído, ganho...)", this, [this]() { FiltersDialog(source_, this).exec(); });
	menu.addAction("Propriedades (dispositivo)", this, [this]() { PropertiesDialog(source_, this).exec(); });
	menu.exec(QCursor::pos());
}

/* ---------------- mixer ---------------- */

MixerWidget::MixerWidget(QWidget *parent) : QScrollArea(parent)
{
	setWidgetResizable(true);
	setFrameShape(QFrame::NoFrame);
	inner_ = new QWidget();
	lay_ = new QVBoxLayout(inner_);
	lay_->setContentsMargins(0, 0, 0, 0);
	lay_->setSpacing(6);
	setWidget(inner_);
	connect(&SceneManager::get(), &SceneManager::audioSourcesChanged, this, &MixerWidget::rebuild);
	auto *t = new QTimer(this);
	connect(t, &QTimer::timeout, this, [this]() { for (auto *s : strips_) s->updateMeter(); });
	t->start(33);
	rebuild();
}

void MixerWidget::rebuild()
{
	const auto sources = SceneManager::get().audioSources();
	QList<obs_source_t *> current;
	for (auto *s : strips_)
		current << s->source();
	if (current == sources)
		return;
	strips_.clear();
	while (QLayoutItem *it = lay_->takeAt(0)) {
		delete it->widget();
		delete it;
	}
	for (obs_source_t *s : sources) {
		auto *strip = new AudioStrip(s, inner_);
		strips_ << strip;
		lay_->addWidget(strip);
	}
	if (sources.isEmpty())
		lay_->addWidget(new QLabel("Nenhuma fonte de áudio. Adicione um microfone ou áudio de aplicativo em Fontes."));
	lay_->addStretch(1);
}

} // namespace sx
