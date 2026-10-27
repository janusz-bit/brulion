// MiniHttpServer — a tiny HTTP/1.1 server on 127.0.0.1 for the tests of pictures from the
// internet (tst_workspace_pictures, tst_appio_export): fixed bodies by path, 404 otherwise,
// stalled paths never answered, the requested paths recorded. Header-only; link Qt6::Network.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

/// A tiny HTTP/1.1 server on 127.0.0.1: `responses` by path, 404 for the rest.
class MiniHttpServer : public QObject {
public:
    QHash<QByteArray, QByteArray> bodies; ///< path → picture bytes
    QSet<QByteArray> stalled;             ///< paths never answered
    QList<QByteArray> requests;

    MiniHttpServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
            }
        });
    }
    QString url(const QString &path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path); }

private:
    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        if (!buffer.contains("\r\n\r\n"))
            return;
        const QByteArray path = buffer.left(buffer.indexOf('\n')).split(' ').value(1);
        m_buffers.remove(socket);
        requests.append(path);
        if (stalled.contains(path))
            return;
        const auto it = bodies.constFind(path);
        const QByteArray body = it == bodies.constEnd() ? QByteArray("<html>Nie ma</html>") : *it;
        const QByteArray status = it == bodies.constEnd() ? "404 Not Found" : "200 OK";
        socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nConnection: close\r\n\r\n" + body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};


