// StreamX — cenas, fontes, transições e canais de áudio globais
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <obs.hpp>

namespace sx {

struct SourceType {
	QString id;      // ex.: monitor_capture
	QString name;    // nome traduzido do plugin
	bool video = false, audio = false;
};

struct ItemInfo {
	int64_t id = 0;
	QString name;
	QString typeId;
	bool visible = true;
	bool locked = false;
};

class SceneManager : public QObject {
	Q_OBJECT
public:
	static SceneManager &get();

	// Coleção de cenas
	bool load(const QString &path);   // carrega; se não existir, cria a coleção padrão
	bool save(const QString &path) const;
	void createDefault();
	void clear();

	// Cenas
	QStringList sceneNames() const;
	QString currentSceneName() const;
	obs_scene_t *currentScene() const;
	obs_source_t *sceneSource(const QString &name) const;
	bool addScene(const QString &name);
	bool removeScene(const QString &name);
	bool renameScene(const QString &oldName, const QString &newName);
	bool setCurrentScene(const QString &name, bool useTransition = true);

	// Fontes da cena atual
	QList<ItemInfo> items() const;              // do topo para o fundo
	obs_sceneitem_t *item(int64_t id) const;
	obs_source_t *addSource(const char *typeId, const QString &name, obs_data_t *settings = nullptr);
	bool addExistingSource(const QString &sourceName);
	void removeItem(int64_t id);
	void setItemVisible(int64_t id, bool visible);
	void setItemLocked(int64_t id, bool locked);
	void moveItem(int64_t id, bool up);
	void fitItem(int64_t id);
	QString uniqueSourceName(const QString &base) const;

	// Tipos de fonte disponíveis (plugins carregados)
	static QList<SourceType> inputTypes();
	static QList<SourceType> filterTypes(bool video, bool audio);
	static QList<SourceType> transitionTypes();

	// Transição
	bool setTransition(const QString &typeId, int durationMs);
	QString transitionType() const;
	int transitionDuration() const { return transitionMs_; }

	// Áudio global (canais 1 = PC, 3 = microfone) e lista de fontes com áudio
	obs_source_t *desktopAudio() const { return desktopAudio_; }
	obs_source_t *micAudio() const { return micAudio_; }
	QList<obs_source_t *> audioSources() const;
	void setAudioDevices(const QString &desktopId, const QString &micId);

	struct Option { QString name; QString value; };
	// Opções reais de uma propriedade de lista (ex.: monitores de "monitor_capture")
	static QList<Option> listOptions(const char *typeId, const char *property);

signals:
	void scenesChanged();
	void currentSceneChanged();
	void itemsChanged();
	void audioSourcesChanged();

private:
	SceneManager() = default;
	void connectScene(obs_source_t *scene);
	static void onSceneSignal(void *data, calldata_t *);
	QList<OBSSource> scenes_;      // ordem da lista de cenas
	OBSSource current_;
	OBSSourceAutoRelease transition_;
	int transitionMs_ = 300;
	OBSSourceAutoRelease desktopAudio_;
	OBSSourceAutoRelease micAudio_;
};

} // namespace sx
