// StreamX — cenas, fontes e canais de áudio globais
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <obs.hpp>

namespace sx {

class SceneManager : public QObject {
	Q_OBJECT
public:
	static SceneManager &get();

	// Cria a configuração inicial: uma cena com a tela inteira + áudio do PC + microfone
	void createDefault();
	void clear();

	obs_scene_t *currentScene() const { return current_; }
	obs_source_t *desktopAudio() const { return desktopAudio_; }
	obs_source_t *micAudio() const { return micAudio_; }

	obs_source_t *addSource(const char *typeId, const QString &name, obs_data_t *settings = nullptr);

	struct Option { QString name; QString value; };
	// Opções reais de uma propriedade de lista de um tipo de fonte (ex.: monitores de "monitor_capture")
	static QList<Option> listOptions(const char *typeId, const char *property);

signals:
	void changed();

private:
	SceneManager() = default;
	OBSSceneAutoRelease scene_;
	obs_scene_t *current_ = nullptr;
	OBSSourceAutoRelease desktopAudio_;
	OBSSourceAutoRelease micAudio_;
};

} // namespace sx
