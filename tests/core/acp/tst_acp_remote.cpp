// The Agent Client Protocol to an agent on another computer, this side (core acp/remote.h): the
// host's address and pairing codes as typed, its answers and errors read, and RemoteStream against
// a host the test plays — the agent started, lines sent in order and in batches, a batch sent again
// after a lost answer only from what the host lacks, messages received in order (none twice), the
// agent's end, a device unpaired, a stream the host forgot, a host unreachable for too long, stop(),
// and a stream deleted while a request is on its way.
#include "acp/remote.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

#include <functional>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::acp;

namespace {

/// A host the test answers: every request waits in `pending` until answered.
class FakeHost : public sync::HttpTransport {
public:
    struct Pending {
        sync::HttpRequest request;
        sync::HttpCallback done;
        QString endpoint() const
        {
            const QString path = request.url.path();
            const qsizetype at = path.indexOf(u"/acp/v1/"_s);
            return at < 0 ? path : path.mid(at + 8);
        }
        QJsonObject body() const { return QJsonDocument::fromJson(request.body).object(); }
        QUrlQuery query() const { return QUrlQuery(request.url); }
    };
    QList<Pending> pending;
    QList<sync::HttpRequest> log;

    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        log.append(request);
        pending.append({request, std::move(done)});
    }

    /// The first request waiting whose endpoint ends with `suffix` (nullopt: none).
    std::optional<Pending> take(const QString &suffix)
    {
        for (qsizetype i = 0; i < pending.size(); ++i) {
            if (pending.at(i).endpoint().endsWith(suffix))
                return pending.takeAt(i);
        }
        return std::nullopt;
    }
    bool waiting(const QString &suffix) const
    {
        return std::ranges::any_of(pending, [&suffix](const Pending &p) { return p.endpoint().endsWith(suffix); });
    }

    static sync::HttpResponse json(int status, const QJsonObject &o)
    {
        sync::HttpResponse r;
        r.status = status;
        r.body = QJsonDocument(o).toJson(QJsonDocument::Compact);
        r.headers.insert("content-type"_ba, "application/json"_ba);
        return r;
    }
    static sync::HttpResponse error(int status, const QString &code, const QString &message)
    {
        return json(status, QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}});
    }
    static sync::HttpResponse offline()
    {
        sync::HttpResponse r;
        r.networkError = u"Network unreachable"_s;
        return r;
    }
};

QJsonObject message(int id, const QString &method)
{
    return QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"method"_s, method}, {u"params"_s, QJsonObject()}};
}

QByteArray line(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject received(const QList<std::pair<qint64, QJsonObject>> &messages, bool closed = false,
                     const QString &why = {})
{
    QJsonArray list;
    for (const auto &[seq, m] : messages)
        list.append(QJsonObject{{u"seq"_s, seq}, {u"message"_s, m}});
    QJsonObject o{{u"messages"_s, list}, {u"closed"_s, closed}};
    if (!why.isEmpty())
        o.insert(u"why"_s, why);
    return o;
}

} // namespace

class TestAcpRemote : public QObject {
    Q_OBJECT

    const QUrl m_base{u"https://agent.example.pl/brulion"_s};

