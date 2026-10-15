// StreamX — páginas: Clipes, Game Mode, Assistente, Controle remoto
#pragma once
#include <QWidget>
#include "../core/PerfMonitor.h"
#include "../core/GameMode.h"

class QTreeWidget;
class QLabel;
class QPushButton;
class QListWidget;
class QProgressBar;
class QComboBox;
class QSpinBox;
class QTableWidget;
class QPlainTextEdit;

namespace sx {

class ClipsPage : public QWidget {
	Q_OBJECT
public:
	explicit ClipsPage(QWidget *parent = nullptr);
	void refresh();

private:
	QStringList selectedFiles() const;
	void updateStatus();
	QTreeWidget *list_ = nullptr;
	QLabel *status_ = nullptr;
	QPushButton *toggle_ = nullptr, *save_ = nullptr;
};

class GameModePage : public QWidget {
	Q_OBJECT
public:
	explicit GameModePage(QWidget *parent = nullptr);

private:
	void onGame(const sx::DetectedGame &g);
	void onPerf(const sx::PerfSample &s);
	QPushButton *toggle_ = nullptr, *apply_ = nullptr, *saveOverride_ = nullptr;
	QLabel *game_ = nullptr, *note_ = nullptr;
	QProgressBar *cpu_ = nullptr, *gpu_ = nullptr, *ram_ = nullptr;
	QLabel *stats_ = nullptr;
	QListWidget *tips_ = nullptr;
	QTableWidget *presets_ = nullptr;
	QSpinBox *ovFps_ = nullptr, *ovHeight_ = nullptr;
	DetectedGame current_;
};

class AssistantPage : public QWidget {
	Q_OBJECT
public:
	explicit AssistantPage(QWidget *parent = nullptr);

private:
	void analyze();
	QComboBox *goal_ = nullptr;
	QLabel *hw_ = nullptr, *result_ = nullptr;
	QListWidget *reasons_ = nullptr;
	QPushButton *apply_ = nullptr;
};

class ControlPage : public QWidget {
	Q_OBJECT
public:
	explicit ControlPage(QWidget *parent = nullptr);
	void refresh();

private:
	QLabel *state_ = nullptr, *urls_ = nullptr;
	QPlainTextEdit *log_ = nullptr;
};

} // namespace sx
