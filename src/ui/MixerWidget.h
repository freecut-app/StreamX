// StreamX — mixer de áudio (medidor, fader em dB, mudo, faixas, monitoramento, filtros)
#pragma once
#include <QWidget>
#include <QScrollArea>
#include <atomic>
#include <obs.hpp>

class QSlider;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace sx {

class LevelMeter : public QWidget {
	Q_OBJECT
public:
	explicit LevelMeter(QWidget *parent = nullptr);
	void setLevels(float peakDb, float magDb);

protected:
	void paintEvent(QPaintEvent *) override;

private:
	float peak_ = -60, mag_ = -60, hold_ = -60;
	qint64 holdAt_ = 0;
};

class AudioStrip : public QWidget {
	Q_OBJECT
public:
	explicit AudioStrip(obs_source_t *source, QWidget *parent = nullptr);
	~AudioStrip() override;
	void updateMeter();
	obs_source_t *source() const { return source_; }

private:
	static void onLevels(void *data, const float mag[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS], const float input[MAX_AUDIO_CHANNELS]);
	static void onFader(void *data, float db);
	static void onMute(void *data, calldata_t *cd);
	void syncMute();
	void showMenu();
	OBSSource source_;
	obs_volmeter_t *volmeter_ = nullptr;
	obs_fader_t *fader_ = nullptr;
	LevelMeter *meter_ = nullptr;
	QSlider *slider_ = nullptr;
	QLabel *db_ = nullptr;
	QPushButton *mute_ = nullptr;
	std::atomic<float> peak_{-60.0f}, mag_{-60.0f};
};

class MixerWidget : public QScrollArea {
	Q_OBJECT
public:
	explicit MixerWidget(QWidget *parent = nullptr);

private:
	void rebuild();
	QWidget *inner_ = nullptr;
	QVBoxLayout *lay_ = nullptr;
	QList<AudioStrip *> strips_;
};

} // namespace sx
