// StreamX — janela principal
#pragma once
#include <QMainWindow>
#include "../core/PerfMonitor.h"

class QPushButton;
class QLabel;
class QListWidget;
class QStackedWidget;
class QComboBox;
class QSpinBox;
class QButtonGroup;

namespace sx {

class PreviewWidget;
class MixerWidget;
class ClipsPage;
class ControlPage;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	QWidget *buildTopBar();
	QWidget *buildNav();
	QWidget *buildStudio();
	QWidget *panel(const QString &title, QWidget *content, QWidget *toolbar = nullptr);

	void refreshScenes();
	void refreshSources();
	void refreshProfiles();
	void updateControls();
	void onPerf(const sx::PerfSample &s);
	void profileMenu();
	void toggleRecording();
	void toggleStreaming();
	void toggleClipIt();
	void saveClip();
	int64_t currentItemId() const;
	void sourceMenu(const QPoint &pos);
	void notify(const QString &msg, bool error = false);

	QStackedWidget *pages_ = nullptr;
	QButtonGroup *navGroup_ = nullptr;
	PreviewWidget *preview_ = nullptr;
	QListWidget *scenes_ = nullptr;
	QListWidget *sources_ = nullptr;
	MixerWidget *mixer_ = nullptr;
	QComboBox *transition_ = nullptr;
	QSpinBox *transitionMs_ = nullptr;
	QComboBox *profile_ = nullptr;
	QPushButton *recBtn_ = nullptr, *streamBtn_ = nullptr, *clipBtn_ = nullptr, *clipSave_ = nullptr, *editBtn_ = nullptr;
	QPushButton *gameChip_ = nullptr;
	QLabel *cpu_ = nullptr, *gpu_ = nullptr, *ram_ = nullptr, *fps_ = nullptr;
	QLabel *recInfo_ = nullptr, *streamInfo_ = nullptr, *clipInfo_ = nullptr, *toast_ = nullptr;
	ClipsPage *clipsPage_ = nullptr;
	ControlPage *controlPage_ = nullptr;
	bool updatingLists_ = false;
};

} // namespace sx
