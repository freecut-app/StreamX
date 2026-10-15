#include "Log.h"
#include "Engine.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QTextStream>
#include <util/base.h>
#include <cstdarg>
#include <cstdio>

namespace sx {

static QFile g_file;
static QMutex g_mutex;

QString logFilePath() { return g_file.fileName(); }

static void write(const char *level, const QString &msg)
{
	QMutexLocker lock(&g_mutex);
	const QString line = QString("%1 [%2] %3\n").arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), level, msg);
	if (g_file.isOpen()) {
		g_file.write(line.toUtf8());
		g_file.flush();
	}
	fputs(line.toUtf8().constData(), stderr);
}

void logInfo(const QString &m) { write("INFO", m); }
void logWarn(const QString &m) { write("AVISO", m); }
void logError(const QString &m) { write("ERRO", m); }

// Redireciona os logs do libobs para o mesmo arquivo
static void obsLogHandler(int lvl, const char *fmt, va_list args, void *)
{
	char buf[4096];
	vsnprintf(buf, sizeof(buf), fmt, args);
	const char *level = lvl <= LOG_ERROR ? "OBS-ERRO" : lvl <= LOG_WARNING ? "OBS-AVISO" : lvl <= LOG_INFO ? "OBS" : nullptr;
	if (level)
		write(level, QString::fromUtf8(buf));
}

void initLog()
{
	const QString dir = Engine::configDir() + "/logs";
	QDir().mkpath(dir);
	// mantém só os 10 logs mais recentes
	QDir d(dir);
	const auto old = d.entryInfoList({"*.txt"}, QDir::Files, QDir::Time);
	for (int i = 9; i < old.size(); i++)
		QFile::remove(old[i].absoluteFilePath());
	g_file.setFileName(dir + "/" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".txt");
	if (!g_file.open(QIODevice::WriteOnly | QIODevice::Text))
		fputs("StreamX: nao foi possivel criar o arquivo de log\n", stderr);
	base_set_log_handler(obsLogHandler, nullptr);
}

} // namespace sx
