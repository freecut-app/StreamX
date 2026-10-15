// StreamX — Game Mode: detecta o jogo em primeiro plano e aplica um perfil otimizado
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QJsonObject>
#include "PerfMonitor.h"

namespace sx {

struct GamePreset {
	QString id;            // chave do preset
	QString name;          // nome exibido
	QStringList exes;      // executáveis do jogo (minúsculas)
	int fps = 60;
	int outH = 1080;       // altura de saída sugerida
	QString captureMode = "window"; // game_capture: capturar a janela do jogo
	QString note;          // dica específica do jogo
};

struct DetectedGame {
	QString exe;           // nome do executável
	QString path;          // caminho completo
	QString title;         // título da janela
	QString windowSpec;    // "título:classe:exe" no formato do game_capture
	bool fullscreen = false;
	const GamePreset *preset = nullptr;
};

class GameMode : public QObject {
	Q_OBJECT
public:
	static GameMode &get();

	static const QList<GamePreset> &presets();
	const GamePreset *presetFor(const QString &exe) const;

	void setEnabled(bool on);
	bool isEnabled() const { return enabled_; }
	DetectedGame current() const { return game_; }

	// Cria/atualiza a cena "Game Mode" com captura de jogo apontada para o jogo detectado
	bool applyToScene(const DetectedGame &g, QString *error);
	// Ajusta prioridade do processo (estabilidade sem roubar FPS do jogo)
	void applyProcessPriority(bool gaming);
	// Sugestões a partir das métricas atuais
	QStringList suggestions(const PerfSample &s) const;

	// Ajustes personalizados por jogo (fps/altura), salvos em games.json
	QJsonObject userOverrides(const QString &exe) const;
	void setUserOverrides(const QString &exe, const QJsonObject &o);

signals:
	void gameChanged(const sx::DetectedGame &g);
	void enabledChanged(bool on);

private:
	GameMode() = default;
	void poll();
	static DetectedGame foreground();
	bool enabled_ = false;
	DetectedGame game_;
	QJsonObject overrides_;
	bool loaded_ = false;
	void loadOverrides();
};

} // namespace sx
