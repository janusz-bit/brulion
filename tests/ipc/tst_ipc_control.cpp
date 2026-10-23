// The remote control protocol and socket (src/ipc: controlprotocol.h, controlserver.h,
// controlclient.h): request lines parsed and refused when malformed (not JSON, no object, no
// method, a bad id or params, unknown fields, too long), every method's parameters checked
// (unknown method, unknown parameter, wrong type, out of range, an unknown enum value, a missing
// one, targets of two kinds or half a point, conditions of waitFor), modifiers, buttons and
// route matching; and the socket end to end: owner-only permissions (0600), answers in any
// order (a later answer, a client gone meanwhile), Polish text through it, errors for bad lines,
// a live server never taken over, a stale socket file replaced, the file gone after close().
#include "controlclient.h"
#include "controlprotocol.h"
#include "controlserver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <QElapsedTimer>

#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

using namespace Qt::StringLiterals;
using namespace brulion::ipc;

namespace {

QJsonObject obj(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

/// The next response line on a raw socket (the event loop runs meanwhile: the server may live in
/// this thread).
QJsonObject nextResponse(QLocalSocket &socket)
{
    QElapsedTimer t;
    t.start();
    while (!socket.canReadLine() && t.elapsed() < 5000)
        QTest::qWait(10);
    return QJsonDocument::fromJson(socket.readLine()).object();
}

/// A line sent on a raw socket and the response line read back.
QJsonObject exchange(QLocalSocket &socket, const QByteArray &line)
{
    socket.write(line + '\n');
    socket.flush();
    return nextResponse(socket);
}

/// A ControlServer on a thread of its own: ControlClient blocks its thread while it waits.
class ServerThread : public QThread {
public:
    ServerThread(const QString &key, ControlServer::Handler handler) : m_key(key), m_handler(std::move(handler)) { }
    ~ServerThread() override
    {
        quit();
        wait();
    }
    /// Start and wait until it listens (or failed to).
    bool startListening()
    {
        start();
        m_ready.acquire();
        return m_listening;
    }
    QString error;

protected:
    void run() override
    {
        ControlServer server;
        server.setHandler(m_handler);
        m_listening = server.listen(m_key);
        error = server.errorString();
        m_ready.release();
        exec();
    }

private:
    QString m_key;
    ControlServer::Handler m_handler;
    QSemaphore m_ready;
    bool m_listening = false;
};

} // namespace

class TestIpcControl : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString key(const QString &name) const { return m_dir.filePath(name); }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void keyNextToTheSingleInstanceOne()
    {
        QCOMPARE(controlKeyFor(u"/run/user/1000/brulion-0123456789abcdef"_s),
                 u"/run/user/1000/brulion-0123456789abcdef-control"_s);
    }