    /// A stream started: POST streams answered.
    std::unique_ptr<RemoteStream> started(FakeHost &host)
    {
        auto stream = std::make_unique<RemoteStream>(&host, m_base, u"bat_token"_s, u"claude"_s);
        stream->setRetryDelays({10});
        stream->start();
        std::optional<FakeHost::Pending> open = host.take(u"streams"_s);
        if (!open)
            return nullptr;
        open->done(FakeHost::json(
                201, QJsonObject{{u"stream"_s, u"s_ABC"_s}, {u"root"_s, u"/var/lib/brulion-agent/Notatki"_s}}));
        return stream;
    }

private Q_SLOTS:
    void addresses_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("base");
        QTest::newRow("a name") << u"agent.example.pl"_s << u"https://agent.example.pl"_s;
        QTest::newRow("https with a slash") << u"https://agent.example.pl/"_s << u"https://agent.example.pl"_s;
        QTest::newRow("a path") << u"https://example.pl/brulion/"_s << u"https://example.pl/brulion"_s;
        QTest::newRow("the API pasted") << u"https://agent.example.pl/acp/v1/info"_s << u"https://agent.example.pl"_s;
        QTest::newRow("http on a port") << u"http://127.0.0.1:8790"_s << u"http://127.0.0.1:8790"_s;
        QTest::newRow("spaces around") << u"  agent.ts.example.pl  "_s << u"https://agent.ts.example.pl"_s;
        QTest::newRow("empty") << u""_s << QString();
        QTest::newRow("another scheme") << u"ftp://agent.example.pl"_s << QString();
        QTest::newRow("user info") << u"https://jan:hasło@agent.example.pl"_s << QString();
        QTest::newRow("a query") << u"https://agent.example.pl/?x=1"_s << QString();
        QTest::newRow("no host") << u"https://"_s << QString();
    }
    void addresses()
    {
        QFETCH(QString, typed);
        QFETCH(QString, base);
        QCOMPARE(normalizeRemoteBase(typed).toString(), base);
    }

    void endpoints()
    {
        QCOMPARE(remoteUrl(QUrl(u"https://agent.example.pl"_s), u"info"_s).toString(),
                 u"https://agent.example.pl/acp/v1/info"_s);
        QCOMPARE(remoteUrl(QUrl(u"https://example.pl/brulion/"_s), u"streams/s_1/send"_s).toString(),
                 u"https://example.pl/brulion/acp/v1/streams/s_1/send"_s);
        QCOMPARE(remoteUrl(QUrl(u"https://example.pl"_s), u"streams/s_1/receive?after=3&wait=25"_s).toString(),
                 u"https://example.pl/acp/v1/streams/s_1/receive?after=3&wait=25"_s);
    }

    void pairingCodes_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("code");
        QTest::newRow("as shown") << u"K7QM-2XRP"_s << u"K7QM2XRP"_s;
        QTest::newRow("lower case, spaces") << u" k7qm 2xrp "_s << u"K7QM2XRP"_s;
        QTest::newRow("a dash of a keyboard") << u"K7QM–2XRP"_s << u"K7QM2XRP"_s;
        QTest::newRow("empty") << u""_s << u""_s;
    }
    void pairingCodes()
    {
        QFETCH(QString, typed);
        QFETCH(QString, code);
        QCOMPARE(normalizePairingCode(typed), code);
    }

    void info()
    {
        const std::optional<RemoteInfo> info = RemoteInfo::fromJson(
                QJsonObject{{u"protocol"_s, 1},
                            {u"version"_s, u"0.27.0"_s},
                            {u"host"_s, u"raspberry-pi-4"_s},
                            {u"root"_s, u"/var/lib/brulion-agent/Notatki"_s},
                            {u"tools"_s, true},
                            {u"agents"_s, QJsonArray{QJsonObject{{u"id"_s, u"claude"_s},
                                                                 {u"name"_s, u"Claude Code"_s},
                                                                 {u"command"_s, u"claude-agent-acp"_s}},
                                                     QJsonObject{{u"id"_s, u"bez-nazwy"_s}},
                                                     QJsonObject{{u"name"_s, u"no id: left out"_s}}}},
                            {u"sync"_s, QJsonObject{{u"enabled"_s, true}, {u"ok"_s, true}}}});
        QVERIFY(info);
        QCOMPARE(info->protocol, 1);
        QCOMPARE(info->host, u"raspberry-pi-4"_s);
        QCOMPARE(info->root, u"/var/lib/brulion-agent/Notatki"_s);
        QVERIFY(info->tools);
        QCOMPARE(info->agents.size(), 2);
        QCOMPARE(info->agents.at(0), (RemoteAgent{u"claude"_s, u"Claude Code"_s, u"claude-agent-acp"_s}));
        QCOMPARE(info->agents.at(1).name, u"bez-nazwy"_s);
        QVERIFY(info->sync.value(u"enabled"_s).toBool());
        QVERIFY(!RemoteInfo::fromJson(QJsonObject{{u"root"_s, u"/x"_s}}));
    }

    void errors()
    {
        RemoteError e = remoteErrorOf(FakeHost::offline());
        QCOMPARE(e.kind, RemoteError::Kind::Network);
        QCOMPARE(e.detail, u"Network unreachable"_s);
        e = remoteErrorOf(FakeHost::error(401, u"unauthorized"_s, u"This device is not paired (any more)"_s));
        QCOMPARE(e.kind, RemoteError::Kind::Unauthorized);
        QCOMPARE(e.code, u"unauthorized"_s);
        e = remoteErrorOf(FakeHost::error(403, u"bad_code"_s, u"That is not the pairing code"_s));
        QCOMPARE(e.kind, RemoteError::Kind::Refused);
        QCOMPARE(e.detail, u"That is not the pairing code"_s);
        e = remoteErrorOf(FakeHost::error(502, QString(), QString()));
        QCOMPARE(e.kind, RemoteError::Kind::Server);
        QCOMPARE(e.detail, u"HTTP 502"_s);
        sync::HttpResponse html;
        html.status = 200;
        html.body = "<html>a captive portal</html>";
        QCOMPARE(remoteErrorOf(html).kind, RemoteError::Kind::Malformed);
        QJsonObject o;
        e = remoteErrorOf(FakeHost::json(200, QJsonObject{{u"received"_s, 3}}), &o);
        QVERIFY(!e.failed());
        QCOMPARE(o.value(u"received"_s).toInt(), 3);
    }

    void pairAndInfo()
    {
        FakeHost host;
        RemotePairing paired;
        RemoteError pairError;
        bool pairDone = false;
        pairRemote(&host, m_base, u"k7qm-2xrp"_s, u"Pixel 9 Pro"_s, [&](const RemotePairing &p, const RemoteError &e) {
            paired = p;
            pairError = e;
            pairDone = true;
        });
        std::optional<FakeHost::Pending> pair = host.take(u"pair"_s);
        QVERIFY(pair);
        QCOMPARE(pair->request.method, "POST"_ba);
        QCOMPARE(pair->request.url.toString(), u"https://agent.example.pl/brulion/acp/v1/pair"_s);
        QCOMPARE(pair->body().value(u"code"_s).toString(), u"K7QM2XRP"_s);
        QCOMPARE(pair->body().value(u"device"_s).toString(), u"Pixel 9 Pro"_s);
        QVERIFY(std::ranges::none_of(pair->request.headers, [](const auto &h) { return h.first == "Authorization"; }));
        pair->done(FakeHost::json(
                200, QJsonObject{{u"token"_s, u"bat_xyz"_s},
                                 {u"device"_s, QJsonObject{{u"id"_s, u"dev_1"_s}, {u"name"_s, u"Pixel 9 Pro"_s}}}}));
        QVERIFY(pairDone);
        QVERIFY(!pairError.failed());
        QCOMPARE(paired.token, u"bat_xyz"_s);
        QCOMPARE(paired.deviceId, u"dev_1"_s);

        RemoteInfo info;
        RemoteError infoError;
        fetchRemoteInfo(&host, m_base, paired.token, [&](const RemoteInfo &i, const RemoteError &e) {
            info = i;
            infoError = e;
        });
        std::optional<FakeHost::Pending> got = host.take(u"info"_s);
        QVERIFY(got);
        QVERIFY(std::ranges::any_of(got->request.headers, [](const auto &h) {
            return h.first == "Authorization" && h.second == "Bearer bat_xyz";
        }));
        got->done(FakeHost::json(200, QJsonObject{{u"protocol"_s, 2}, {u"root"_s, u"/x"_s}}));
        QCOMPARE(infoError.kind, RemoteError::Kind::Version); // (a newer host this side cannot speak to)
        QCOMPARE(infoError.detail, u"2"_s);
    }

    void conversation()
    {
        FakeHost host;
        auto stream = std::make_unique<RemoteStream>(&host, m_base, u"bat_token"_s, u"claude"_s);
        QSignalSpy startedSpy(stream.get(), &RemoteStream::started);
        QSignalSpy receivedSpy(stream.get(), &RemoteStream::received);
        QSignalSpy endedSpy(stream.get(), &RemoteStream::ended);
        // Lines written before the agent runs wait for it.
        stream->write(line(message(1, u"initialize"_s)));
        stream->start();
        stream->write(line(message(2, u"session/new"_s)));
        QCOMPARE(host.pending.size(), 1);
        std::optional<FakeHost::Pending> open = host.take(u"streams"_s);
        QVERIFY(open);
        QCOMPARE(open->body().value(u"agent"_s).toString(), u"claude"_s);
        open->done(FakeHost::json(201, QJsonObject{{u"stream"_s, u"s_ABC"_s}, {u"root"_s, u"/srv/Notatki"_s}}));
        QCOMPARE(startedSpy.size(), 1);
        QCOMPARE(startedSpy.at(0).at(0).toString(), u"/srv/Notatki"_s);
        QCOMPARE(stream->root(), u"/srv/Notatki"_s);

        // Both lines in one batch, in order, from 0; a receive waits meanwhile.
        std::optional<FakeHost::Pending> send = host.take(u"s_ABC/send"_s);
        QVERIFY(send);
        QCOMPARE(send->body().value(u"from"_s).toInt(), 0);
        QJsonArray sent = send->body().value(u"messages"_s).toArray();
        QCOMPARE(sent.size(), 2);
        QCOMPARE(sent.at(0).toObject().value(u"method"_s).toString(), u"initialize"_s);
        QCOMPARE(sent.at(1).toObject().value(u"method"_s).toString(), u"session/new"_s);
        std::optional<FakeHost::Pending> poll = host.take(u"s_ABC/receive"_s);
        QVERIFY(poll);
        QCOMPARE(poll->query().queryItemValue(u"after"_s), u"0"_s);
        QCOMPARE(poll->query().queryItemValue(u"wait"_s), u"25"_s);
        QVERIFY(poll->request.timeoutMs > 25000);

        // One more line while the batch is on its way: it goes after the batch's answer.
        stream->write(line(message(3, u"session/prompt"_s)));
        QVERIFY(!host.waiting(u"send"_s));
        send->done(FakeHost::json(200, QJsonObject{{u"received"_s, 2}}));
        send = host.take(u"s_ABC/send"_s);
        QVERIFY(send);
        QCOMPARE(send->body().value(u"from"_s).toInt(), 2);
        QCOMPARE(send->body().value(u"messages"_s).toArray().size(), 1);
        send->done(FakeHost::json(200, QJsonObject{{u"received"_s, 3}}));

        // The agent's messages, whole lines in order; one heard again (the same seq) is left out.
        const QJsonObject answer{
                {u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, 1}, {u"result"_s, QJsonObject{{u"protocolVersion"_s, 1}}}};
        const QJsonObject update{{u"jsonrpc"_s, u"2.0"_s},
                                 {u"method"_s, u"session/update"_s},
                                 {u"params"_s, QJsonObject{{u"text"_s, u"Zażółć gęślą jaźń"_s}}}};
        poll->done(FakeHost::json(200, received({{1, answer}, {2, update}})));
        QCOMPARE(receivedSpy.size(), 1);
        QCOMPARE(receivedSpy.at(0).at(0).toByteArray(), line(answer) + line(update));
        poll = host.take(u"s_ABC/receive"_s);
        QVERIFY(poll);
        QCOMPARE(poll->query().queryItemValue(u"after"_s), u"2"_s);
        poll->done(FakeHost::json(200, received({{2, update}, {3, answer}})));
        QCOMPARE(receivedSpy.size(), 2);
        QCOMPARE(receivedSpy.at(1).at(0).toByteArray(), line(answer));

        // The agent ends on the host: what it said last, then the end.
        poll = host.take(u"s_ABC/receive"_s);
        QVERIFY(poll);
        poll->done(FakeHost::json(200, received({{4, update}}, true, u"it ended with code 3: fake agent: koniec"_s)));
        QCOMPARE(receivedSpy.size(), 3);
        QCOMPARE(endedSpy.size(), 1);
        QCOMPARE(endedSpy.at(0).at(0).value<RemoteStream::End>(), RemoteStream::End::AgentEnded);
        QCOMPARE(endedSpy.at(0).at(1).toString(), u"it ended with code 3: fake agent: koniec"_s);
        QVERIFY(stream->isEnded());
        QVERIFY(host.pending.isEmpty());
        stream->write(line(message(9, u"session/prompt"_s))); // (nothing more goes)
        QVERIFY(host.pending.isEmpty());
    }

    void batchSentAgain()
    {
        FakeHost host;
        std::unique_ptr<RemoteStream> stream = started(host);
        QVERIFY(stream);
        for (int i = 1; i <= 3; ++i)
            stream->write(line(message(i, u"m"_s)));
        QVERIFY(!host.waiting(u"send"_s)); // (once the event loop goes on: all in one batch)
        QTRY_VERIFY(host.waiting(u"send"_s));
        std::optional<FakeHost::Pending> send = host.take(u"send"_s);
        QVERIFY(send);
        QCOMPARE(send->body().value(u"messages"_s).toArray().size(), 3);
        // The answer is lost: the same batch again, from the same place.
        send->done(FakeHost::offline());
        QTRY_VERIFY(host.waiting(u"send"_s));
        send = host.take(u"send"_s);
        QCOMPARE(send->body().value(u"from"_s).toInt(), 0);
        QCOMPARE(send->body().value(u"messages"_s).toArray().size(), 3);
        // The host had two of them (the lost answer's batch reached it in part): only the third again.
        send->done(FakeHost::json(200, QJsonObject{{u"received"_s, 2}}));
        send = host.take(u"send"_s);
        QVERIFY(send);
        QCOMPARE(send->body().value(u"from"_s).toInt(), 2);
        QJsonArray rest = send->body().value(u"messages"_s).toArray();
        QCOMPARE(rest.size(), 1);
        QCOMPARE(rest.at(0).toObject().value(u"id"_s).toInt(), 3);
        send->done(FakeHost::json(200, QJsonObject{{u"received"_s, 3}}));
        QVERIFY(!host.waiting(u"send"_s));
    }

    void largeBatches()
    {
        FakeHost host;
        std::unique_ptr<RemoteStream> stream = started(host);
        QVERIFY(stream);
        for (int i = 0; i < RemoteMaxBatch + 5; ++i)
            stream->write(line(message(i, u"m"_s)));
        QTRY_VERIFY(host.waiting(u"send"_s));
        std::optional<FakeHost::Pending> send = host.take(u"send"_s);
        QVERIFY(send);
        QCOMPARE(send->body().value(u"messages"_s).toArray().size(), RemoteMaxBatch);
        send->done(FakeHost::json(200, QJsonObject{{u"received"_s, RemoteMaxBatch}}));
        send = host.take(u"send"_s);
        QVERIFY(send);
        QCOMPARE(send->body().value(u"from"_s).toInt(), RemoteMaxBatch);
        QCOMPARE(send->body().value(u"messages"_s).toArray().size(), 5);
    }

    void ends_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("code");
        QTest::addColumn<RemoteStream::End>("end");
        QTest::newRow("unpaired") << 401 << u"unauthorized"_s << RemoteStream::End::Unauthorized;
        QTest::newRow("forgotten") << 404 << u"no_stream"_s << RemoteStream::End::Gone;
    }
    void ends()
    {
        QFETCH(int, status);
        QFETCH(QString, code);
        QFETCH(RemoteStream::End, end);
        FakeHost host;
        std::unique_ptr<RemoteStream> stream = started(host);
        QVERIFY(stream);
        QSignalSpy endedSpy(stream.get(), &RemoteStream::ended);
        std::optional<FakeHost::Pending> poll = host.take(u"receive"_s);
        QVERIFY(poll);
        poll->done(FakeHost::error(status, code, u"powód"_s));
        QCOMPARE(endedSpy.size(), 1);
        QCOMPARE(endedSpy.at(0).at(0).value<RemoteStream::End>(), end);
        QCOMPARE(endedSpy.at(0).at(2).toString(), code);
        QVERIFY(host.pending.isEmpty());
    }

    void refusedToStart()
    {
        FakeHost host;
        RemoteStream stream(&host, m_base, u"bat_token"_s, u"nie-ma"_s);
        QSignalSpy endedSpy(&stream, &RemoteStream::ended);
        stream.start();
        std::optional<FakeHost::Pending> open = host.take(u"streams"_s);
        QVERIFY(open);
        open->done(FakeHost::error(404, u"unknown_agent"_s, u"This computer has no agent nie-ma"_s));
        QCOMPARE(endedSpy.size(), 1);
        QCOMPARE(endedSpy.at(0).at(0).value<RemoteStream::End>(), RemoteStream::End::Refused);
        QCOMPARE(endedSpy.at(0).at(1).toString(), u"This computer has no agent nie-ma"_s);
        QCOMPARE(endedSpy.at(0).at(2).toString(), u"unknown_agent"_s);
    }

    void unreachable()
    {
        FakeHost host;
        qint64 clock = 0;
        std::unique_ptr<RemoteStream> stream = started(host);
        QVERIFY(stream);
        stream->setClock([&clock] { return clock; });
        stream->setUnreachableMs(60000);
        QSignalSpy endedSpy(stream.get(), &RemoteStream::ended);
        // A phone in a tunnel: the receives fail, are tried again …
        std::optional<FakeHost::Pending> poll = host.take(u"receive"_s);
        poll->done(FakeHost::offline());
        QTRY_VERIFY(host.waiting(u"receive"_s));
        clock = 30000;
        host.take(u"receive"_s)->done(FakeHost::offline());
        QTRY_VERIFY(host.waiting(u"receive"_s));
        // … one gets through: the count starts again.
        host.take(u"receive"_s)->done(FakeHost::json(200, received({})));
        QVERIFY(host.waiting(u"receive"_s));
        clock = 80000;
        host.take(u"receive"_s)->done(FakeHost::offline());
        QTRY_VERIFY(host.waiting(u"receive"_s));
        QCOMPARE(endedSpy.size(), 0);
        // A minute of nothing after that: the stream is over.
        clock = 140001;
        host.take(u"receive"_s)->done(FakeHost::offline());
        QCOMPARE(endedSpy.size(), 1);
        QCOMPARE(endedSpy.at(0).at(0).value<RemoteStream::End>(), RemoteStream::End::Unreachable);
        QCOMPARE(endedSpy.at(0).at(1).toString(), u"Network unreachable"_s);
        QTest::qWait(50);
        QVERIFY(host.pending.isEmpty());
    }

    void stop()
    {
        FakeHost host;
        std::unique_ptr<RemoteStream> stream = started(host);
        QVERIFY(stream);
        QSignalSpy endedSpy(stream.get(), &RemoteStream::ended);
        QSignalSpy receivedSpy(stream.get(), &RemoteStream::received);
        std::optional<FakeHost::Pending> poll = host.take(u"receive"_s);
        stream->stop();
        std::optional<FakeHost::Pending> del = host.take(u"streams/s_ABC"_s);
        QVERIFY(del);
        QCOMPARE(del->request.method, "DELETE"_ba);
        // What the receive brings after stop() is not heard.
        poll->done(FakeHost::json(200, received({{1, message(1, u"x"_s)}})));
        QCOMPARE(receivedSpy.size(), 0);
        QCOMPARE(endedSpy.size(), 0);
        del->done(FakeHost::json(204, {}));
        QVERIFY(host.pending.isEmpty());
    }

    void deletedWithRequestsOnTheirWay()
    {
        FakeHost host;
        std::unique_ptr<RemoteStream> stream = started(host);
        QVERIFY(stream);
        stream->write(line(message(1, u"m"_s)));
        QTRY_COMPARE(host.pending.size(), 2); // a send and a receive
        stream.reset();
        for (FakeHost::Pending &p : host.pending)
            p.done(FakeHost::json(200, received({{1, message(1, u"x"_s)}})));
        host.pending.clear();
    }

    void receiverDeletesTheStream()
    {
        // The app ends a conversation in answer to what it heard: nothing touches the stream after.
        FakeHost host;
        std::unique_ptr<RemoteStream> stream = started(host);
        QVERIFY(stream);
        RemoteStream *raw = stream.get();
        connect(raw, &RemoteStream::received, this, [&stream] { stream.reset(); });
        host.take(u"receive"_s)->done(FakeHost::json(200, received({{1, message(1, u"x"_s)}}, true, u"koniec"_s)));
        QVERIFY(!stream);
        QVERIFY(host.pending.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAcpRemote)
#include "tst_acp_remote.moc"
