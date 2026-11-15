// FileServer — a small HTTP/1.1 server on 127.0.0.1 for the tests of downloads (the models' files:
// tst_ai_localfiles, tst_shell_localai): files by path, with ranges; faults on request — a server
// without ranges, a connection that breaks, a slow transfer. Header-only; link Qt6::Network.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <memory>

/// HTTP/1.1 on 127.0.0.1: files by path, with ranges; faults on request.
class FileServer : public QObject {
public:
    QHash<QByteArray, QByteArray> files;
    bool ranges = true;     ///< false: a Range header is ignored (200 with the whole file)
    qint64 breakAfter = -1; ///< the connection closes after this many bytes of a body
    bool slow = false;      ///< the body goes out in small pieces, a few milliseconds apart
    struct Request {
        QByteArray path;
        QByteArray range;
    };
    QList<Request> requests;

    FileServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
            }
        });
    }
    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }
    /// The bytes of answers not given to the network yet: what a client that stopped reading
    /// leaves here.
    qint64 unsent() const
    {
        qint64 bytes = 0;
        const QList<QTcpSocket *> sockets = findChildren<QTcpSocket *>();
        for (const QTcpSocket *socket : sockets)
            bytes += socket->bytesToWrite();
        return bytes;
    }

private:
    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        if (!buffer.contains("\r\n\r\n"))
            return;
        const QList<QByteArray> lines = buffer.split('\n');
        m_buffers.remove(socket);
        Request request;
        request.path = lines.value(0).split(' ').value(1);
        for (const QByteArray &line : lines) {
            if (line.toLower().startsWith("range:"))
                request.range = line.mid(6).trimmed();
        }
        requests.append(request);
        const auto it = files.constFind(request.path);
        if (it == files.constEnd()) {
            socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nNot found");
            socket->disconnectFromHost();
            return;
        }
        QByteArray body = *it;
        QByteArray head = "HTTP/1.1 200 OK\r\n";
        if (ranges && request.range.startsWith("bytes=")) {
            const qint64 from = request.range.mid(6).split('-').value(0).toLongLong();
            head = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + QByteArray::number(from) + "-"
                   + QByteArray::number(body.size() - 1) + "/" + QByteArray::number(body.size()) + "\r\n";
            body = body.mid(from);
        }
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
        socket->write(head);
        if (breakAfter >= 0)
            body.truncate(breakAfter);
        if (!slow) {
            socket->write(body);
            socket->disconnectFromHost();
            return;
        }
        auto *timer = new QTimer(socket);
        auto sent = std::make_shared<qint64>(0);
        connect(timer, &QTimer::timeout, socket, [socket, timer, body, sent] {
            if (*sent >= body.size()) {
                timer->stop();
                socket->disconnectFromHost();
                return;
            }
            socket->write(body.mid(*sent, 4096));
            *sent += 4096;
        });
        timer->start(2);
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
