#include "MainWindow.h"
#include "PreviewWidget.h"
#include "../core/Engine.h"
#include "../core/Recorder.h"
#include "../core/Log.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace sx {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	setWindowTitle("StreamX");
	resize(1280, 800);

	auto *central = new QWidget(this);
	auto *lay = new QVBoxLayout(central);
	lay->setContentsMargins(12, 12, 12, 12);
	lay->setSpacing(10);

	preview_ = new PreviewWidget(central);
	lay->addWidget(preview_, 1);

	auto *bar = new QHBoxLayout();
	recBtn_ = new QPushButton("● Gravar", central);
	recBtn_->setObjectName("recordButton");
	recBtn_->setMinimumHeight(40);
	recTime_ = new QLabel("00:00:00", central);
	recTime_->setObjectName("recTime");
	bar->addWidget(recBtn_);
	bar->addWidget(recTime_);
	bar->addStretch(1);
	lay->addLayout(bar);
	setCentralWidget(central);

	encoder_ = new QLabel(this);
	perf_ = new QLabel(this);
	statusBar()->addWidget(encoder_);
	statusBar()->addPermanentWidget(perf_);

	const auto encs = Engine::get().videoEncoders();
	encoder_->setText(encs.isEmpty() ? "Sem codificador" : "Codificador: " + encs.first().name);

	connect(recBtn_, &QPushButton::clicked, this, &MainWindow::toggleRecording);
	connect(&Recorder::get(), &Recorder::stopped, this, [this](const QString &path, int code, const QString &err) {
		updateStatus();
		if (code != 0)
			QMessageBox::warning(this, "Gravação", "A gravação parou com erro: " + err);
		else
			statusBar()->showMessage("Gravação salva: " + path, 8000);
	});

	statusTimer_ = new QTimer(this);
	connect(statusTimer_, &QTimer::timeout, this, &MainWindow::updateStatus);
	statusTimer_->start(500);
}

void MainWindow::toggleRecording()
{
	auto &rec = Recorder::get();
	if (rec.isActive()) {
		rec.stop();
		return;
	}
	QString err;
	if (!rec.start(&err))
		QMessageBox::critical(this, "Gravação", err);
	updateStatus();
}

void MainWindow::updateStatus()
{
	const bool on = Recorder::get().isActive();
	recBtn_->setText(on ? "■ Parar" : "● Gravar");
	recBtn_->setProperty("active", on);
	recBtn_->style()->unpolish(recBtn_);
	recBtn_->style()->polish(recBtn_);
	const qint64 s = Recorder::get().elapsedMs() / 1000;
	recTime_->setText(QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0')));
	perf_->setText(QString("%1 fps · renderização %2 ms")
			       .arg(obs_get_active_fps(), 0, 'f', 1)
			       .arg(double(obs_get_average_frame_time_ns()) / 1e6, 0, 'f', 2));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	if (Recorder::get().isActive()) {
		if (QMessageBox::question(this, "StreamX", "Há uma gravação em andamento. Parar e sair?") != QMessageBox::Yes) {
			e->ignore();
			return;
		}
		Recorder::get().stop();
	}
	e->accept();
}

} // namespace sx
