// FakeAiServer — an OpenAI-compatible chat API on 127.0.0.1 for the tests of the app's AI (the
// chat's turns side by side, the editor's writing assistant): POST /v1/chat/completions answered
// with the next of `answers` (a text; "" a turn with no words; "tool:<name> <json arguments>" a call
// of that tool), as one JSON completion or — a request with "stream": true — as server-sent events
// a word at a time; `hold` keeps the answers until release(). The requests' bodies are recorded.
// Header-only; link Qt6::Network.
//
//   FakeAiServer server;
//   settings.provider = "openai"; settings.baseUrl = server.url(); settings.model = "fake";
//   server.answers = {"Ala ma kota."}; … server.requests.size() …
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

class FakeAiServer : public QObject {
public:
    /// The next answers, in order ("Dobrze." when none is left).
    QStringList answers;
    /// Answers wait until release() (each request is recorded when it arrives).
    bool hold = false;
    /// The bodies of the requests as they came (parsed JSON).
    QList<QJsonObject> requests;

    FakeAiServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
            }
        });
    }
    /// The API's base address (Settings::baseUrl).
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(m_server.serverPort()); }
    /// How many requests wait for their answer.
    int waiting() const { return int(m_waiting.size()); }
    /// Answers the waiting requests (all, or the first `count`) in the order they came.
    void release(int count = -1)
    {
        while (!m_waiting.isEmpty() && count-- != 0) {
            const Waiting w = m_waiting.takeFirst();
            if (w.socket)
                respond(w.socket, w.stream, w.answer);
        }
    }

    /// The text of a request's last user message.
    static QString lastUserText(const QJsonObject &request)
    {
        const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
        for (qsizetype i = messages.size() - 1; i >= 0; --i) {
            const QJsonObject m = messages.at(i).toObject();
            if (m.value(QStringLiteral("role")).toString() != QStringLiteral("user"))
                continue;
            const QJsonValue content = m.value(QStringLiteral("content"));
            if (content.isString())
                return content.toString();
            QString text;
            for (const QJsonValue part : content.toArray()) {
                if (part.toObject().value(QStringLiteral("type")).toString() == QStringLiteral("text"))
                    text += part.toObject().value(QStringLiteral("text")).toString();
            }
            return text;
        }
        return {};
    }

private:
    struct Waiting {
        QPointer<QTcpSocket> socket;
        bool stream = false;
        QString answer;
    };

    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        qsizetype length = 0;
        for (const QByteArray &line : buffer.left(headerEnd).split('\n')) {
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toLongLong();
        }
        if (buffer.size() < headerEnd + 4 + length)
            return;
        const QByteArray body = buffer.mid(headerEnd + 4, length);
        m_buffers.remove(socket);
        const QJsonObject request = QJsonDocument::fromJson(body).object();
        requests.append(request);
        const QString answer = answers.isEmpty() ? QStringLiteral("Dobrze.") : answers.takeFirst();
        const bool stream = request.value(QStringLiteral("stream")).toBool();
        if (hold) {
            m_waiting.append({socket, stream, answer});
            return;
        }
        respond(socket, stream, answer);
    }

    static QByteArray chunk(const QJsonObject &delta, const QString &finish = QString())
    {
        QJsonObject choice{{QStringLiteral("index"), 0}, {QStringLiteral("delta"), delta}};
        if (!finish.isEmpty())
            choice.insert(QStringLiteral("finish_reason"), finish);
        const QJsonObject event{{QStringLiteral("model"), QStringLiteral("fake")},
                                {QStringLiteral("choices"), QJsonArray{choice}}};
        return "data: " + QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n\n";
    }

    void respond(QTcpSocket *socket, bool stream, const QString &answer)
    {
        QByteArray body;
        QByteArray type = "application/json";
        if (answer.startsWith(QStringLiteral("tool:"))) {
            // A tool call, as one completion (a streaming client reads that too).
            const QString call = answer.mid(5);
            const qsizetype space = call.indexOf(QLatin1Char(' '));
            const QString name = space < 0 ? call : call.left(space);
            const QString arguments = space < 0 ? QStringLiteral("{}") : call.mid(space + 1);
            const QJsonObject function{{QStringLiteral("name"), name}, {QStringLiteral("arguments"), arguments}};
            const QJsonObject message{
                    {QStringLiteral("role"), QStringLiteral("assistant")},
                    {QStringLiteral("content"), QJsonValue()},
                    {QStringLiteral("tool_calls"),
                     QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("call_%1").arg(++m_calls)},
                                            {QStringLiteral("type"), QStringLiteral("function")},
                                            {QStringLiteral("function"), function}}}}};
            const QJsonObject completion{
                    {QStringLiteral("model"), QStringLiteral("fake")},
                    {QStringLiteral("choices"),
                     QJsonArray{QJsonObject{{QStringLiteral("index"), 0},
                                            {QStringLiteral("message"), message},
                                            {QStringLiteral("finish_reason"), QStringLiteral("tool_calls")}}}}};
            body = QJsonDocument(completion).toJson(QJsonDocument::Compact);
        } else if (stream) {
            type = "text/event-stream";
            const QStringList words = answer.split(QLatin1Char(' '));
            for (qsizetype i = 0; i < words.size(); ++i)
                body += chunk(QJsonObject{{QStringLiteral("content"),
                                           QString(words.at(i) + (i + 1 < words.size() ? QStringLiteral(" ") : QString()))}});
            body += chunk(QJsonObject(), QStringLiteral("stop"));
            body += "data: [DONE]\n\n";
        } else {
            const QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
                                      {QStringLiteral("content"), answer}};
            const QJsonObject completion{
                    {QStringLiteral("model"), QStringLiteral("fake")},
                    {QStringLiteral("choices"),
                     QJsonArray{QJsonObject{{QStringLiteral("index"), 0},
                                            {QStringLiteral("message"), message},
                                            {QStringLiteral("finish_reason"), QStringLiteral("stop")}}}}};
            body = QJsonDocument(completion).toJson(QJsonDocument::Compact);
        }
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\nConnection: close\r\n\r\n" + body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QList<Waiting> m_waiting;
    int m_calls = 0;
};
