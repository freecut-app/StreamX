#include "FreeCutBridge.h"
#include "../core/Engine.h"
#include "../core/Log.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>

namespace sx {

static QSettings globalIni() { return QSettings(Engine::configDir() + "/integration.ini", QSettings::IniFormat); }

void FreeCutBridge::setCustomPath(const QString &exe) { globalIni().setValue("freecut/path", exe); }
QString FreeCutBridge::customPath() { return globalIni().value("freecut/path").toString(); }
QString FreeCutBridge::handoffFile() { return Engine::configDir() + "/handoff/freecut-import.json"; }

QString FreeCutBridge::findExecutable()
{
	QStringList candidates;
	if (!customPath().isEmpty())
		candidates << customPath();
	// instalador NSIS por usuário do FreeCut (electron-builder)
	candidates << QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + "/Programs/FreeCut/FreeCut.exe";
	// registro de desinstalação (instalação em pasta escolhida pelo usuário)
	for (const char *root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
		QSettings reg(QString("%1\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall").arg(root), QSettings::NativeFormat);
		for (const QString &key : reg.childGroups()) {
			reg.beginGroup(key);
			if (reg.value("DisplayName").toString().startsWith("FreeCut")) {
				const QString loc = reg.value("InstallLocation").toString();
				if (!loc.isEmpty())
					candidates << QDir::fromNativeSeparators(loc) + "/FreeCut.exe";
			}
			reg.endGroup();
		}
	}
	for (const auto &c : candidates)
		if (QFileInfo(c).isFile())
			return QDir::toNativeSeparators(c);
	return QString();
}

FreeCutLaunch FreeCutBridge::open(const QStringList &files)
{
	FreeCutLaunch r;
	QStringList existing;
	for (const auto &f : files)
		if (QFileInfo::exists(f))
			existing << QDir::toNativeSeparators(QFileInfo(f).absoluteFilePath());
	if (existing.isEmpty()) {
		r.message = "O arquivo não existe mais.";
		return r;
	}
	// arquivo de entrega: lido pelo FreeCut quando ele implementar o contrato
	QDir().mkpath(QFileInfo(handoffFile()).absolutePath());
	QFile h(handoffFile());
	if (h.open(QIODevice::WriteOnly | QIODevice::Truncate))
		h.write(QJsonDocument(QJsonObject{{"version", 1}, {"source", "StreamX"}, {"createdAt", QDateTime::currentDateTime().toString(Qt::ISODate)},
						  {"files", QJsonArray::fromStringList(existing)}}).toJson());

	// facilita importar manualmente: caminho na área de transferência + arquivo selecionado no Explorer
	QGuiApplication::clipboard()->setText(existing.first());
	QProcess::startDetached("explorer.exe", {"/select," + existing.first()});

	r.exe = findExecutable();
	if (r.exe.isEmpty()) {
		r.message = "FreeCut não encontrado. Instale o FreeCut ou indique o FreeCut.exe em Configurações → Integrações. "
			    "O arquivo ficou selecionado no Explorer.";
		return r;
	}
	QStringList args;
	for (const auto &f : existing)
		args << "--import" << f;
	r.launched = QProcess::startDetached(r.exe, args, QFileInfo(r.exe).absolutePath());
	logInfo(QString("Editar no FreeCut: %1 %2 -> %3").arg(r.exe, args.join(' ')).arg(r.launched ? "aberto" : "falhou"));
	r.message = r.launched ? "FreeCut aberto. O arquivo está selecionado no Explorer e o caminho foi copiado: arraste-o para a área de mídia do FreeCut."
			       : "Não foi possível abrir o FreeCut em " + r.exe;
	return r;
}

} // namespace sx