    void parseRequests_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<QString>("error"); // "" when it parses
        QTest::addColumn<QJsonValue>("id");
        QTest::addColumn<QString>("method");
        QTest::newRow("full") << R"({"id":1,"method":"state","params":{}})"_ba << QString() << QJsonValue(1)
                              << u"state"_s;
        QTest::newRow("string id, no params")
                << R"({"id":"a-1","method":"hello"})"_ba << QString() << QJsonValue(u"a-1"_s) << u"hello"_s;
        QTest::newRow("no id") << R"({"method":"hello"})"_ba << QString() << QJsonValue() << u"hello"_s;
        QTest::newRow("spaces around") << "   {\"id\":2,\"method\":\"quit\"}  \r"_ba << QString() << QJsonValue(2)
                                       << u"quit"_s;
        QTest::newRow("Polish params") << R"({"id":3,"method":"type","params":{"text":"zażółć gęślą jaźń"}})"_ba
                                       << QString() << QJsonValue(3) << u"type"_s;
        QTest::newRow("not JSON") << "{nie json"_ba << u"parse_error"_s << QJsonValue() << QString();
        QTest::newRow("empty") << ""_ba << u"parse_error"_s << QJsonValue() << QString();
        QTest::newRow("an array") << R"([{"id":1,"method":"state"}])"_ba << u"invalid_request"_s << QJsonValue()
                                  << QString();
        // QJsonDocument takes only an object or an array at the top.
        QTest::newRow("a string") << R"("state")"_ba << u"parse_error"_s << QJsonValue() << QString();
        QTest::newRow("no method") << R"({"id":4})"_ba << u"invalid_request"_s << QJsonValue(4) << QString();
        QTest::newRow("empty method") << R"({"id":5,"method":""})"_ba << u"invalid_request"_s << QJsonValue(5)
                                      << QString();
        QTest::newRow("method a number") << R"({"id":6,"method":7})"_ba << u"invalid_request"_s << QJsonValue(6)
                                         << QString();
        QTest::newRow("id an object") << R"({"id":{},"method":"state"})"_ba << u"invalid_request"_s << QJsonValue()
                                      << QString();
        QTest::newRow("params an array") << R"({"id":8,"method":"state","params":[1]})"_ba << u"invalid_request"_s
                                         << QJsonValue(8) << QString();
        QTest::newRow("unknown field") << R"({"id":9,"method":"state","pararms":{}})"_ba << u"invalid_request"_s
                                       << QJsonValue(9) << QString();
        QTest::newRow("too long") << (R"({"id":10,"method":"type","params":{"text":")"_ba
                                      + QByteArray(MaxLineBytes, 'a') + R"("}})"_ba)
                                  << u"invalid_request"_s << QJsonValue() << QString();
    }
    void parseRequests()
    {
        QFETCH(QByteArray, line);
        QFETCH(QString, error);
        QFETCH(QJsonValue, id);
        QFETCH(QString, method);
        ControlFailure failure;
        const auto request = parseRequest(line, &failure);
        if (error.isEmpty()) {
            QVERIFY2(request, qPrintable(failure.message));
            QCOMPARE(request->id, id);
            QCOMPARE(request->method, method);
        } else {
            QVERIFY(!request);
            QCOMPARE(failure.code, error);
            QCOMPARE(failure.id, id);
            QVERIFY(!failure.message.isEmpty());
        }
    }

    void encodeAndBack()
    {
        const QByteArray line = encodeRequest(QJsonValue(7), u"type"_s, {{u"text"_s, u"ĄĆĘŁŃÓŚŹŻ ąćęłńóśźż"_s}});
        QVERIFY(!line.contains('\n'));
        ControlFailure failure;
        const auto request = parseRequest(line, &failure);
        QVERIFY(request);
        QCOMPARE(request->params.value(u"text"_s).toString(), u"ĄĆĘŁŃÓŚŹŻ ąćęłńóśźż"_s);
        const QJsonObject ok = QJsonDocument::fromJson(encodeResult(QJsonValue(7), QJsonObject{{u"a"_s, 1}})).object();
        QCOMPARE(ok.value(u"ok"_s).toBool(), true);
        QCOMPARE(ok.value(u"id"_s).toInt(), 7);
        QCOMPARE(ok.value(u"result"_s).toObject().value(u"a"_s).toInt(), 1);
        const QJsonObject error = QJsonDocument::fromJson(encodeError(QJsonValue(u"x"_s), u"not_found"_s, u"Nie ma"_s,
                                                                      {{u"handle"_s, u"h1"_s}}))
                                          .object();
        QCOMPARE(error.value(u"ok"_s).toBool(true), false);
        QCOMPARE(error.value(u"id"_s).toString(), u"x"_s);
        QCOMPARE(error.value(u"error"_s).toObject().value(u"code"_s).toString(), u"not_found"_s);
        QCOMPARE(error.value(u"error"_s).toObject().value(u"details"_s).toObject().value(u"handle"_s).toString(),
                 u"h1"_s);
    }

    void checkRequests_data()
    {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QJsonObject>("params");
        QTest::addColumn<QString>("error"); // "" when fine
        QTest::addColumn<QString>("param"); // details.param, when the error names one
        const QString bad = u"bad_params"_s;
        QTest::newRow("hello") << u"hello"_s << QJsonObject() << QString() << QString();
        QTest::newRow("state with a param") << u"state"_s << obj(R"({"all":true})") << bad << u"all"_s;
        QTest::newRow("unknown method") << u"explode"_s << QJsonObject() << u"unknown_method"_s << QString();
        QTest::newRow("method case") << u"State"_s << QJsonObject() << u"unknown_method"_s << QString();
        QTest::newRow("navigate a view") << u"navigate"_s << obj(R"({"route":"tasks"})") << QString() << QString();
        QTest::newRow("navigate a route")
                << u"navigate"_s << obj(R"({"route":{"view":"page","path":"Żółw.md"}})") << QString() << QString();
        QTest::newRow("navigate without a route") << u"navigate"_s << QJsonObject() << bad << u"route"_s;
        QTest::newRow("navigate a number") << u"navigate"_s << obj(R"({"route":5})") << bad << u"route"_s;
        QTest::newRow("navigate side a string")
                << u"navigate"_s << obj(R"({"route":"tasks","side":"yes"})") << bad << u"side"_s;
        QTest::newRow("open nothing") << u"open"_s << QJsonObject() << QString() << QString();
        QTest::newRow("open a line") << u"open"_s << obj(R"({"ref":"Dom","line":12})") << QString() << QString();
        QTest::newRow("open a fractional line") << u"open"_s << obj(R"({"ref":"Dom","line":1.5})") << bad << u"line"_s;
        QTest::newRow("open a negative line") << u"open"_s << obj(R"({"ref":"Dom","line":-1})") << bad << u"line"_s;
        QTest::newRow("command") << u"command"_s << obj(R"({"id":"go.tasks"})") << QString() << QString();
        QTest::newRow("command without id") << u"command"_s << QJsonObject() << bad << u"id"_s;
        QTest::newRow("capture Polish") << u"capture"_s << obj(R"({"text":"Kupić mleko jutro o 18"})") << QString()
                                        << QString();
        QTest::newRow("screenshot") << u"screenshot"_s << obj(R"({"path":"/tmp/a.png"})") << QString() << QString();
        QTest::newRow("find a text") << u"find"_s << obj(R"({"text":"Zadania","limit":5})") << QString() << QString();
        QTest::newRow("find nothing") << u"find"_s << obj(R"({"visibleOnly":false})") << bad << QString();
        QTest::newRow("find limit 0") << u"find"_s << obj(R"({"text":"a","limit":0})") << bad << u"limit"_s;
        QTest::newRow("find with a handle") << u"find"_s << obj(R"({"handle":"h1"})") << bad << u"handle"_s;
        QTest::newRow("click a handle") << u"click"_s << obj(R"({"handle":"h3"})") << QString() << QString();
        QTest::newRow("click a point") << u"click"_s << obj(R"({"x":10,"y":20.5,"button":"Right"})") << QString()
                                       << QString();
        QTest::newRow("click a text with modifiers")
                << u"click"_s << obj(R"({"text":"Zażółć","modifiers":"ctrl+shift","device":"touch"})") << QString()
                << QString();
        QTest::newRow("click modifiers as a list")
                << u"click"_s << obj(R"({"text":"a","modifiers":["Ctrl","Alt"]})") << QString() << QString();
        QTest::newRow("click nothing") << u"click"_s << QJsonObject() << bad << QString();
        QTest::newRow("click half a point") << u"click"_s << obj(R"({"x":10})") << bad << QString();
        QTest::newRow("click two targets") << u"click"_s << obj(R"({"handle":"h1","text":"a"})") << bad << QString();
        QTest::newRow("click an empty text") << u"click"_s << obj(R"({"text":""})") << bad << QString();
        QTest::newRow("click exact alone") << u"click"_s << obj(R"({"x":1,"y":2,"exact":true})") << bad << QString();
        QTest::newRow("click a bad button")
                << u"click"_s << obj(R"({"x":1,"y":2,"button":"fourth"})") << bad << u"button"_s;
        QTest::newRow("click a bad modifier")
                << u"click"_s << obj(R"({"x":1,"y":2,"modifiers":"hyper"})") << bad << u"modifiers"_s;
        QTest::newRow("click a bad device")
                << u"click"_s << obj(R"({"x":1,"y":2,"device":"pen"})") << bad << u"device"_s;
        QTest::newRow("click far away") << u"click"_s << obj(R"({"x":1e7,"y":2})") << bad << u"x"_s;
        QTest::newRow("click a string x") << u"click"_s << obj(R"({"x":"10","y":2})") << bad << u"x"_s;
        QTest::newRow("long press") << u"longPress"_s << obj(R"({"text":"Żółw","ms":1200})") << QString() << QString();
        QTest::newRow("long press too short") << u"longPress"_s << obj(R"({"text":"a","ms":10})") << bad << u"ms"_s;
        QTest::newRow("double click") << u"doubleClick"_s << obj(R"({"objectName":"pageEditor"})") << QString()
                                      << QString();
        QTest::newRow("drag") << u"drag"_s << obj(R"({"from":{"handle":"h1"},"to":{"x":5,"y":6},"holdMs":300})")
                              << QString() << QString();
        QTest::newRow("drag without to") << u"drag"_s << obj(R"({"from":{"handle":"h1"}})") << bad << u"to"_s;
        QTest::newRow("drag to nothing") << u"drag"_s << obj(R"({"from":{"handle":"h1"},"to":{}})") << bad << u"to"_s;
        QTest::newRow("drag from a string")
                << u"drag"_s << obj(R"({"from":"h1","to":{"x":1,"y":1}})") << bad << u"from"_s;
        QTest::newRow("drag an unknown field")
                << u"drag"_s << obj(R"({"from":{"handle":"h1","z":1},"to":{"x":1,"y":1}})") << bad << u"from"_s;
        QTest::newRow("scroll the middle") << u"scroll"_s << obj(R"({"dy":300})") << QString() << QString();
        QTest::newRow("scroll a target") << u"scroll"_s << obj(R"({"type":"ListView","dy":-120.5,"dx":0})") << QString()
                                         << QString();
        QTest::newRow("scroll by nothing") << u"scroll"_s << obj(R"({"dy":0})") << bad << u"dy"_s;
        QTest::newRow("scroll without a distance") << u"scroll"_s << obj(R"({"text":"Żółw"})") << bad << u"dy"_s;
        QTest::newRow("type") << u"type"_s << obj(R"({"text":"Zażółć gęślą jaźń\n"})") << QString() << QString();
        QTest::newRow("type nothing") << u"type"_s << QJsonObject() << bad << u"text"_s;
        QTest::newRow("type too much") << u"type"_s << QJsonObject{{u"text"_s, QString(20001, u'ż')}} << bad
                                       << u"text"_s;
        QTest::newRow("key") << u"key"_s << obj(R"({"sequence":"Ctrl+K","count":2})") << QString() << QString();
        QTest::newRow("key blank") << u"key"_s << obj(R"({"sequence":"  "})") << bad << u"sequence"_s;
        QTest::newRow("key count 0") << u"key"_s << obj(R"({"sequence":"Down","count":0})") << bad << u"count"_s;
        QTest::newRow("wait a route") << u"waitFor"_s << obj(R"({"route":"tasks","ms":200})") << QString() << QString();
        QTest::newRow("wait an item gone")
                << u"waitFor"_s << obj(R"({"text":"Zapisano","gone":true})") << QString() << QString();
        QTest::newRow("wait idle and a route")
                << u"waitFor"_s << obj(R"({"idle":true,"route":{"view":"page"}})") << QString() << QString();
        QTest::newRow("wait for nothing") << u"waitFor"_s << obj(R"({"ms":100})") << bad << QString();
        QTest::newRow("wait idle false") << u"waitFor"_s << obj(R"({"idle":false})") << bad << QString();
        QTest::newRow("wait gone alone") << u"waitFor"_s << obj(R"({"route":"tasks","gone":true})") << bad << u"gone"_s;
        QTest::newRow("wait too long") << u"waitFor"_s << obj(R"({"idle":true,"ms":999999})") << bad << u"ms"_s;
        QTest::newRow("wait a handle and a text")
                << u"waitFor"_s << obj(R"({"handle":"h1","text":"a"})") << bad << QString();
        QTest::newRow("quit") << u"quit"_s << QJsonObject() << QString() << QString();
    }
    void checkRequests()
    {
        QFETCH(QString, method);
        QFETCH(QJsonObject, params);
        QFETCH(QString, error);
        QFETCH(QString, param);
        const auto problem = checkRequest(ControlRequest{QJsonValue(1), method, params});
        if (error.isEmpty()) {
            QVERIFY2(!problem, problem ? qPrintable(problem->message) : "");
            return;
        }
        QVERIFY(problem);
        QCOMPARE(problem->code, error);
        QCOMPARE(problem->id, QJsonValue(1));
        QVERIFY(!problem->message.isEmpty());
        if (!param.isEmpty())
            QCOMPARE(problem->details.value(u"param"_s).toString(), param);
        if (error == u"unknown_method")
            QVERIFY(problem->details.value(u"methods"_s).toArray().contains(u"state"_s));
    }

    void everyMethodIsDescribed()
    {
        QStringList names;
        for (const MethodSpec &m : controlMethods()) {
            QVERIFY(!names.contains(m.name));
            names.append(m.name);
            QVERIFY2(!m.summary.isEmpty(), qPrintable(m.name));
            QCOMPARE(findControlMethod(m.name), &m);
        }
        for (const char *expected :
             {"hello", "state", "navigate", "open", "commands", "command", "capture", "screenshot", "find", "click",
              "longPress", "doubleClick", "drag", "scroll", "type", "key", "waitFor", "quit"})
            QVERIFY2(names.contains(QLatin1String(expected)), expected);
        QVERIFY(!findControlMethod(u"nothing"_s));
    }

    void modifiersAndButtons_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<int>("modifiers");
        QTest::newRow("none") << QJsonValue() << true << 0;
        QTest::newRow("empty") << QJsonValue(u""_s) << true << 0;
        QTest::newRow("ctrl") << QJsonValue(u"ctrl"_s) << true << int(Qt::ControlModifier);
        QTest::newRow("Control+Shift") << QJsonValue(u"Control+Shift"_s) << true
                                       << int(Qt::ControlModifier | Qt::ShiftModifier);
        QTest::newRow("list") << QJsonValue(QJsonArray{u"alt"_s, u"META"_s}) << true
                              << int(Qt::AltModifier | Qt::MetaModifier);
        QTest::newRow("unknown") << QJsonValue(u"ctrl+hyper"_s) << false << 0;
        QTest::newRow("number in list") << QJsonValue(QJsonArray{1}) << false << 0;
        QTest::newRow("number") << QJsonValue(3) << false << 0;
    }
    void modifiersAndButtons()
    {
        QFETCH(QJsonValue, value);
        QFETCH(bool, valid);
        QFETCH(int, modifiers);
        const auto parsed = parseModifiers(value);
        QCOMPARE(parsed.has_value(), valid);
        if (valid)
            QCOMPARE(int(*parsed), modifiers);
        QCOMPARE(parseMouseButton(u""_s), std::optional<Qt::MouseButton>(Qt::LeftButton));
        QCOMPARE(parseMouseButton(u"Right"_s), std::optional<Qt::MouseButton>(Qt::RightButton));
        QCOMPARE(parseMouseButton(u"middle"_s), std::optional<Qt::MouseButton>(Qt::MiddleButton));
        QVERIFY(!parseMouseButton(u"back"_s));
    }

    void routes_data()
    {
        QTest::addColumn<QJsonObject>("route");
        QTest::addColumn<QJsonValue>("wanted");
        QTest::addColumn<bool>("matches");
        const QJsonObject page = obj(R"({"view":"page","path":"Projekty/Żółw.md","line":3})");
        QTest::newRow("view name") << page << QJsonValue(u"page"_s) << true;
        QTest::newRow("other view") << page << QJsonValue(u"tasks"_s) << false;
        QTest::newRow("home") << obj(R"({"view":"journals"})") << QJsonValue(u"home"_s) << true;
        QTest::newRow("path") << page << QJsonValue(obj(R"({"view":"page","path":"Projekty/Żółw.md"})")) << true;
        QTest::newRow("other path") << page << QJsonValue(obj(R"({"path":"Projekty/Zolw.md"})")) << false;
        QTest::newRow("line as a double") << page << QJsonValue(obj(R"({"line":3.0})")) << true;
        QTest::newRow("missing key") << page << QJsonValue(obj(R"({"blockId":"abc"})")) << false;
        QTest::newRow("empty object") << page << QJsonValue(QJsonObject()) << true;
        QTest::newRow("a number") << page << QJsonValue(4) << false;
    }
    void routes()
    {
        QFETCH(QJsonObject, route);
        QFETCH(QJsonValue, wanted);
        QFETCH(bool, matches);
        QCOMPARE(routeMatches(route, wanted), matches);
    }

    // ---- The socket ---------------------------------------------------------------------------

    void serverAndClient()
    {
        const QString k = key(u"brulion-test-control"_s);
        ServerThread server(k, [](const ControlRequest &request, ControlReply reply) {
            if (request.method == u"hello")
                reply.ok(QJsonObject{{u"protocol"_s, ControlProtocolVersion}});
            else if (request.method == u"type")
                reply.ok(QJsonObject{{u"echo"_s, request.params.value(u"text"_s)}});
            else
                reply.fail(QString::fromLatin1(ControlError::NotFound), u"Nie ma takiego elementu"_s);
        });
        QVERIFY2(server.startListening(), qPrintable(server.error));
        // Owner only: rw for the user, nothing for the group and others.
        struct stat st{};
        QCOMPARE(::stat(QFile::encodeName(k).constData(), &st), 0);
        QVERIFY(S_ISSOCK(st.st_mode));
        QCOMPARE(int(st.st_mode & 0777), 0600);

        ControlClient client(k);
        QVERIFY(client.connect());
        ControlClient::Response r = client.call(u"hello"_s);
        QVERIFY2(r.ok, qPrintable(r.code));
        QCOMPARE(r.result.toObject().value(u"protocol"_s).toInt(), ControlProtocolVersion);
        r = client.call(u"type"_s, {{u"text"_s, u"Zażółć gęślą jaźń"_s}});
        QVERIFY(r.ok);
        QCOMPARE(r.result.toObject().value(u"echo"_s).toString(), u"Zażółć gęślą jaźń"_s);
        r = client.call(u"find"_s, {{u"text"_s, u"Żółw"_s}});
        QVERIFY(!r.ok);
        QVERIFY(!r.transportError);
        QCOMPARE(r.code, u"not_found"_s);
        QCOMPARE(r.message, u"Nie ma takiego elementu"_s);
        // Refused before the handler: an unknown method, bad parameters.
        r = client.call(u"explode"_s);
        QCOMPARE(r.code, u"unknown_method"_s);
        r = client.call(u"click"_s, {{u"x"_s, 1}});
        QCOMPARE(r.code, u"bad_params"_s);
        QCOMPARE(r.details.value(u"param"_s).toString(), QString());

        // The server goes: the client sees the end, the socket file is gone.
        server.quit();
        server.wait();
        QVERIFY(!QFileInfo::exists(k));
        r = client.call(u"hello"_s, {}, 2000);
        QVERIFY(r.transportError);
        ControlClient late(k);
        QVERIFY(!late.connect(300));
    }

    void answersInAnyOrder()
    {
        // The server in this thread, raw clients that let the event loop run.
        const QString k = key(u"brulion-test-order"_s);
        ControlServer server;
        QList<ControlReply> later;
        server.setHandler([&later](const ControlRequest &request, ControlReply reply) {
            if (request.method == u"waitFor")
                later.append(reply); // answered after the next request
            else
                reply.ok(QJsonObject{{u"method"_s, request.method}});
        });
        QVERIFY2(server.listen(k), qPrintable(server.errorString()));
        QCOMPARE(server.socketPath(), k);
        QLocalSocket raw;
        raw.connectToServer(k);
        QVERIFY(raw.waitForConnected(1000));
        QJsonObject response = exchange(raw, "{nie json");
        QCOMPARE(response.value(u"ok"_s).toBool(true), false);
        QVERIFY(response.value(u"id"_s).isNull());
        QCOMPARE(response.value(u"error"_s).toObject().value(u"code"_s).toString(), u"parse_error"_s);
        response = exchange(raw, R"({"id":3,"method":"click","params":{"x":1}})");
        QCOMPARE(response.value(u"id"_s).toInt(), 3);
        QCOMPARE(response.value(u"error"_s).toObject().value(u"code"_s).toString(), u"bad_params"_s);
        raw.write(R"({"id":"w","method":"waitFor","params":{"idle":true}})"
                  "\n");
        raw.flush();
        QTRY_COMPARE(later.size(), 1);
        response = exchange(raw, R"({"id":"h","method":"hello"})");
        QCOMPARE(response.value(u"id"_s).toString(), u"h"_s);
        later.first().ok(QJsonObject{{u"waitedMs"_s, 5}});
        later.first().ok(QJsonObject{{u"twice"_s, true}}); // a second answer is dropped
        response = nextResponse(raw);
        QCOMPARE(response.value(u"id"_s).toString(), u"w"_s);
        QCOMPARE(response.value(u"result"_s).toObject().value(u"waitedMs"_s).toInt(), 5);
        QTest::qWait(100);
        QVERIFY(!raw.canReadLine());

        // Two requests in one write, answered in turn.
        raw.write(R"({"id":1,"method":"hello"})"
                  "\n"
                  R"({"id":2,"method":"state"})"
                  "\n");
        raw.flush();
        QCOMPARE(nextResponse(raw).value(u"id"_s).toInt(), 1);
        QCOMPARE(nextResponse(raw).value(u"result"_s).toObject().value(u"method"_s).toString(), u"state"_s);

        // A client that leaves before its answer: the answer goes nowhere, nothing breaks.
        later.clear();
        {
            QLocalSocket gone;
            gone.connectToServer(k);
            QVERIFY(gone.waitForConnected(1000));
            gone.write(R"({"id":1,"method":"waitFor","params":{"idle":true}})"
                       "\n");
            gone.flush();
            QTRY_COMPARE(later.size(), 1);
            gone.abort();
        }
        QTRY_VERIFY(!later.first().connected());
        later.first().ok(QJsonObject());
        QCOMPARE(exchange(raw, R"({"id":9,"method":"hello"})").value(u"id"_s).toInt(), 9);

        // close(): connections end, the socket file is gone.
        QVERIFY(server.isListening());
        server.close();
        QVERIFY(!server.isListening());
        QVERIFY(!QFileInfo::exists(k));
        QTRY_COMPARE(raw.state(), QLocalSocket::UnconnectedState);
    }

    void liveServerIsNotTakenOver()
    {
        const QString k = key(u"brulion-test-live"_s);
        ServerThread first(k, [](const ControlRequest &, ControlReply reply) { reply.ok(QJsonObject{{u"who"_s, 1}}); });
        QVERIFY(first.startListening());
        ControlServer second;
        QVERIFY(!second.listen(k)); // another Brulion run (--no-single-instance) keeps its socket
        QVERIFY(!second.errorString().isEmpty());
        ControlClient client(k);
        QVERIFY(client.connect());
        QCOMPARE(client.call(u"hello"_s).result.toObject().value(u"who"_s).toInt(), 1);
    }

    void staleSocketFileIsReplaced()
    {
        const QString k = key(u"brulion-test-stale"_s);
        {
            // A socket file left behind by a crashed run: bound, then closed without unlinking.
            const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            QVERIFY(fd >= 0);
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            const QByteArray path = QFile::encodeName(k);
            QVERIFY(size_t(path.size()) < sizeof(address.sun_path));
            std::memcpy(address.sun_path, path.constData(), size_t(path.size()));
            QCOMPARE(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
            ::close(fd);
        }
        QVERIFY(QFileInfo::exists(k));
        QVERIFY(!ControlClient(k).connect(300)); // nobody answers there
        ServerThread server(k, [](const ControlRequest &, ControlReply reply) { reply.ok(); });
        QVERIFY2(server.startListening(), qPrintable(server.error));
        ControlClient client(k);
        QVERIFY(client.connect());
        QVERIFY(client.call(u"hello"_s).ok);
    }

    void noHandlerMeansNotReady()
    {
        const QString k = key(u"brulion-test-nohandler"_s);
        ServerThread server(k, {});
        QVERIFY(server.startListening());
        ControlClient client(k);
        QVERIFY(client.connect());
        const ControlClient::Response r = client.call(u"hello"_s);
        QCOMPARE(r.code, u"not_ready"_s);
    }

    // The socket only in a folder no other user can write to: in a shared one (the temp folder,
    // where QLocalServer puts a bare name when there is no runtime folder) another user could
    // take the name first, answer the command line in the app's place and read what it sends.
    void onlyInAPrivateFolder()
    {
        // A bare name (no runtime folder): never the temp folder.
        const QString bare = u"brulion-test-bare-%1-control"_s.arg(QCoreApplication::applicationPid());
        ControlServer server;
        QVERIFY(!server.listen(bare));
        QVERIFY(!server.errorString().isEmpty());
        QVERIFY(!server.isListening());
        QVERIFY(!QFileInfo::exists(QDir::temp().filePath(bare)));

        // A folder the group or others can write to (the shared /tmp is 1777).
        const QString shared = key(u"wspólny"_s);
        QVERIFY(QDir().mkpath(shared));
        QCOMPARE(::chmod(QFile::encodeName(shared).constData(), 01777), 0);
        QVERIFY(!server.listen(shared + u"/brulion-test-control"_s));
        QVERIFY(!QFileInfo::exists(shared + u"/brulion-test-control"_s));
        QCOMPARE(::chmod(QFile::encodeName(shared).constData(), 0770), 0);
        QVERIFY(!server.listen(shared + u"/brulion-test-control"_s));

        // A symbolic link to a private folder: whoever owns the link decides where it points.
        const QString target = key(u"prywatny"_s);
        QVERIFY(QDir().mkpath(target));
        QCOMPARE(::chmod(QFile::encodeName(target).constData(), 0700), 0);
        QVERIFY(QFile::link(target, key(u"dowiązanie"_s)));
        QVERIFY(!server.listen(key(u"dowiązanie/brulion-test-control"_s)));

        // The private folder itself (also readable by the group: nobody else can add a file).
        QVERIFY2(server.listen(target + u"/brulion-test-control"_s), qPrintable(server.errorString()));
        server.close();
        QCOMPARE(::chmod(QFile::encodeName(target).constData(), 0750), 0);
        QVERIFY2(server.listen(target + u"/brulion-test-control"_s), qPrintable(server.errorString()));
        ControlClient client(target + u"/brulion-test-control"_s);
        QVERIFY(client.connect());
        QVERIFY(client.errorString().isEmpty());
        server.close();
    }
};

QTEST_GUILESS_MAIN(TestIpcControl)
#include "tst_ipc_control.moc"
