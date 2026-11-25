// The app's live tools for an agent on another computer (core acp/apptunnel.h): what a session asks
// for (its handle, the live tools, the chat's) in its request's `_meta`, written and taken out again
// (other keys of `_meta` kept, an empty one dropped, a malformed one taken out too, other methods
// untouched); a call to the device's app as a JSON-RPC request and back, malformed calls refused
// with why (no id, no handle, odd characters, another scope, no method, params no object, too
// large); the device's answers (a result, an error with details) and what an older app's JSON-RPC
// error or an answer that is none becomes; an answer too large replaced by a failure. The client's
// side (acp/acp.h): extension methods handed to its extension handler — "method not found" without
// one —, and a session's `_meta` sent with session/new.
#include "acp/acp.h"
#include "acp/apptunnel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::acp;

namespace {

QJsonObject request(const QString &method, const QJsonObject &params)
{
    return {{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, 7}, {u"method"_s, method}, {u"params"_s, params}};
}

/// The params of a call, `over` replacing its keys and `without` taken out (an undefined value in a
/// QJsonObject is no key at all).
QJsonObject callParams(const QJsonObject &over = {}, const QStringList &without = {})
{
    QJsonObject p{{u"handle"_s, u"h_K7QM2XRP-zaz"_s},
                  {u"scope"_s, u"control"_s},
                  {u"method"_s, u"theme"_s},
                  {u"params"_s, QJsonObject{{u"tokens"_s, QJsonObject{{u"accent"_s, u"#33aa77"_s}}}}}};
    for (auto it = over.begin(); it != over.end(); ++it)
        p.insert(it.key(), it.value());
    for (const QString &key : without)
        p.remove(key);
    return p;
}

} // namespace

