// StreamX — janela principal
#pragma once
#include <QMainWindow>

class QPushButton;
class QLabel;
class QTimer;

namespace sx {

class PreviewWidget;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	void toggleRecording();
	void updateStatus();

	PreviewWidget *preview_ = nullptr;
	QPushButton *recBtn_ = nullptr;
	QLabel *recTime_ = nullptr;
	QLabel *perf_ = nullptr;
	QLabel *encoder_ = nullptr;
	QTimer *statusTimer_ = nullptr;
};

} // namespace sx
