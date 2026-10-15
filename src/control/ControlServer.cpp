#include "ControlServer.h"
#include "../core/ClipIt.h"
#include "../core/Log.h"
#include "../core/PerfMonitor.h"
#include "../core/Recorder.h"
#include "../core/SceneManager.h"
#include "../core/Streamer.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>

namespace sx {

ControlServer &ControlServer::get()
{
	static ControlServer c;
	return c;
}

bool ControlServer::isRunning() const { return server_ && server_->isListening(); }

bool ControlServer::start(quint16 port, bool lan, const QString &token, QString *error)
{
	stop();
	if (token.size() < 16) {
		if (error)
			*error = "Token inválido.";
		return false;
	}
	server_ = new QTcpServer(this);
	if (!server_->listen(lan ? QHostAddress::AnyIPv4 : QHostAddress::LocalHost, port)) {
		if (error)
			*error = QString("Não foi possível abrir a porta %1: %2").arg(port).arg(server_->errorString());
		delete server_;
		server_ = nullptr;
		return false;
	}
	port_ = port;
	lan_ = lan;
	token_ = token;
	connect(server_, &QTcpServer::newConnection, this, &ControlServer::onConnection);
	logInfo(QString("StreamX Control ativo na porta %1 (%2)").arg(port).arg(lan ? "rede local" : "somente este PC"));
	return true;
}

void ControlServer::stop()
{
	if (!server_)
		return;
	server_->close();
	server_->deleteLater();
	server_ = nullptr;
	logInfo("StreamX Control desligado");
}

QStringList ControlServer::addresses() const
{
	QStringList out{QString("http://127.0.0.1:%1/").arg(port_)};
	if (!lan_)
		return out;
	for (const auto &a : QNetworkInterface::allAddresses())
		if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && a.isPrivateUse())
			out << QString("http://%1:%2/").arg(a.toString()).arg(port_);
	return out;
}

bool ControlServer::allowedPeer(const QHostAddress &a) const
{
	QHostAddress v4 = a;
	bool ok = false;
	const quint32 ip = a.toIPv4Address(&ok);
	if (ok)
		v4 = QHostAddress(ip);
	if (v4.isLoopback())
		return true;
	return lan_ && (v4.isPrivateUse() || v4.isLinkLocal());
}

void ControlServer::onConnection()
{
	while (QTcpSocket *s = server_->nextPendingConnection()) {
		if (!allowedPeer(s->peerAddress())) {
			s->abort();
			s->deleteLater();
			continue;
		}
		connect(s, &QTcpSocket::readyRead, this, [this, s]() { handle(s); });
		connect(s, &QTcpSocket::disconnected, this, [this, s]() { buffers_.remove(s); s->deleteLater(); });
	}
}

void ControlServer::respond(QTcpSocket *s, int status, const QByteArray &body, const QByteArray &type)
{
	static const QHash<int, QByteArray> text = {{200, "OK"}, {400, "Bad Request"}, {401, "Unauthorized"}, {404, "Not Found"},
						     {409, "Conflict"}, {413, "Payload Too Large"}, {429, "Too Many Requests"}};
	QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + " " + text.value(status, "Error") + "\r\n";
	r += "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
	r += "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n";
	r += body;
	s->write(r);
	s->disconnectFromHost();
}

static QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }

// comparação em tempo constante (não vaza o token por tempo de resposta)
static bool sameToken(const QByteArray &a, const QByteArray &b)
{
	if (a.size() != b.size())
		return false;
	unsigned char d = 0;
	for (int i = 0; i < a.size(); i++)
		d |= uchar(a[i]) ^ uchar(b[i]);
	return d == 0;
}

QJsonObject ControlServer::status() const
{
	auto &rec = Recorder::get();
	auto &st = Streamer::get();
	auto &clip = ClipIt::get();
	const auto perf = PerfMonitor::get().last();
	QJsonArray scenes;
	for (const auto &n : SceneManager::get().sceneNames())
		scenes << n;
	return QJsonObject{
		{"app", "StreamX"}, {"version", STREAMX_VERSION},
		{"recording", QJsonObject{{"active", rec.isActive()}, {"elapsedMs", rec.elapsedMs()}, {"file", rec.lastFile()}}},
		{"streaming", QJsonObject{{"active", st.isActive()}, {"elapsedMs", st.elapsedMs()}}},
		{"clipIt", QJsonObject{{"active", clip.isActive()}, {"seconds", clip.seconds()}}},
		{"scene", SceneManager::get().currentSceneName()}, {"scenes", scenes},
		{"perf", QJsonObject{{"cpu", qRound(perf.cpuSystem)}, {"gpu", qRound(perf.gpu)}, {"ramGB", perf.ramUsedGB}, {"fps", perf.fps}}},
	};
}

