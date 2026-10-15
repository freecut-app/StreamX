// StreamX Control — API HTTP local para controle remoto (token obrigatório, só rede local)
#pragma once
#include <QObject>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>

class QTcpServer;
class QTcpSocket;

namespace sx {

class ControlServer : public QObject {
	Q_OBJECT
public:
	static ControlServer &get();

	// lan = false: só aceita conexões deste PC (127.0.0.1)
	// lan = true: aceita da rede local (IPs privados); internet é sempre recusada
	bool start(quint16 port, bool lan, const QString &token, QString *error);
	void stop();
	bool isRunning() const;
	quint16 port() const { return port_; }
	QStringList addresses() const; // URLs para abrir no celular/PC

signals:
	void request(const QString &peer, const QString &line, int status);

private:
	ControlServer() = default;
	void onConnection();
	void handle(QTcpSocket *s);
	void respond(QTcpSocket *s, int status, const QByteArray &body, const QByteArray &type = "application/json; charset=utf-8");
	QJsonObject status() const;
	bool allowedPeer(const QHostAddress &a) const;
	QTcpServer *server_ = nullptr;
	quint16 port_ = 0;
	bool lan_ = false;
	QString token_;
	QHash<QString, QList<qint64>> failures_;
	QHash<QTcpSocket *, QByteArray> buffers_;
};

} // namespace sx