class TestAcpAppTunnel : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void session()
    {
        const AppSession s{u"h_abc-DEF_123"_s, true, false};
        QCOMPARE(AppSession::fromJson(s.toJson()), s);
        QVERIFY(!s.isEmpty());
        QVERIFY((AppSession{u"h"_s, false, false}).isEmpty()); // (asks for nothing)
        QVERIFY((AppSession{QString(), true, true}).isEmpty());
        // Only a true boolean asks.
        const std::optional<AppSession> strings =
                AppSession::fromJson(QJsonObject{{u"handle"_s, u"h1"_s}, {u"control"_s, u"true"_s}, {u"chat"_s, 1}});
        QVERIFY(strings);
        QVERIFY(!strings->control);
        QVERIFY(!strings->chat);
    }

    void sessionRefused_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("no object") << QJsonValue(u"h1"_s);
        QTest::newRow("no handle") << QJsonValue(QJsonObject{{u"control"_s, true}});
        QTest::newRow("empty handle") << QJsonValue(QJsonObject{{u"handle"_s, u""_s}, {u"control"_s, true}});
        QTest::newRow("a number") << QJsonValue(QJsonObject{{u"handle"_s, 12}, {u"control"_s, true}});
        QTest::newRow("spaces") << QJsonValue(QJsonObject{{u"handle"_s, u"h 1"_s}, {u"control"_s, true}});
        QTest::newRow("Polish letters") << QJsonValue(QJsonObject{{u"handle"_s, u"żółw"_s}, {u"control"_s, true}});
        QTest::newRow("a slash") << QJsonValue(QJsonObject{{u"handle"_s, u"../h"_s}, {u"control"_s, true}});
        QTest::newRow("too long") << QJsonValue(
                QJsonObject{{u"handle"_s, QString(MaxAppHandleChars + 1, u'a')}, {u"control"_s, true}});
    }
    void sessionRefused()
    {
        QFETCH(QJsonValue, value);
        QVERIFY(!AppSession::fromJson(value));
    }

    void sessionMeta()
    {
        const AppSession s{u"h_rozmowa1"_s, true, true};
        // Written into the params, the meta keys already there kept.
        const QJsonObject params = withAppSession(
                QJsonObject{{u"cwd"_s, u"/home/ala/Notatki"_s}, {u"_meta"_s, QJsonObject{{u"inne"_s, 1}}}}, s);
        QCOMPARE(params.value(u"_meta"_s).toObject().value(u"inne"_s).toInt(), 1);
        QCOMPARE(params.value(u"_meta"_s).toObject().value(AppMetaKey).toObject(), s.toJson());
        // Taken out of session/new, /load, /resume — the other keys stay.
        for (const QString &method : {u"session/new"_s, u"session/load"_s, u"session/resume"_s}) {
            QJsonObject m = request(method, params);
            const std::optional<AppSession> taken = takeAppSession(m);
            QVERIFY2(taken, qPrintable(method));
            QCOMPARE(*taken, s);
            const QJsonObject left = m.value(u"params"_s).toObject();
            QCOMPARE(left.value(u"cwd"_s).toString(), u"/home/ala/Notatki"_s);
            QCOMPARE(left.value(u"_meta"_s).toObject(), (QJsonObject{{u"inne"_s, 1}}));
        }
        // Alone in `_meta`: `_meta` goes with it.
        QJsonObject alone = request(u"session/new"_s, withAppSession(QJsonObject{{u"cwd"_s, u"/x"_s}}, s));
        QVERIFY(takeAppSession(alone));
        QVERIFY(!alone.value(u"params"_s).toObject().contains(u"_meta"_s));
        // A malformed one is taken out all the same (the agent never sees it), and asks for nothing.
        QJsonObject odd =
                request(u"session/new"_s, QJsonObject{{u"_meta"_s, QJsonObject{{AppMetaKey, u"wszystko"_s}}}});
        QVERIFY(!takeAppSession(odd));
        QVERIFY(!odd.value(u"params"_s).toObject().contains(u"_meta"_s));
        // Another method, or none there: untouched.
        QJsonObject prompt = request(u"session/prompt"_s, params);
        const QJsonObject before = prompt;
        QVERIFY(!takeAppSession(prompt));
        QCOMPARE(prompt, before);
        QJsonObject plain = request(u"session/new"_s, QJsonObject{{u"cwd"_s, u"/x"_s}});
        const QJsonObject plainBefore = plain;
        QVERIFY(!takeAppSession(plain));
        QCOMPARE(plain, plainBefore);
    }

    void call()
    {
        AppCall c;
        c.id = u"_brulion/app#Q7-1"_s;
        c.handle = u"h_K7QM2XRP-zaz"_s;
        c.scope = QString(AppScopeChat);
        c.method = u"chatStart"_s;
        c.params = QJsonObject{{u"text"_s, u"Czy ten plan wyjazdu ma sens? Zażółć gęślą jaźń."_s}};
        const QJsonObject m = c.toMessage();
        QCOMPARE(m.value(u"method"_s).toString(), QString(AppMethod));
        QCOMPARE(m.value(u"jsonrpc"_s).toString(), u"2.0"_s);
        QString error;
        const std::optional<AppCall> back = AppCall::fromMessage(m, &error);
        QVERIFY2(back, qPrintable(error));
        QCOMPARE(back->id, c.id);
        QCOMPARE(back->handle, c.handle);
        QCOMPARE(back->scope, c.scope);
        QCOMPARE(back->method, c.method);
        QCOMPARE(back->params, c.params);
        // No params of its own: an empty object (also as null).
        QJsonObject bare = request(QString(AppMethod), callParams({}, {u"params"_s}));
        QVERIFY(AppCall::fromMessage(bare));
        QVERIFY(AppCall::fromMessage(bare)->params.isEmpty());
        bare = request(QString(AppMethod), callParams({{u"params"_s, QJsonValue::Null}}));
        QVERIFY(AppCall::fromMessage(bare));
        // A number as the id is a JSON-RPC id too.
        QVERIFY(AppCall::fromMessage(request(QString(AppMethod), callParams())));
    }

    void callRefused_data()
    {
        QTest::addColumn<QJsonObject>("message");
        QTest::addColumn<QString>("why");
        const QString method = QString(AppMethod);
        QJsonObject noId = request(method, callParams());
        noId.remove(u"id"_s);
        QTest::newRow("no id") << noId << u"no id"_s;
        QJsonObject objectId = request(method, callParams());
        objectId.insert(u"id"_s, QJsonObject{{u"a"_s, 1}});
        QTest::newRow("an object as id") << objectId << u"no id"_s;
        QTest::newRow("another method") << request(u"_brulion/inne"_s, callParams()) << u"not a call"_s;
        QJsonObject noParams = request(method, {});
        noParams.remove(u"params"_s);
        QTest::newRow("no params") << noParams << u"no params"_s;
        QTest::newRow("no handle") << request(method, callParams({}, {u"handle"_s}))
                                   << u"handle"_s;
        QTest::newRow("a bad handle") << request(method, callParams({{u"handle"_s, u"h/../x"_s}})) << u"handle"_s;
        QTest::newRow("another scope") << request(method, callParams({{u"scope"_s, u"admin"_s}})) << u"scope"_s;
        QTest::newRow("no scope") << request(method, callParams({}, {u"scope"_s})) << u"scope"_s;
        QTest::newRow("no method") << request(method, callParams({{u"method"_s, u""_s}})) << u"method"_s;
        QTest::newRow("a method with spaces")
                << request(method, callParams({{u"method"_s, u"set Property"_s}})) << u"method"_s;
        QTest::newRow("a Polish method") << request(method, callParams({{u"method"_s, u"żółć"_s}})) << u"method"_s;
        QTest::newRow("a long method") << request(method, callParams({{u"method"_s, QString(65, u'a')}}))
                                       << u"method"_s;
        QTest::newRow("params an array") << request(method, callParams({{u"params"_s, QJsonArray{1, 2}}}))
                                         << u"no object"_s;
        QTest::newRow("too large") << request(
                method, callParams({{u"params"_s, QJsonObject{{u"code"_s, QString(int(MaxAppCallBytes), u'x')}}}}))
                                   << u"larger"_s;
    }
    void callRefused()
    {
        QFETCH(QJsonObject, message);
        QFETCH(QString, why);
        QString error;
        QVERIFY(!AppCall::fromMessage(message, &error));
        QVERIFY2(error.contains(why), qPrintable(error));
    }

    void answers()
    {
        // A result, as the remote control's socket gives it.
        const AppAnswer ok = AppAnswer::success(QJsonObject{{u"undone"_s, 3}});
        QCOMPARE(ok.toJson(), (QJsonObject{{u"ok"_s, true}, {u"result"_s, QJsonObject{{u"undone"_s, 3}}}}));
        const QJsonObject okMessage = appAnswerMessage(u"_brulion/app#1"_s, ok);
        QCOMPARE(okMessage.value(u"id"_s).toString(), u"_brulion/app#1"_s);
        const AppAnswer okBack = AppAnswer::fromResponse(okMessage);
        QVERIFY(okBack.ok);
        QCOMPARE(okBack.result.toObject().value(u"undone"_s).toInt(), 3);
        // A refusal with its details.
        const AppAnswer bad =
                AppAnswer::failure(u"bad_params"_s, u"Nie ma takiego tokenu: ąę"_s, {{u"param"_s, u"tokens"_s}});
        const AppAnswer badBack = AppAnswer::fromResponse(appAnswerMessage(2, bad));
        QVERIFY(!badBack.ok);
        QCOMPARE(badBack.code, u"bad_params"_s);
        QCOMPARE(badBack.message, u"Nie ma takiego tokenu: ąę"_s);
        QCOMPARE(badBack.details.value(u"param"_s).toString(), u"tokens"_s);
        QVERIFY(!AppAnswer::failure(u"refused"_s, u"x"_s).toJson().value(u"error"_s).toObject().contains(u"details"_s));
        QCOMPARE(AppAnswer::failure(QString(), u"x"_s).code, u"failed"_s);
        // An older app knows no such method: a JSON-RPC error, which says so.
        const AppAnswer older = AppAnswer::fromResponse(
                QJsonObject{{u"id"_s, 3},
                            {u"error"_s, QJsonObject{{u"code"_s, MethodNotFound},
                                                     {u"message"_s, u"Method not found: _brulion/app"_s}}}});
        QVERIFY(!older.ok);
        QCOMPARE(older.code, u"failed"_s);
        QVERIFY2(older.message.contains(u"Method not found"_s), qPrintable(older.message));
        // Answers that are none.
        for (const QJsonObject &none :
             {QJsonObject{{u"id"_s, 4}}, QJsonObject{{u"id"_s, 4}, {u"result"_s, QJsonObject{{u"ok"_s, true}}}},
              QJsonObject{{u"id"_s, 4}, {u"result"_s, QJsonObject{{u"ok"_s, u"tak"_s}, {u"result"_s, 1}}}},
              QJsonObject{{u"id"_s, 4}, {u"result"_s, QJsonObject{{u"ok"_s, false}, {u"error"_s, u"zły"_s}}}},
              QJsonObject{{u"id"_s, 4},
                          {u"result"_s, QJsonObject{{u"ok"_s, false},
                                                    {u"error"_s, QJsonObject{{u"code"_s, QString(200, u'x')}}}}}}}) {
            const AppAnswer a = AppAnswer::fromResponse(none);
            QVERIFY(!a.ok);
            QCOMPARE(a.code, u"failed"_s);
        }
        // An answer larger than MaxAppAnswerBytes: a failure that says so, small.
        const AppAnswer huge = AppAnswer::success(QJsonObject{{u"png"_s, QString(int(MaxAppAnswerBytes), u'A')}});
        const QJsonObject replaced = appAnswerMessage(5, huge);
        QVERIFY(QJsonDocument(replaced).toJson(QJsonDocument::Compact).size() < 1000);
        const AppAnswer cut = AppAnswer::fromResponse(replaced);
        QVERIFY(!cut.ok);
        QVERIFY2(cut.message.contains(u"larger"_s), qPrintable(cut.message));
    }

    void clientHandsOnExtensionMethods()
    {
        QList<QJsonObject> sent;
        Client client([&sent](const QByteArray &line) { sent << QJsonDocument::fromJson(line).object(); });
        const auto agentWrites = [&client](const QJsonObject &m) {
            client.received(QJsonDocument(m).toJson(QJsonDocument::Compact) + '\n');
        };
        AppCall c;
        c.id = u"_brulion/app#x-1"_s;
        c.handle = u"h1"_s;
        c.scope = QString(AppScopeControl);
        c.method = u"state"_s;
        // Without a handler: "method not found", as before.
        agentWrites(c.toMessage());
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent.last().value(u"id"_s).toString(), u"_brulion/app#x-1"_s);
        QCOMPARE(sent.last().value(u"error"_s).toObject().value(u"code"_s).toInt(), MethodNotFound);
        // With one: it gets the request and answers it later.
        QList<std::tuple<QJsonValue, QString, QJsonObject>> heard;
        client.setExtensionHandler([&heard](const QJsonValue &id, const QString &method, const QJsonObject &params) {
            heard.append({id, method, params});
        });
        agentWrites(c.toMessage());
        QCOMPARE(heard.size(), 1);
        QCOMPARE(std::get<1>(heard.at(0)), QString(AppMethod));
        QCOMPARE(std::get<2>(heard.at(0)).value(u"method"_s).toString(), u"state"_s);
        QCOMPARE(sent.size(), 1); // (nothing answered by itself)
        client.respond(std::get<0>(heard.at(0)), AppAnswer::success(QJsonObject{{u"ready"_s, true}}).toJson());
        QCOMPARE(sent.size(), 2);
        QVERIFY(AppAnswer::fromResponse(sent.last()).ok);
        // Other unknown methods are still "method not found".
        agentWrites(QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, 9}, {u"method"_s, u"unknown/thing"_s}});
        QCOMPARE(heard.size(), 1);
        QCOMPARE(sent.last().value(u"error"_s).toObject().value(u"code"_s).toInt(), MethodNotFound);
    }

    void sessionMetaSent()
    {
        QList<QJsonObject> sent;
        Client client([&sent](const QByteArray &line) { sent << QJsonDocument::fromJson(line).object(); });
        const auto agentWrites = [&client](const QJsonObject &m) {
            QJsonObject full = m;
            full.insert(u"jsonrpc"_s, u"2.0"_s);
            client.received(QJsonDocument(full).toJson(QJsonDocument::Compact) + '\n');
        };
        const auto last = [&sent](const QString &method) {
            for (qsizetype i = sent.size() - 1; i >= 0; --i) {
                if (sent.at(i).value(u"method"_s).toString() == method)
                    return sent.at(i);
            }
            return QJsonObject();
        };
        TurnInput in;
        in.cwd = u"/srv/Notatki"_s;
        in.firstPrompt << Block::textBlock(u"Cześć"_s);
        in.prompt << Block::textBlock(u"Cześć"_s);
        in.sessionMeta = QJsonObject{{AppMetaKey, AppSession{u"h_a1"_s, true, true}.toJson()}};
        QVERIFY(client.prompt(u"AI/Rozmowa.md"_s, in));
        agentWrites({{u"id"_s, last(u"initialize"_s).value(u"id"_s)},
                     {u"result"_s, QJsonObject{{u"protocolVersion"_s, 1}, {u"agentCapabilities"_s, QJsonObject()}}}});
        const QJsonObject created = last(u"session/new"_s);
        QVERIFY(!created.isEmpty());
        const QJsonObject params = created.value(u"params"_s).toObject();
        QCOMPARE(params.value(u"cwd"_s).toString(), u"/srv/Notatki"_s);
        QCOMPARE(AppSession::fromJson(params.value(u"_meta"_s).toObject().value(AppMetaKey)),
                 (AppSession{u"h_a1"_s, true, true}));
        // A turn without one sends no `_meta` (as before).
        Client plain([&sent](const QByteArray &line) { sent << QJsonDocument::fromJson(line).object(); });
        sent.clear();
        TurnInput none = in;
        none.sessionMeta = {};
        QVERIFY(plain.prompt(u"AI/Inna.md"_s, none));
        plain.received(QJsonDocument(QJsonObject{{u"jsonrpc"_s, u"2.0"_s},
                                                 {u"id"_s, last(u"initialize"_s).value(u"id"_s)},
                                                 {u"result"_s, QJsonObject{{u"protocolVersion"_s, 1}}}})
                               .toJson(QJsonDocument::Compact)
                       + '\n');
        QVERIFY(!last(u"session/new"_s).value(u"params"_s).toObject().contains(u"_meta"_s));
    }
};

QTEST_GUILESS_MAIN(TestAcpAppTunnel)
#include "tst_acp_apptunnel.moc"
