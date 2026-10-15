#include "Settings.h"
#include "Engine.h"
#include "Log.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>

namespace sx {

QString sanitizeFileName(QString s)
{
	s.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
	s = s.trimmed();
	return s.isEmpty() ? QStringLiteral("Sem nome") : s;
}

static QString randomToken()
{
	static const char cs[] = "abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	QString t;
	for (int i = 0; i < 32; i++)
		t += QChar(cs[QRandomGenerator::system()->bounded(int(sizeof(cs) - 1))]);
	return t;
}

QJsonObject ProfileSettings::toJson() const
{
	return QJsonObject{
		{"video", QJsonObject{{"baseW", baseW}, {"baseH", baseH}, {"outW", outW}, {"outH", outH}, {"fps", fps}}},
		{"record", QJsonObject{{"dir", recDir}, {"format", recFormat}, {"encoder", recEncoder},
				       {"constantQuality", recConstantQuality}, {"quality", recQuality},
				       {"bitrate", recBitrate}, {"tracks", recTracks}}},
		{"stream", QJsonObject{{"service", streamService}, {"server", streamServer}, {"key", streamKey},
				       {"encoder", streamEncoder}, {"bitrate", streamBitrate}, {"audioBitrate", streamAudioBitrate}}},
		{"clip", QJsonObject{{"enabled", clipEnabled}, {"seconds", clipSeconds}, {"maxMB", clipMaxMB}, {"dir", clipDir}}},
		{"audio", QJsonObject{{"desktop", desktopDevice}, {"mic", micDevice}}},
		{"control", QJsonObject{{"enabled", controlEnabled}, {"port", controlPort}, {"lan", controlLan}, {"token", controlToken}}},
		{"gameMode", QJsonObject{{"auto", gameModeAuto}}},
		{"hotkeys", hotkeys},
	};
}

ProfileSettings ProfileSettings::fromJson(const QJsonObject &o)
{
	ProfileSettings p;
	const auto v = o["video"].toObject(), r = o["record"].toObject(), s = o["stream"].toObject(),
		   c = o["clip"].toObject(), a = o["audio"].toObject(), ct = o["control"].toObject(), g = o["gameMode"].toObject();
	p.baseW = v["baseW"].toInt(p.baseW); p.baseH = v["baseH"].toInt(p.baseH);
	p.outW = v["outW"].toInt(p.outW); p.outH = v["outH"].toInt(p.outH); p.fps = v["fps"].toInt(p.fps);
	p.recDir = r["dir"].toString(); p.recFormat = r["format"].toString(p.recFormat); p.recEncoder = r["encoder"].toString();
	p.recConstantQuality = r["constantQuality"].toBool(p.recConstantQuality); p.recQuality = r["quality"].toInt(p.recQuality);
	p.recBitrate = r["bitrate"].toInt(p.recBitrate); p.recTracks = r["tracks"].toInt(p.recTracks);
	p.streamService = s["service"].toString(p.streamService); p.streamServer = s["server"].toString();
	p.streamKey = s["key"].toString(); p.streamEncoder = s["encoder"].toString();
	p.streamBitrate = s["bitrate"].toInt(p.streamBitrate); p.streamAudioBitrate = s["audioBitrate"].toInt(p.streamAudioBitrate);
	p.clipEnabled = c["enabled"].toBool(p.clipEnabled); p.clipSeconds = c["seconds"].toInt(p.clipSeconds);
	p.clipMaxMB = c["maxMB"].toInt(p.clipMaxMB); p.clipDir = c["dir"].toString();
	p.desktopDevice = a["desktop"].toString(p.desktopDevice); p.micDevice = a["mic"].toString(p.micDevice);
	p.controlEnabled = ct["enabled"].toBool(p.controlEnabled); p.controlPort = ct["port"].toInt(p.controlPort);
	p.controlLan = ct["lan"].toBool(p.controlLan); p.controlToken = ct["token"].toString();
	p.gameModeAuto = g["auto"].toBool(p.gameModeAuto);
	p.hotkeys = o["hotkeys"].toObject();
	if (p.recTracks <= 0 || p.recTracks > 63)
		p.recTracks = 1;
	return p;
}

Settings &Settings::get()
{
	static Settings s;
	return s;
}

QString Settings::profilesDir()
{
	const QString d = Engine::configDir() + "/profiles";
	QDir().mkpath(d);
	return d;
}

QString Settings::scenesFile() { return Engine::configDir() + "/scenes.json"; }

QString Settings::profileFile(const QString &name) const { return profilesDir() + "/" + sanitizeFileName(name) + ".json"; }

QStringList Settings::profiles() const
{
	QStringList out;
	for (const auto &f : QDir(profilesDir()).entryInfoList({"*.json"}, QDir::Files, QDir::Name))
		out << f.completeBaseName();
	if (out.isEmpty())
		out << current_;
	return out;
}

void Settings::load()
{
	QFile g(Engine::configDir() + "/global.json");
	if (g.open(QIODevice::ReadOnly))
		current_ = QJsonDocument::fromJson(g.readAll()).object()["profile"].toString(current_);
	QFile f(profileFile(current_));
	if (f.open(QIODevice::ReadOnly))
		profile_ = ProfileSettings::fromJson(QJsonDocument::fromJson(f.readAll()).object());
	else
		profile_ = ProfileSettings();
	if (profile_.controlToken.isEmpty())
		profile_.controlToken = randomToken();
	save();
}

void Settings::save()
{
	QFile f(profileFile(current_));
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(profile_.toJson()).toJson());
	else
		logError("Não foi possível salvar o perfil " + current_);
	QFile g(Engine::configDir() + "/global.json");
	if (g.open(QIODevice::WriteOnly | QIODevice::Truncate))
		g.write(QJsonDocument(QJsonObject{{"profile", current_}}).toJson());
}

bool Settings::createProfile(const QString &name, bool copyCurrent)
{
	const QString n = sanitizeFileName(name);
	if (QFile::exists(profileFile(n)))
		return false;
	save();
	current_ = n;
	if (!copyCurrent) {
		const QString token = profile_.controlToken;
		profile_ = ProfileSettings();
		profile_.controlToken = token;
	}
	save();
	emit profileChanged();
	return true;
}

bool Settings::switchProfile(const QString &name)
{
	if (name == current_ || !QFile::exists(profileFile(name)))
		return false;
	save();
	current_ = name;
	load();
	emit profileChanged();
	return true;
}

bool Settings::renameProfile(const QString &oldName, const QString &newName)
{
	const QString n = sanitizeFileName(newName);
	if (QFile::exists(profileFile(n)) || !QFile::rename(profileFile(oldName), profileFile(n)))
		return false;
	if (current_ == oldName) {
		current_ = n;
		save();
	}
	emit profileChanged();
	return true;
}

bool Settings::deleteProfile(const QString &name)
{
	if (name == current_ || profiles().size() <= 1)
		return false;
	return QFile::remove(profileFile(name));
}

QString Settings::recordingDir() const
{
	const QString d = profile_.recDir.isEmpty() ? Engine::defaultOutputDir() : profile_.recDir;
	QDir().mkpath(d);
	return d;
}

QString Settings::clipsDir() const
{
	const QString d = profile_.clipDir.isEmpty() ? Engine::defaultOutputDir() + "/Clipes" : profile_.clipDir;
	QDir().mkpath(d);
	return d;
}

} // namespace sx
