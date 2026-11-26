// The remote control protocol and socket (src/ipc: controlprotocol.h, controlserver.h,
// controlclient.h): request lines parsed and refused when malformed (not JSON, no object, no
// method, a bad id or params, unknown fields, too long), every method's parameters checked
// (unknown method, unknown parameter, wrong type, out of range, an unknown enum value, a missing
// one, targets of two kinds or half a point, conditions of waitFor), modifiers, buttons and
// route matching; and the socket end to end: owner-only permissions (0600), answers in any
// order (a later answer, a client gone meanwhile), Polish text through it, errors for bad lines,
// a live server never taken over, a stale socket file replaced, the file gone after close(). The
// live methods' parameters (eval, load, theme, inspect, setProperty, events …) and grants: a closed
// server answers `unauthorized` to everything before hello with a grant's token, a wrong token is
// refused also while it is open, a grant's client name sticks, closing drops the connections
// without one, a revoked grant drops its connections; a grant of the chat alone serves only the
// chat's methods (the handler learns its token), a grant of the webhooks only chatHook, which nobody
// else may call, a grant of the agent sharing only the methods a device needs (chatLive, chatAnswer
// … — never the schedules, secrets, notifications, the webhooks' or the app's); ControlClient's
// identity (setIdentity(), BRULION_APP_TOKEN / BRULION_APP_CLIENT) sent in a hello first; the
// asynchronous client of a server the app starts (asynccontrolclient.h): ready once its hello is
// accepted, calls multiplexed and answered in any order, never inside call(), the calls waiting failed
// when the link drops, a call without an answer in time, a refused token, connected again with a
// backoff once the server listens again, nothing sent before it is ready.
#include "asynccontrolclient.h"
#include "controlclient.h"
#include "controlprotocol.h"
#include "controlserver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <QElapsedTimer>

#include <cstdlib>
#include <cstring>
#include <functional>
#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;
using namespace brulion::ipc;

namespace {

/// Sets a variable as a program's environment carries it: in Unicode on Windows, where qputenv()'s
/// bytes are taken in the ANSI code page (which has no "Ż").
void putEnvironment(const char *name, const QString &value)
{
#ifdef Q_OS_WIN
    _wputenv_s(reinterpret_cast<const wchar_t *>(QString::fromLatin1(name).utf16()),
               reinterpret_cast<const wchar_t *>(value.utf16()));
#else
    qputenv(name, value.toUtf8());
#endif
}

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
    while (!socket.canReadLine() && t.elapsed() < 20000 * BRULION_TEST_SLOWDOWN)
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
    ServerThread(const QString &key, ControlServer::Handler handler, std::function<void(ControlServer &)> setup = {})
        : m_key(key), m_handler(std::move(handler)), m_setup(std::move(setup))
    { }
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
        if (m_setup)
            m_setup(server);
        m_listening = server.listen(m_key);
        error = server.errorString();
        m_ready.release();
        exec();
    }

