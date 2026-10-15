// StreamX — configurações e perfis
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>

namespace sx {

struct ProfileSettings {
	// Vídeo
	int baseW = 0, baseH = 0;   // 0 = resolução do monitor principal
	int outW = 0, outH = 0;     // 0 = automática (até 1080p)
	int fps = 60;
	// Gravação
	QString recDir;             // vazio = Vídeos/StreamX
	QString recFormat = "mp4";  // mp4 | mkv | mov | hybrid_mp4
	QString recEncoder;         // vazio = melhor disponível
	bool recConstantQuality = true;
	int recQuality = 20;
	int recBitrate = 20000;
	int recTracks = 1;          // bitmask das faixas de áudio gravadas (1..6)
	// Transmissão
	QString streamService = "custom"; // custom ou nome do serviço (rtmp_common)
	QString streamServer;
	QString streamKey;
	QString streamEncoder;
	int streamBitrate = 6000;
	int streamAudioBitrate = 160;
	// Clip It (replay buffer)
	bool clipEnabled = false;
	int clipSeconds = 30;
	int clipMaxMB = 1024;
	QString clipDir;            // vazio = Vídeos/StreamX/Clipes
	// Áudio
	QString desktopDevice = "default";
	QString micDevice = "default";
	// Controle remoto
	bool controlEnabled = false;
	int controlPort = 4466;
	bool controlLan = false;
	QString controlToken;
	// Game Mode
	bool gameModeAuto = true;
	// Atalhos (JSON do libobs: nome -> combinações)
	QJsonObject hotkeys;

	QJsonObject toJson() const;
	static ProfileSettings fromJson(const QJsonObject &o);
};

class Settings : public QObject {
	Q_OBJECT
public:
	static Settings &get();

	void load();
	void save();

	ProfileSettings &p() { return profile_; }
	const ProfileSettings &p() const { return profile_; }

	QString currentProfile() const { return current_; }
	QStringList profiles() const;
	bool createProfile(const QString &name, bool copyCurrent);
	bool switchProfile(const QString &name);
	bool renameProfile(const QString &oldName, const QString &newName);
	bool deleteProfile(const QString &name);

	QString recordingDir() const;
	QString clipsDir() const;
	static QString profilesDir();
	static QString scenesFile();

signals:
	void profileChanged();

private:
	Settings() = default;
	QString profileFile(const QString &name) const;
	QString current_ = "Padrão";
	ProfileSettings profile_;
};

QString sanitizeFileName(QString s);

} // namespace sx