void ControlServer::handle(QTcpSocket *s)
{
	QByteArray &buf = buffers_[s];
	buf += s->readAll();
	if (buf.size() > 65536)
		return respond(s, 413, json({{"error", "requisição grande demais"}}));
	const int headEnd = buf.indexOf("\r\n\r\n");
	if (headEnd < 0)
		return; // aguarda o restante
	const QList<QByteArray> lines = buf.left(headEnd).split('\n');
	const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
	if (first.size() < 2)
		return respond(s, 400, json({{"error", "requisição inválida"}}));
	const QByteArray method = first[0], path = first[1].split('?').first();
	QHash<QByteArray, QByteArray> headers;
	for (int i = 1; i < lines.size(); i++) {
		const int c = lines[i].indexOf(':');
		if (c > 0)
			headers[lines[i].left(c).trimmed().toLower()] = lines[i].mid(c + 1).trimmed();
	}
	const int len = headers.value("content-length", "0").toInt();
	if (buf.size() < headEnd + 4 + len)
		return; // corpo incompleto
	const QByteArray body = buf.mid(headEnd + 4, len);
	const QString peer = s->peerAddress().toString();
	const QString line = QString::fromUtf8(method + " " + path);
	auto reply = [&](int st, const QByteArray &b, const QByteArray &type = "application/json; charset=utf-8") {
		emit request(peer, line, st);
		respond(s, st, b, type);
	};

	if (method == "GET" && (path == "/" || path == "/index.html")) {
		QFile f(":/control/remote.html");
		f.open(QIODevice::ReadOnly);
		return reply(200, f.readAll(), "text/html; charset=utf-8");
	}
	if (!path.startsWith("/api/"))
		return reply(404, json({{"error", "não encontrado"}}));

	// limite de tentativas: 10 falhas por minuto por IP
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	auto &fails = failures_[peer];
	fails.erase(std::remove_if(fails.begin(), fails.end(), [now](qint64 t) { return now - t > 60000; }), fails.end());
	if (fails.size() >= 10)
		return reply(429, json({{"error", "muitas tentativas, aguarde 1 minuto"}}));
	QByteArray tok = headers.value("x-streamx-token");
	if (tok.isEmpty() && headers.value("authorization").startsWith("Bearer "))
		tok = headers.value("authorization").mid(7);
	if (!sameToken(tok, token_.toUtf8())) {
		fails << now;
		return reply(401, json({{"error", "token inválido"}}));
	}

	QString err;
	auto ok = [&](bool success) {
		QJsonObject o = status();
		o["ok"] = success;
		if (!success)
			o["error"] = err;
		reply(success ? 200 : 409, json(o));
	};
	if (method == "GET" && path == "/api/status")
		return reply(200, json(status()));
	if (method != "POST")
		return reply(404, json({{"error", "use POST para comandos"}}));
	auto &rec = Recorder::get();
	auto &st = Streamer::get();
	auto &clip = ClipIt::get();
	if (path == "/api/record/start") return ok(rec.isActive() || rec.start(&err));
	if (path == "/api/record/stop") { rec.stop(); return ok(true); }
	if (path == "/api/record/toggle") { if (rec.isActive()) { rec.stop(); return ok(true); } return ok(rec.start(&err)); }
	if (path == "/api/stream/start") return ok(st.isActive() || st.start(&err));
	if (path == "/api/stream/stop") { st.stop(); return ok(true); }
	if (path == "/api/clip/start") return ok(clip.isActive() || clip.start(&err));
	if (path == "/api/clip/stop") { clip.stop(); return ok(true); }
	if (path == "/api/clip/save") return ok(clip.save(&err));
	if (path == "/api/scene") {
		const QString name = QJsonDocument::fromJson(body).object()["name"].toString();
		err = "cena não encontrada";
		return ok(SceneManager::get().setCurrentScene(name));
	}
	reply(404, json({{"error", "comando desconhecido"}}));
}

} // namespace sx