private:
    QString m_key;
    ControlServer::Handler m_handler;
    std::function<void(ControlServer &)> m_setup;
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
        // Live changes.
        QTest::newRow("hello with a client and a token")
                << u"hello"_s << obj(R"({"client":"Claude Code","token":"ab12"})") << QString() << QString();
        QTest::newRow("hello a number token") << u"hello"_s << obj(R"({"token":12})") << bad << u"token"_s;
        QTest::newRow("eval") << u"eval"_s << obj(R"({"code":"return 1 + 1","extension":"nawyki"})") << QString()
                              << QString();
        QTest::newRow("eval nothing") << u"eval"_s << QJsonObject() << bad << u"code"_s;
        QTest::newRow("eval blank") << u"eval"_s << obj(R"({"code":"  
"})") << bad << u"code"_s;
        QTest::newRow("eval an extension number")
                << u"eval"_s << obj(R"({"code":"x","extension":1})") << bad << u"extension"_s;
        QTest::newRow("extensions") << u"extensions"_s << QJsonObject() << QString() << QString();
        QTest::newRow("extensions with a param") << u"extensions"_s << obj(R"({"all":1})") << bad << u"all"_s;
        QTest::newRow("load")
                << u"load"_s
                << obj(R"({"id":"zywy","source":"local n = 1","title":"Żywy","permissions":["write","app"]})")
                << QString() << QString();
        QTest::newRow("load permissions as text")
                << u"load"_s << obj(R"({"id":"a","source":"x = 1","permissions":"write, ai"})") << QString()
                << QString();
        QTest::newRow("load permissions of numbers")
                << u"load"_s << obj(R"({"id":"a","source":"x = 1","permissions":[1]})") << bad << u"permissions"_s;
        QTest::newRow("load without source") << u"load"_s << obj(R"({"id":"a"})") << bad << u"source"_s;
        QTest::newRow("load blank source") << u"load"_s << obj(R"({"id":"a","source":" "})") << bad << u"source"_s;
        QTest::newRow("load without id") << u"load"_s << obj(R"({"source":"x = 1"})") << bad << u"id"_s;
        QTest::newRow("unload") << u"unload"_s << obj(R"({"id":"a"})") << QString() << QString();
        QTest::newRow("unload nothing") << u"unload"_s << QJsonObject() << bad << u"id"_s;
        QTest::newRow("reload") << u"reload"_s << QJsonObject() << QString() << QString();
        QTest::newRow("console") << u"console"_s << obj(R"({"since":12,"limit":50})") << QString() << QString();
        QTest::newRow("console limit 0") << u"console"_s << obj(R"({"limit":0})") << bad << u"limit"_s;
        QTest::newRow("console since -1") << u"console"_s << obj(R"({"since":-1})") << bad << u"since"_s;
        QTest::newRow("settings") << u"settings"_s << QJsonObject() << QString() << QString();
        QTest::newRow("set a word") << u"set"_s << obj(R"({"key":"density","value":"compact"})") << QString()
                                    << QString();
        QTest::newRow("set a list") << u"set"_s << obj(R"({"key":"bottomNav","value":["a","b"]})") << QString()
                                    << QString();
        QTest::newRow("set null") << u"set"_s << obj(R"({"key":"uiFont","value":null})") << QString() << QString();
        QTest::newRow("set without a value") << u"set"_s << obj(R"({"key":"density"})") << bad << u"value"_s;
        QTest::newRow("theme tokens") << u"theme"_s
                                      << QJsonObject{{u"tokens"_s,
                                                      QJsonObject{{u"accent"_s, u"#3a7"_s},
                                                                  {u"dark"_s, QJsonObject{{u"bg"_s, u"#0b0d10"_s}}}}},
                                                     {u"catalog"_s, true}}
                                      << QString() << QString();
        QTest::newRow("theme nothing") << u"theme"_s << QJsonObject() << QString() << QString();
        QTest::newRow("theme tokens as text")
                << u"theme"_s << obj(R"({"tokens":"accent=green"})") << bad << u"tokens"_s;
        QTest::newRow("theme tokens and clear")
                << u"theme"_s << obj(R"({"tokens":{},"clear":true})") << bad << u"clear"_s;
        QTest::newRow("contributions") << u"contributions"_s << obj(R"({"slot":"toolbar"})") << QString() << QString();
        QTest::newRow("inspect") << u"inspect"_s << obj(R"({"handle":"h2","depth":8,"properties":true,"limit":2000})")
                                 << QString() << QString();
        QTest::newRow("inspect too deep") << u"inspect"_s << obj(R"({"depth":9})") << bad << u"depth"_s;
        QTest::newRow("inspect too many") << u"inspect"_s << obj(R"({"limit":2001})") << bad << u"limit"_s;
        QTest::newRow("set a property") << u"setProperty"_s
                                        << obj(R"({"handle":"h2","name":"font.pixelSize","value":18})") << QString()
                                        << QString();
        QTest::newRow("set a property without a value")
                << u"setProperty"_s << obj(R"({"handle":"h2","name":"color"})") << bad << u"value"_s;
        QTest::newRow("set a blank property")
                << u"setProperty"_s << obj(R"({"handle":"h2","name":" ","value":1})") << bad << u"name"_s;
        QTest::newRow("events") << u"events"_s << obj(R"({"since":40,"wait":30000})") << QString() << QString();
        QTest::newRow("events waiting too long") << u"events"_s << obj(R"({"wait":30001})") << bad << u"wait"_s;
        QTest::newRow("changes") << u"changes"_s << QJsonObject() << QString() << QString();
        QTest::newRow("revert one") << u"revert"_s << obj(R"({"who":"Claude Code"})") << QString() << QString();
        QTest::newRow("revert a number") << u"revert"_s << obj(R"({"who":3})") << bad << u"who"_s;
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
             {"hello",      "state",       "navigate", "open",      "commands",    "command",    "capture",
              "screenshot", "find",        "click",    "longPress", "doubleClick", "drag",       "scroll",
              "type",       "key",         "waitFor",  "quit",      "eval",        "extensions", "load",
              "unload",     "reload",      "console",  "settings",  "set",         "theme",      "contributions",
              "inspect",    "setProperty", "events",   "changes",   "revert"})
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
#ifdef Q_OS_UNIX
        // Owner only: rw for the user, nothing for the group and others. (Windows: a named pipe,
        // no file; QLocalServer::UserAccessOption gives it an access list of this user alone.)
        struct stat st{};
        QCOMPARE(::stat(QFile::encodeName(k).constData(), &st), 0);
        QVERIFY(S_ISSOCK(st.st_mode));
        QCOMPARE(int(st.st_mode & 0777), 0600);
#endif

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
#ifdef Q_OS_WIN
        QCOMPARE(server.socketPath(), u"\\\\.\\pipe\\"_s + k);
#else
        QCOMPARE(server.socketPath(), k);
#endif
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
#ifndef Q_OS_UNIX
        QSKIP("A socket file left behind is a Unix thing: a named pipe goes with its last handle");
#else
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
#endif
    }

    void clientNamesAndLists()
    {
        QCOMPARE(cleanClientName(u"  Claude\n Code\u200b "_s), u"Claude Code"_s);
        QCOMPARE(cleanClientName(u"Żółw\tbłotny"_s), u"Żółw błotny"_s);
        QCOMPARE(cleanClientName(QString(100, u'ż')).size(), MaxClientName);
        QCOMPARE(cleanClientName(u"\x01\x02"_s), QString());
        QCOMPARE(stringList(QJsonValue(u"write, ai,,  app "_s)), (QStringList{u"write"_s, u"ai"_s, u"app"_s}));
        QCOMPARE(stringList(QJsonArray{u" write"_s, u""_s, u"app"_s}), (QStringList{u"write"_s, u"app"_s}));
        QCOMPARE(stringList(QJsonValue()), QStringList());
    }

    // A server only for grants (the remote control's setting off, an AI agent the app started let
    // change it): raw connections in this thread.
    void grants()
    {
        const QString k = key(u"brulion-test-grants"_s);
        ControlServer server;
        int handled = 0;
        server.setHandler([&handled](const ControlRequest &request, ControlReply reply) {
            ++handled;
            reply.ok(QJsonObject{
                    {u"method"_s, request.method}, {u"client"_s, request.client}, {u"granted"_s, request.granted}});
        });
        QVERIFY(server.listen(k));
        server.setOpen(false);
        QVERIFY(!server.isOpen());
        const QString token = server.addGrant(u"Claude\nCode"_s);
        static const QRegularExpression hex(u"^[0-9a-f]{64}$"_s);
        QVERIFY2(hex.match(token).hasMatch(), qPrintable(token));
        const QString other = server.addGrant(u"Codex"_s);
        QVERIFY(other != token);
        QCOMPARE(server.grantCount(), 2);
        const auto code = [](const QJsonObject &response) {
            return response.value(u"error"_s).toObject().value(u"code"_s).toString();
        };
        const auto result = [](const QJsonObject &response) { return response.value(u"result"_s).toObject(); };

        // Nothing before hello with a grant's token: not even an unknown method's list.
        QLocalSocket stranger;
        stranger.connectToServer(k);
        QVERIFY(stranger.waitForConnected(1000));
        QCOMPARE(code(exchange(stranger, R"({"id":1,"method":"state"})")), u"unauthorized"_s);
        QCOMPARE(code(exchange(stranger, R"({"id":2,"method":"hello"})")), u"unauthorized"_s);
        QCOMPARE(code(exchange(stranger, R"({"id":3,"method":"hello","params":{"client":"Ja"}})")), u"unauthorized"_s);
        QCOMPARE(code(exchange(stranger, R"({"id":4,"method":"explode"})")), u"unauthorized"_s);
        const QJsonObject wrong = exchange(stranger, R"({"id":5,"method":"hello","params":{"token":"00ff"}})");
        QCOMPARE(code(wrong), u"unauthorized"_s);
        QCOMPARE(wrong.value(u"id"_s).toInt(), 5);
        // (A token of the right length but not a grant's: refused too.)
        QCOMPARE(code(exchange(stranger,
                               QJsonDocument(QJsonObject{{u"id"_s, 6},
                                                         {u"method"_s, u"hello"_s},
                                                         {u"params"_s, QJsonObject{{u"token"_s, QString(64, u'a')}}}})
                                       .toJson(QJsonDocument::Compact))),
                 u"unauthorized"_s);
        QCOMPARE(handled, 0);

        // A grant's token: its client from now on (hello's own name does not replace it).
        QLocalSocket agent;
        agent.connectToServer(k);
        QVERIFY(agent.waitForConnected(1000));
        const QByteArray hello =
                QJsonDocument(QJsonObject{{u"id"_s, 1},
                                          {u"method"_s, u"hello"_s},
                                          {u"params"_s, QJsonObject{{u"token"_s, token}, {u"client"_s, u"Inny"_s}}}})
                        .toJson(QJsonDocument::Compact);
        QJsonObject r = exchange(agent, hello);
        QVERIFY2(r.value(u"ok"_s).toBool(), QJsonDocument(r).toJson().constData());
        QCOMPARE(result(r).value(u"client"_s).toString(), u"Claude Code"_s);
        QVERIFY(result(r).value(u"granted"_s).toBool());
        r = exchange(agent, R"({"id":2,"method":"hello","params":{"client":"Ktoś"}})");
        QCOMPARE(result(r).value(u"client"_s).toString(), u"Claude Code"_s);
        r = exchange(agent, R"({"id":3,"method":"eval","params":{"code":"return 1"}})");
        QCOMPARE(result(r).value(u"method"_s).toString(), u"eval"_s);
        QCOMPARE(result(r).value(u"client"_s).toString(), u"Claude Code"_s);
        QCOMPARE(code(exchange(agent, R"({"id":4,"method":"explode"})")), u"unknown_method"_s);

        // Open (the setting on): a connection without a token is served and may name itself; a
        // wrong token is still refused.
        server.setOpen(true);
        QLocalSocket script;
        script.connectToServer(k);
        QVERIFY(script.waitForConnected(1000));
        r = exchange(script, R"({"id":1,"method":"state"})");
        QCOMPARE(result(r).value(u"client"_s).toString(), QString());
        QVERIFY(!result(r).value(u"granted"_s).toBool());
        r = exchange(script, R"({"id":2,"method":"hello","params":{"client":"  Skrypt\u0007 "}})");
        QCOMPARE(result(r).value(u"client"_s).toString(), u"Skrypt"_s);
        r = exchange(script, R"({"id":3,"method":"state"})");
        QCOMPARE(result(r).value(u"client"_s).toString(), u"Skrypt"_s);
        QCOMPARE(code(exchange(script, R"({"id":4,"method":"hello","params":{"token":"zły"}})")), u"unauthorized"_s);

        // Closed again: the connections without a grant end, the agent's goes on.
        server.setOpen(false);
        QTRY_COMPARE(script.state(), QLocalSocket::UnconnectedState);
        QTRY_COMPARE(stranger.state(), QLocalSocket::UnconnectedState);
        r = exchange(agent, R"({"id":5,"method":"state"})");
        QVERIFY(r.value(u"ok"_s).toBool());

        // Revoked (the agent stopped): its connections end, its token is no grant any more.
        QVERIFY(server.revokeGrant(token));
        QVERIFY(!server.revokeGrant(token));
        QCOMPARE(server.grantCount(), 1);
        QTRY_COMPARE(agent.state(), QLocalSocket::UnconnectedState);
        QLocalSocket again;
        again.connectToServer(k);
        QVERIFY(again.waitForConnected(1000));
        QCOMPARE(code(exchange(again, hello)), u"unauthorized"_s);

        // Grants outlive close() and listen().
        server.close();
        QCOMPARE(server.grantCount(), 1);
        QVERIFY(server.listen(k));
        QLocalSocket codex;
        codex.connectToServer(k);
        QVERIFY(codex.waitForConnected(1000));
        r = exchange(codex, QJsonDocument(QJsonObject{{u"id"_s, 1},
                                                      {u"method"_s, u"hello"_s},
                                                      {u"params"_s, QJsonObject{{u"token"_s, other}}}})
                                    .toJson(QJsonDocument::Compact));
        QCOMPARE(result(r).value(u"client"_s).toString(), u"Codex"_s);
        server.close();
    }

    // A grant of the chat alone (every agent the app starts with Brulion's tools, a conversation's):
    // hello and the chat's methods, nothing else — whether the server is open or not; the handler
    // learns the token and that it is such a grant.
    void chatGrants()
    {
        const QString k = key(u"brulion-test-chatgrants"_s);
        ControlServer server;
        server.setHandler([](const ControlRequest &request, ControlReply reply) {
            reply.ok(QJsonObject{{u"method"_s, request.method},
                                 {u"client"_s, request.client},
                                 {u"grant"_s, request.grant},
                                 {u"chatOnly"_s, request.chatOnly}});
        });
        QVERIFY(server.listen(k));
        server.setOpen(false);
        const QString chat = server.addGrant(u"Claude Code"_s, GrantScope::Chat);
        const QString full = server.addGrant(u"Claude Code"_s);
        const auto code = [](const QJsonObject &response) {
            return response.value(u"error"_s).toObject().value(u"code"_s).toString();
        };
        const auto result = [](const QJsonObject &response) { return response.value(u"result"_s).toObject(); };
        const auto hello = [](const QString &token) {
            return QJsonDocument(QJsonObject{{u"id"_s, 1},
                                             {u"method"_s, u"hello"_s},
                                             {u"params"_s, QJsonObject{{u"token"_s, token}}}})
                    .toJson(QJsonDocument::Compact);
        };
        for (const bool open : {false, true}) {
            server.setOpen(open);
            QLocalSocket agent;
            agent.connectToServer(k);
            QVERIFY(agent.waitForConnected(1000));
            QJsonObject r = exchange(agent, hello(chat));
            QVERIFY(r.value(u"ok"_s).toBool());
            QVERIFY(result(r).value(u"chatOnly"_s).toBool());
            r = exchange(agent, R"({"id":2,"method":"chatStart","params":{"text":"Zapytaj Codexa o zdanie"}})");
            QVERIFY2(r.value(u"ok"_s).toBool(), QJsonDocument(r).toJson().constData());
            QCOMPARE(result(r).value(u"grant"_s).toString(), chat);
            QCOMPARE(result(r).value(u"client"_s).toString(), u"Claude Code"_s);
            QVERIFY(result(r).value(u"chatOnly"_s).toBool());
            for (const char *line : {R"({"id":3,"method":"eval","params":{"code":"return 1"}})",
                                     R"({"id":4,"method":"state"})",
                                     R"({"id":5,"method":"click","params":{"x":1,"y":2}})"}) {
                r = exchange(agent, line);
                QCOMPARE(code(r), u"unauthorized"_s);
                QVERIFY(r.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"chat"_s));
            }
            // (Its requests are checked as anyone's.)
            QCOMPARE(code(exchange(agent, R"({"id":6,"method":"explode"})")), u"unknown_method"_s);
            QCOMPARE(code(exchange(agent, R"({"id":7,"method":"chatWait","params":{"path":"AI/a.md","ms":-1}})")),
                     u"bad_params"_s);
            r = exchange(agent, R"({"id":8,"method":"chatSchedules"})");
            QVERIFY(r.value(u"ok"_s).toBool());
            r = exchange(agent, R"({"id":9,"method":"chatNotify","params":{"text":"Gotowe","title":"Budowanie"}})");
            QVERIFY2(r.value(u"ok"_s).toBool(), QJsonDocument(r).toJson().constData());
        }
        // The full grant: everything, not chat-only.
        QLocalSocket other;
        other.connectToServer(k);
        QVERIFY(other.waitForConnected(1000));
        QVERIFY(exchange(other, hello(full)).value(u"ok"_s).toBool());
        QJsonObject r = exchange(other, R"({"id":2,"method":"state"})");
        QVERIFY(r.value(u"ok"_s).toBool());
        QVERIFY(!result(r).value(u"chatOnly"_s).toBool());
        QCOMPARE(result(r).value(u"grant"_s).toString(), full);
        // Without a grant (open): no token, not chat-only.
        QLocalSocket script;
        script.connectToServer(k);
        QVERIFY(script.waitForConnected(1000));
        r = exchange(script, R"({"id":1,"method":"chats"})");
        QCOMPARE(result(r).value(u"grant"_s).toString(), QString());
        QVERIFY(!result(r).value(u"chatOnly"_s).toBool());
        // The chat's methods, as the protocol lists them.
        QStringList chatMethods;
        for (const MethodSpec &m : controlMethods()) {
            if (m.chat)
                chatMethods << m.name;
        }
        QCOMPARE(chatMethods, (QStringList{u"chats"_s, u"chatRead"_s, u"chatStart"_s, u"chatSend"_s, u"chatWait"_s,
                                           u"chatStop"_s, u"chatPartners"_s, u"chatLive"_s, u"chatSchedules"_s,
                                           u"chatScheduleAdd"_s, u"chatScheduleSet"_s, u"chatScheduleRemove"_s,
                                           u"chatScheduleRun"_s, u"chatRequestSecret"_s, u"chatNotify"_s}));
        // A secret asked for: its parameters checked before the app sees them.
        r = exchange(script,
                     R"({"id":2,"method":"chatRequestSecret","params":{"label":"Token GitHuba","delivery":"value"}})");
        QVERIFY2(r.value(u"ok"_s).toBool(), QJsonDocument(r).toJson().constData());
        QCOMPARE(code(exchange(script, R"({"id":3,"method":"chatRequestSecret","params":{"reason":"bez etykiety"}})")),
                 u"bad_params"_s);
        QCOMPARE(code(exchange(script,
                               R"({"id":4,"method":"chatRequestSecret","params":{"label":"x","delivery":"mail"}})")),
                 u"bad_params"_s);
        QCOMPARE(code(exchange(script, R"({"id":5,"method":"chatRequestSecret","params":{"label":"x","ms":900001}})")),
                 u"bad_params"_s);
        // A notification: its text is needed, its fields are strings.
        QCOMPARE(code(exchange(script, R"({"id":6,"method":"chatNotify","params":{"title":"Gotowe"}})")),
                 u"bad_params"_s);
        QCOMPARE(code(exchange(script, R"({"id":7,"method":"chatNotify","params":{"text":"Już","page":3}})")),
                 u"bad_params"_s);
        r = exchange(script, R"({"id":8,"method":"chatNotify","params":{"text":"Zbudowane — żółwie są szybkie"}})");
        QVERIFY2(r.value(u"ok"_s).toBool(), QJsonDocument(r).toJson().constData());
        server.close();
    }

    // A grant of the webhooks alone (the app's `brulion-cli app hooks serve`): hello and chatHook,
    // nothing else — and chatHook for nobody else, not a full grant, a chat grant or an open socket's
    // script; the handler learns that it is such a grant.
    void hookGrants()
    {
        const QString k = key(u"brulion-test-hookgrants"_s);
        ControlServer server;
        server.setHandler([](const ControlRequest &request, ControlReply reply) {
            reply.ok(QJsonObject{{u"method"_s, request.method},
                                 {u"hookOnly"_s, request.hookOnly},
                                 {u"chatOnly"_s, request.chatOnly},
                                 {u"granted"_s, request.granted}});
        });
        QVERIFY(server.listen(k));
        const QString hooks = server.addGrant(u"webhooks"_s, GrantScope::Hook);
        const QString chat = server.addGrant(u"Claude Code"_s, GrantScope::Chat);
        const QString full = server.addGrant(u"Claude Code"_s);
        const auto code = [](const QJsonObject &response) {
            return response.value(u"error"_s).toObject().value(u"code"_s).toString();
        };
        const auto hello = [](const QString &token) {
            return QJsonDocument(QJsonObject{{u"id"_s, 1},
                                             {u"method"_s, u"hello"_s},
                                             {u"params"_s, QJsonObject{{u"token"_s, token}}}})
                    .toJson(QJsonDocument::Compact);
        };
        const QByteArray hook = QJsonDocument(QJsonObject{{u"id"_s, 2},
                                                          {u"method"_s, u"chatHook"_s},
                                                          {u"params"_s, QJsonObject{{u"token"_s, QString(43, u'a')},
                                                                                    {u"text"_s, u"{\"a\": 1}"_s},
                                                                                    {u"kind"_s, u"json"_s},
                                                                                    {u"bytes"_s, 8},
                                                                                    {u"cut"_s, false}}}})
                                        .toJson(QJsonDocument::Compact);
        for (const bool open : {false, true}) {
            server.setOpen(open);
            QLocalSocket hookServer;
            hookServer.connectToServer(k);
            QVERIFY(hookServer.waitForConnected(1000));
            QJsonObject r = exchange(hookServer, hello(hooks));
            QVERIFY(r.value(u"ok"_s).toBool());
            QVERIFY(r.value(u"result"_s).toObject().value(u"hookOnly"_s).toBool());
            r = exchange(hookServer, hook);
            QVERIFY2(r.value(u"ok"_s).toBool(), QJsonDocument(r).toJson().constData());
            QVERIFY(r.value(u"result"_s).toObject().value(u"hookOnly"_s).toBool());
            QVERIFY(!r.value(u"result"_s).toObject().value(u"chatOnly"_s).toBool());
            for (const char *line : {R"({"id":3,"method":"chats"})", R"({"id":4,"method":"state"})",
                                     R"({"id":5,"method":"eval","params":{"code":"return 1"}})",
                                     R"({"id":6,"method":"chatScheduleRun","params":{"id":"s1"}})"}) {
                r = exchange(hookServer, line);
                QCOMPARE(code(r), u"unauthorized"_s);
                QVERIFY(r.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"chatHook"_s));
            }
            QCOMPARE(code(exchange(hookServer, R"({"id":7,"method":"chatHook","params":{}})")), u"bad_params"_s);
            QCOMPARE(code(exchange(hookServer, R"({"id":8,"method":"chatHook","params":{"token":"t","kind":"xml"}})")),
                     u"bad_params"_s);
            // Nobody else calls it: a chat grant, a full grant.
            for (const QString &token : {chat, full}) {
                QLocalSocket other;
                other.connectToServer(k);
                QVERIFY(other.waitForConnected(1000));
                QVERIFY(exchange(other, hello(token)).value(u"ok"_s).toBool());
                QCOMPARE(code(exchange(other, hook)), u"unauthorized"_s);
            }
        }
        // Nor a script of the open socket.
        server.setOpen(true);
        QLocalSocket script;
        script.connectToServer(k);
        QVERIFY(script.waitForConnected(1000));
        const QJsonObject r = exchange(script, hook);
        QCOMPARE(code(r), u"unauthorized"_s);
        QVERIFY(r.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"webhook"_s));
        QStringList hookMethods;
        for (const MethodSpec &m : controlMethods()) {
            if (m.hook)
                hookMethods << m.name;
            QVERIFY2(!(m.hook && m.chat), qPrintable(m.name));
        }
        QCOMPARE(hookMethods, QStringList{u"chatHook"_s});
        server.close();
    }

    // A grant of the agent sharing (the app's `brulion-cli acp serve`, for the user's phone): hello and
    // the methods marked share — reading, starting, sending, stopping, waiting, chatLive, chatAnswer —,
    // nothing else (no schedules, secrets, notifications, webhooks, live changes); chatAnswer is no
    // method of an agent's chat grant; the handler learns that it is such a grant.
    void shareGrants()
    {
        const QString k = key(u"brulion-test-sharegrants"_s);
        ControlServer server;
        server.setHandler([](const ControlRequest &request, ControlReply reply) {
            reply.ok(QJsonObject{{u"method"_s, request.method},
                                 {u"shareOnly"_s, request.shareOnly},
                                 {u"chatOnly"_s, request.chatOnly},
                                 {u"granted"_s, request.granted},
                                 {u"client"_s, request.client}});
        });
        QVERIFY(server.listen(k));
        const QString share = server.addGrant(u"agent sharing"_s, GrantScope::Share);
        const QString chat = server.addGrant(u"Claude Code"_s, GrantScope::Chat);
        const auto code = [](const QJsonObject &response) {
            return response.value(u"error"_s).toObject().value(u"code"_s).toString();
        };
        const auto result = [](const QJsonObject &response) { return response.value(u"result"_s).toObject(); };
        const auto hello = [](const QString &token) {
            return QJsonDocument(QJsonObject{{u"id"_s, 1},
                                             {u"method"_s, u"hello"_s},
                                             {u"params"_s, QJsonObject{{u"token"_s, token}}}})
                    .toJson(QJsonDocument::Compact);
        };
        for (const bool open : {false, true}) {
            server.setOpen(open);
            QLocalSocket sharing;
            sharing.connectToServer(k);
            QVERIFY(sharing.waitForConnected(1000));
            QJsonObject r = exchange(sharing, hello(share));
            QVERIFY(r.value(u"ok"_s).toBool());
            QVERIFY(result(r).value(u"shareOnly"_s).toBool());
            for (const char *line :
                 {R"({"id":2,"method":"chats"})", R"({"id":3,"method":"chatRead","params":{"path":"AI/a.md"}})",
                  R"({"id":4,"method":"chatLive","params":{"since":-1,"timeout":0,"path":"AI/Żółw.md"}})",
                  R"({"id":5,"method":"chatSend","params":{"path":"AI/a.md","text":"Dalej","mode":"steer"}})",
                  R"({"id":6,"method":"chatStart","params":{"text":"Popraw testy","project":"p1","worktree":"/w"}})",
                  R"({"id":7,"method":"chatStop","params":{"path":"AI/a.md"}})",
                  R"({"id":8,"method":"chatWait","params":{"path":"AI/a.md","ms":0}})",
                  R"({"id":9,"method":"chatPartners"})",
                  R"({"id":10,"method":"chatAnswer","params":{"path":"AI/a.md","option":"Zezwól"}})"}) {
                r = exchange(sharing, line);
                QVERIFY2(r.value(u"ok"_s).toBool(), line);
                QVERIFY(result(r).value(u"shareOnly"_s).toBool());
                QVERIFY(result(r).value(u"granted"_s).toBool());
                QVERIFY(!result(r).value(u"chatOnly"_s).toBool());
                QCOMPARE(result(r).value(u"client"_s).toString(), u"agent sharing"_s);
            }
            for (const char *line :
                 {R"({"id":11,"method":"chatSchedules"})",
                  R"({"id":12,"method":"chatScheduleRun","params":{"id":"s1"}})",
                  R"({"id":13,"method":"chatRequestSecret","params":{"label":"PIN"}})",
                  R"({"id":14,"method":"chatNotify","params":{"text":"Gotowe"}})", R"({"id":15,"method":"state"})",
                  R"({"id":16,"method":"eval","params":{"code":"return 1"}})", R"({"id":17,"method":"quit"})"}) {
                r = exchange(sharing, line);
                QCOMPARE(code(r), u"unauthorized"_s);
                QVERIFY(r.value(u"error"_s).toObject().value(u"message"_s).toString().contains(u"agent sharing"_s));
            }
            QCOMPARE(code(exchange(sharing, R"({"id":18,"method":"chatHook","params":{"token":"t"}})")),
                     u"unauthorized"_s);
            // (Its requests are checked as anyone's.)
            QCOMPARE(code(exchange(sharing, R"({"id":19,"method":"chatLive","params":{"timeout":60001}})")),
                     u"bad_params"_s);
            QCOMPARE(code(exchange(sharing, R"({"id":20,"method":"chatLive","params":{"since":-2}})")),
                     u"bad_params"_s);
            QCOMPARE(code(exchange(sharing,
                                   R"({"id":21,"method":"chatSend","params":{"path":"a","text":"x","mode":"teraz"}})")),
                     u"bad_params"_s);
            QCOMPARE(code(exchange(sharing, R"({"id":22,"method":"chatAnswer","params":{"path":"AI/a.md"}})")),
                     u"bad_params"_s);
        }
        // An agent's chat grant reads chatLive, but never answers the user's questions.
        QLocalSocket agent;
        agent.connectToServer(k);
        QVERIFY(agent.waitForConnected(1000));
        QVERIFY(exchange(agent, hello(chat)).value(u"ok"_s).toBool());
        QVERIFY(exchange(agent, R"({"id":2,"method":"chatLive"})").value(u"ok"_s).toBool());
        QCOMPARE(code(exchange(agent, R"({"id":3,"method":"chatAnswer","params":{"path":"AI/a.md","option":"Tak"}})")),
                 u"unauthorized"_s);
        // The agent sharing's methods, as the protocol lists them.
        QStringList shareMethods;
        for (const MethodSpec &m : controlMethods()) {
            if (m.share)
                shareMethods << m.name;
            QVERIFY2(!(m.share && m.hook), qPrintable(m.name));
        }
        QCOMPARE(shareMethods, (QStringList{u"chats"_s, u"chatRead"_s, u"chatStart"_s, u"chatSend"_s, u"chatWait"_s,
                                            u"chatStop"_s, u"chatPartners"_s, u"chatLive"_s, u"chatAnswer"_s}));
        // Revoked: its connection dropped, a new hello refused.
        QLocalSocket again;
        again.connectToServer(k);
        QVERIFY(again.waitForConnected(1000));
        QVERIFY(exchange(again, hello(share)).value(u"ok"_s).toBool());
        QVERIFY(server.revokeGrant(share));
        QTRY_VERIFY(again.state() != QLocalSocket::ConnectedState);
        QLocalSocket late;
        late.connectToServer(k);
        QVERIFY(late.waitForConnected(1000));
        QCOMPARE(code(exchange(late, hello(share))), u"unauthorized"_s);
        server.close();
    }

    // The asynchronous client of a server the app starts (acp serve's agent sharing): connected again
    // and again until the app listens, ready once its hello with the token is accepted; calls
    // multiplexed (answered in any order), answered never inside call(); a call without an answer
    // in time; every call waiting failed when the link drops; nothing goes out while it is down; a
    // refused token keeps it down; the link back once the server listens again.
    void asyncClient()
    {
        const QString k = key(u"brulion-test-async"_s);
        auto server = std::make_unique<ControlServer>();
        server->setOpen(false);
        QList<ControlReply> held;
        QStringList heard;
        const auto handler = [&held, &heard](const ControlRequest &request, ControlReply reply) {
            if (request.method == u"hello")
                return reply.ok(QJsonObject{{u"protocol"_s, 1}, {u"granted"_s, request.granted}});
            heard << request.method + u' ' + request.params.value(u"text"_s).toString();
            if (request.method == u"chatWait")
                return held.append(reply); // (answered later, or never)
            if (request.method == u"chatStop")
                return reply.fail(u"not_found"_s, u"Nie ma takiej rozmowy"_s, {{u"param"_s, u"path"_s}});
            reply.ok(QJsonObject{{u"echo"_s, request.params.value(u"text"_s)}, {u"shareOnly"_s, request.shareOnly}});
        };
        server->setHandler(handler);
        const QString token = server->addGrant(u"agent sharing"_s, GrantScope::Share);

        // Nothing listens yet: down, a call fails at once (later, from the event loop).
        AsyncControlClient client(k, token, u"agent sharing"_s);
        client.setRetryDelays({50, 100});
        QSignalSpy ready(&client, &AsyncControlClient::readyChanged);
        client.start();
        QVERIFY(!client.isReady());
        bool answered = false;
        AsyncControlClient::Response early;
        client.call(u"chats"_s, {}, 1000, [&](const AsyncControlClient::Response &r) {
            early = r;
            answered = true;
        });
        QVERIFY(!answered); // (never inside call())
        QTRY_VERIFY(answered);
        QVERIFY(early.transportError);
        QCOMPARE(early.code, u"disconnected"_s);
        QVERIFY(heard.isEmpty());

        // The app listens: the link comes up by itself.
        QVERIFY(server->listen(k));
        QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 10000 * BRULION_TEST_SLOWDOWN);
        QCOMPARE(ready.count(), 1);
        QVERIFY(client.hello().value(u"granted"_s).toBool());

        // Multiplexed: a held call does not keep the next one waiting.
        QList<std::pair<QString, AsyncControlClient::Response>> answers;
        const auto note = [&answers](const QString &name) {
            return [&answers, name](const AsyncControlClient::Response &r) { answers.append({name, r}); };
        };
        client.call(u"chatWait"_s, {{u"path"_s, u"AI/a.md"_s}}, 30000, note(u"wait"_s));
        client.call(u"chatSend"_s, {{u"path"_s, u"AI/a.md"_s}, {u"text"_s, u"Zażółć gęślą jaźń"_s}}, 30000,
                    note(u"send"_s));
        client.call(u"chatStop"_s, {{u"path"_s, u"AI/b.md"_s}}, 30000, note(u"stop"_s));
        QTRY_COMPARE(answers.size(), 2);
        QCOMPARE(answers.at(0).first, u"send"_s);
        QVERIFY(answers.at(0).second.ok);
        QCOMPARE(answers.at(0).second.result.toObject().value(u"echo"_s).toString(), u"Zażółć gęślą jaźń"_s);
        QVERIFY(answers.at(0).second.result.toObject().value(u"shareOnly"_s).toBool());
        QCOMPARE(answers.at(1).first, u"stop"_s);
        QVERIFY(!answers.at(1).second.ok);
        QVERIFY(!answers.at(1).second.transportError);
        QCOMPARE(answers.at(1).second.code, u"not_found"_s);
        QCOMPARE(answers.at(1).second.message, u"Nie ma takiej rozmowy"_s);
        QCOMPARE(answers.at(1).second.details.value(u"param"_s).toString(), u"path"_s);
        QCOMPARE(client.pending(), 1);
        // The held one answered late, in its own time.
        QCOMPARE(held.size(), 1);
        held.takeFirst().ok(QJsonObject{{u"state"_s, u"idle"_s}});
        QTRY_COMPARE(answers.size(), 3);
        QCOMPARE(answers.at(2).first, u"wait"_s);
        QCOMPARE(answers.at(2).second.result.toObject().value(u"state"_s).toString(), u"idle"_s);

        // No answer in time.
        answers.clear();
        client.call(u"chatWait"_s, {{u"path"_s, u"AI/a.md"_s}}, 100, note(u"slow"_s));
        QTRY_COMPARE(answers.size(), 1);
        QVERIFY(answers.at(0).second.transportError);
        QCOMPARE(answers.at(0).second.code, u"no_answer"_s);
        // (Its late answer is dropped.)
        QTRY_COMPARE(held.size(), 1);
        held.takeFirst().ok(QJsonObject{});
        QTest::qWait(50);
        QCOMPARE(answers.size(), 1);

        // The app goes: the calls waiting fail, it is down — and comes back by itself.
        answers.clear();
        client.call(u"chatWait"_s, {{u"path"_s, u"AI/a.md"_s}}, 30000, note(u"gone"_s));
        QTRY_COMPARE(held.size(), 1);
        held.clear();
        server->close();
        QTRY_COMPARE(answers.size(), 1);
        QVERIFY(answers.at(0).second.transportError);
        QCOMPARE(answers.at(0).second.code, u"disconnected"_s);
        QVERIFY(!client.isReady());
        QCOMPARE(ready.count(), 2);
        QVERIFY(server->listen(k));
        QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 10000 * BRULION_TEST_SLOWDOWN);
        answers.clear();
        client.call(u"chatSend"_s, {{u"path"_s, u"AI/a.md"_s}, {u"text"_s, u"Znowu"_s}}, 30000, note(u"back"_s));
        QTRY_COMPARE(answers.size(), 1);
        QCOMPARE(answers.at(0).second.result.toObject().value(u"echo"_s).toString(), u"Znowu"_s);

        // The grant revoked: dropped, and a hello with it refused from then on (it stays down).
        QVERIFY(server->revokeGrant(token));
        QTRY_VERIFY(!client.isReady());
        QTest::qWait(400);
        QVERIFY(!client.isReady());
        QVERIFY(client.lastError().contains(u"refused"_s));
        client.stop();
        server->close();
    }

    // ControlClient says who it is in a hello first: setIdentity(), else the environment.
    void clientIdentity()
    {
        const QString k = key(u"brulion-test-identity"_s);
        QString token;
        ServerThread server(
                k,
                [](const ControlRequest &request, ControlReply reply) {
                    reply.ok(QJsonObject{{u"client"_s, request.client}, {u"granted"_s, request.granted}});
                },
                [&token](ControlServer &s) {
                    s.setOpen(false);
                    token = s.addGrant(u"Agent"_s);
                });
        QVERIFY2(server.startListening(), qPrintable(server.error));
        QVERIFY(!token.isEmpty());

        qunsetenv(ControlClient::TokenVariable);
        qunsetenv(ControlClient::ClientVariable);
        ControlClient granted(k);
        granted.setIdentity(token, u"Ktoś"_s);
        QVERIFY(granted.connect());
        ControlClient::Response r = granted.call(u"state"_s);
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(r.result.toObject().value(u"client"_s).toString(), u"Agent"_s);
        QVERIFY(r.result.toObject().value(u"granted"_s).toBool());

        // A refused token: connected, but every call answers the refusal (no transport error).
        ControlClient wrong(k);
        wrong.setIdentity(u"00ff"_s, QString());
        QVERIFY(wrong.connect());
        r = wrong.call(u"state"_s);
        QVERIFY(!r.ok);
        QVERIFY(!r.transportError);
        QCOMPARE(r.code, u"unauthorized"_s);
        QCOMPARE(wrong.call(u"hello"_s).code, u"unauthorized"_s);
        // No token at all: the closed server refuses the request itself.
        ControlClient none(k);
        QVERIFY(none.token().isEmpty());
        QVERIFY(none.connect());
        QCOMPARE(none.call(u"state"_s).code, u"unauthorized"_s);

        // The environment of a program an agent started.
        qputenv(ControlClient::TokenVariable, token.toUtf8());
        putEnvironment(ControlClient::ClientVariable, u"Żółw\nbłotny"_s);
        ControlClient fromEnv(k);
        qunsetenv(ControlClient::TokenVariable);
        qunsetenv(ControlClient::ClientVariable);
        QCOMPARE(fromEnv.token(), token);
        QCOMPARE(fromEnv.client(), u"Żółw błotny"_s);
        QVERIFY(fromEnv.connect());
        r = fromEnv.call(u"state"_s);
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(r.result.toObject().value(u"client"_s).toString(), u"Agent"_s);
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

#ifndef Q_OS_UNIX
        QSKIP("Folder modes and symbolic links are Unix checks: on Windows the socket is a named pipe "
              "with an access list of the user");
#else
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
#endif
    }

    // Who may talk with whom (trustedPeer(), the control and the single-instance sockets alike):
    // on Linux and Windows only this user, never a process whose credentials cannot be read.
    void peersOfThisUserOnly()
    {
        const qint64 own = ownUid();
        QVERIFY(trustedPeer(own, own));
        QVERIFY(listenerProblem(own, u"/run/user/1000/brulion-x"_s).isEmpty());
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
        QVERIFY(own >= 0);
        QVERIFY(!trustedPeer(own + 1, own));
        QVERIFY(!trustedPeer(-1, own));
        QVERIFY(listenerProblem(own + 1, u"/tmp/brulion-x"_s).contains(u"Another user"_s));
        QVERIFY(listenerProblem(-1, u"/tmp/brulion-x"_s).contains(u"/tmp/brulion-x"_s));
        QCOMPARE(peerUid(-1), -1);
        // A real connection: each end sees this user at the other.
        QLocalServer server;
        QVERIFY(server.listen(key(u"rozmówca"_s)));
        QLocalSocket client;
        client.connectToServer(key(u"rozmówca"_s));
        QVERIFY(client.waitForConnected(1000));
        QVERIFY(server.waitForNewConnection(1000));
        QLocalSocket *accepted = server.nextPendingConnection();
        QVERIFY(accepted);
        QCOMPARE(peerUid(accepted->socketDescriptor()), own);
        QCOMPARE(peerUid(client.socketDescriptor()), own);
#endif
    }
};

QTEST_GUILESS_MAIN(TestIpcControl)
#include "tst_ipc_control.moc"
