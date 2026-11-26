// Code mode's threads on another computer (core acp/codehost.h): the slug of ids and worktrees (the
// app's rule, Polish letters without their marks), project ids, the --projects file (entries that are
// none dropped, ids once, the limit), a new worktree's folder and branch (a free name), what a
// thread's session names in `_meta` — written and taken out again (other keys kept, an empty one
// dropped, a malformed one taken out and said so, other methods untouched) —, a turn's checkpoint as
// a notification and back (ids that are none refused), the turn kept with its checkpoint, and the
// client's side: an extension's notification handed on with its conversation (acp/acp.h).
#include "acp/acp.h"
#include "acp/codehost.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::acp;

class TestAcpCodeHost : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void slug_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("slug");
        QTest::newRow("words") << u"Mój Projekt (2)!"_s << u"moj-projekt-2"_s;
        QTest::newRow("Polish") << u"Łódź — żółw"_s << u"lodz-zolw"_s;
        QTest::newRow("a title") << u"Napraw błąd #12"_s << u"napraw-blad-12"_s;
        QTest::newRow("nothing left") << u"…"_s << u"project"_s;
        QTest::newRow("empty") << QString() << u"project"_s;
        QTest::newRow("long") << QString(60, u'a') << QString(40, u'a');
        QTest::newRow("dashes") << u"--a--b--"_s << u"a-b"_s;
    }
    void slug()
    {
        QFETCH(QString, text);
        QFETCH(QString, slug);
        QCOMPARE(projectSlug(text), slug);
        QVERIFY(isProjectId(projectSlug(text)));
    }

    void projectIds_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("ok");
        QTest::newRow("slug") << u"projekt-zolwia"_s << true;
        QTest::newRow("numbered") << u"ala-2"_s << true;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("upper case") << u"Ala"_s << false;
        QTest::newRow("a path") << u"../ala"_s << false;
        QTest::newRow("a slash") << u"a/b"_s << false;
        QTest::newRow("Polish") << u"żółw"_s << false;
        QTest::newRow("leading dash") << u"-a"_s << false;
        QTest::newRow("double dash") << u"a--b"_s << false;
        QTest::newRow("too long") << QString(61, u'a') << false;
    }
    void projectIds()
    {
        QFETCH(QString, id);
        QFETCH(bool, ok);
        QCOMPARE(isProjectId(id), ok);
    }

    void projectsFile()
    {
        const QByteArray json = R"([
            {"id": "zolw", "name": "Projekt  żółwia", "path": "/home/ala/kod/zolw/", "worktreesRoot": "/home/ala/drzewa"},
            {"id": "zolw", "name": "drugi", "path": "/x"},
            {"id": "../x", "name": "zły", "path": "/x"},
            {"id": "bez-sciezki", "name": "x"},
            {"id": "wzgledna", "path": "kod/x"},
            {"id": "zly-korzen", "path": "/x", "worktreesRoot": "drzewa"},
            {"id": "bez-nazwy", "path": "/srv/x"},
            7
        ])";
        QString error;
        const std::optional<QList<HostProject>> read = parseHostProjects(json, &error);
        QVERIFY2(read, qPrintable(error));
        QCOMPARE(read->size(), 2);
        QCOMPARE(read->at(0), (HostProject{u"zolw"_s, u"Projekt żółwia"_s, u"/home/ala/kod/zolw"_s, u"/home/ala/drzewa"_s}));
        QCOMPARE(read->at(1).name, u"bez-nazwy"_s); // (no name: its id)
        QCOMPARE(parseHostProjects(hostProjectsJson(*read)), read);
        // No list: refused, with why.
        QVERIFY(!parseHostProjects(R"({"projects": []})"_ba, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!parseHostProjects("nie json"_ba));
        // At most MaxHostProjects.
        QJsonArray many;
        for (int i = 0; i < MaxHostProjects + 5; ++i)
            many.append(QJsonObject{{u"id"_s, u"p-%1"_s.arg(i)}, {u"path"_s, u"/p/%1"_s.arg(i)}});
        QCOMPARE(parseHostProjects(QJsonDocument(many).toJson())->size(), MaxHostProjects);
    }

    void worktrees()
    {
        QStringList taken{u"/drzewa/zolw/napraw-blad-12"_s, u"/drzewa/zolw/napraw-blad-12-2"_s};
        const WorktreePlace place = worktreePlace(u"/drzewa"_s, u"zolw"_s, u"Napraw błąd #12"_s,
                                                  [&taken](const QString &f) { return taken.contains(f); });
        QCOMPARE(place.folder, u"/drzewa/zolw/napraw-blad-12-3"_s);
        QCOMPARE(place.branch, u"brulion/napraw-blad-12-3"_s);
        const WorktreePlace first = worktreePlace(QString(), u"zolw"_s, u"Nowy wątek"_s, {});
        QCOMPARE(first.folder, QDir::cleanPath(defaultWorktreesRoot() + u"/zolw/nowy-watek"_s));
        QCOMPARE(first.branch, u"brulion/nowy-watek"_s);
    }

    void session()
    {
        const CodeSession s{u"zolw"_s, u"/home/ala/drzewa/zolw/napraw"_s, u"brulion/napraw"_s};
        QCOMPARE(CodeSession::fromJson(s.toJson()).value_or(CodeSession{}), s);
        QCOMPARE(CodeSession::fromJson(QJsonObject{{u"project"_s, u"zolw"_s}}).value_or(CodeSession{}), (CodeSession{u"zolw"_s, {}, {}}));
        // A worktree as it is cleaned.
        QCOMPARE(CodeSession::fromJson(QJsonObject{{u"project"_s, u"zolw"_s}, {u"worktree"_s, u"/a/b/../c/"_s}})->worktree,
                 u"/a/c"_s);
        // None: no object, an id that is none, a worktree that is not absolute, a NUL, a branch of lines.
        QVERIFY(!CodeSession::fromJson(u"zolw"_s));
        QVERIFY(!CodeSession::fromJson(QJsonObject{{u"project"_s, u"../etc"_s}}));
        QVERIFY(!CodeSession::fromJson(QJsonObject{{u"project"_s, u"zolw"_s}, {u"worktree"_s, u"drzewa/x"_s}}));
        QVERIFY(!CodeSession::fromJson(QJsonObject{{u"project"_s, u"zolw"_s}, {u"worktree"_s, u"/a\0b"_s}}));
        QVERIFY(!CodeSession::fromJson(QJsonObject{{u"project"_s, u"zolw"_s}, {u"branch"_s, u"a\nb"_s}}));
        QVERIFY(!CodeSession::fromJson(
                QJsonObject{{u"project"_s, u"zolw"_s}, {u"worktree"_s, QString(u"/"_s + QString(MaxRemotePathChars, u'a'))}}));
    }

    void takenOut()
    {
        // Written into a session's request with other `_meta` keys, taken out again.
        const CodeSession s{u"zolw"_s, QString(), QString()};
        QJsonObject params{{u"cwd"_s, u"/telefon"_s}, {u"_meta"_s, QJsonObject{{u"brulion/app"_s, QJsonObject{{u"handle"_s, u"h"_s}}}}}};
        params = withCodeSession(params, s);
        QJsonObject message{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, 3}, {u"method"_s, u"session/new"_s}, {u"params"_s, params}};
        bool malformed = true;
        QCOMPARE(takeCodeSession(message, &malformed).value_or(CodeSession{}), s);
        QVERIFY(!malformed);
        const QJsonObject meta = message.value(u"params"_s).toObject().value(u"_meta"_s).toObject();
        QVERIFY(!meta.contains(CodeMetaKey));
        QVERIFY(meta.contains(u"brulion/app"_s));
        // Its `_meta` alone: dropped.
        QJsonObject alone{{u"method"_s, u"session/load"_s}, {u"params"_s, withCodeSession({{u"sessionId"_s, u"s1"_s}}, s)}};
        QVERIFY(takeCodeSession(alone));
        QVERIFY(!alone.value(u"params"_s).toObject().contains(u"_meta"_s));
        QCOMPARE(alone.value(u"params"_s).toObject().value(u"sessionId"_s).toString(), u"s1"_s);
        // A malformed one: taken out all the same, and said so.
        QJsonObject bad{{u"method"_s, u"session/resume"_s},
                        {u"params"_s, QJsonObject{{u"_meta"_s, QJsonObject{{CodeMetaKey, QJsonObject{{u"project"_s, u"/etc"_s}}}}}}}};
        QVERIFY(!takeCodeSession(bad, &malformed));
        QVERIFY(malformed);
        QVERIFY(!bad.value(u"params"_s).toObject().contains(u"_meta"_s));
        // Other methods: untouched.
        QJsonObject prompt{{u"method"_s, u"session/prompt"_s}, {u"params"_s, withCodeSession({}, s)}};
        const QJsonObject before = prompt;
        QVERIFY(!takeCodeSession(prompt, &malformed));
        QVERIFY(!malformed);
        QCOMPARE(prompt, before);
        // None: none.
        QJsonObject plain{{u"method"_s, u"session/new"_s}, {u"params"_s, QJsonObject{{u"cwd"_s, u"/x"_s}}}};
        QVERIFY(!takeCodeSession(plain, &malformed));
        QVERIFY(!malformed);
    }

    void checkpoint()
    {
        const RemoteCheckpoint c{u"s1"_s, u"tura-1"_s, u"0123456789abcdef0123456789abcdef"_s, u"2026-11-26T10:00:00Z"_s};
        const QJsonObject n = checkpointNotification(c);
        QCOMPARE(n.value(u"method"_s).toString(), QString(CheckpointMethod));
        QVERIFY(!n.contains(u"id"_s)); // (a notification)
        QCOMPARE(readCheckpoint(n.value(u"params"_s).toObject()).value_or(RemoteCheckpoint{}), c);
        // Ids that are none: refused.
        for (const QString &id : {QString(), u"../x"_s, u"a b"_s, u"a//b"_s, QString(201, u'a'), u"żółw"_s}) {
            QJsonObject p = n.value(u"params"_s).toObject();
            p.insert(u"checkpoint"_s, QJsonObject{{u"id"_s, id}});
            QVERIFY2(!readCheckpoint(p), qPrintable(id));
        }
        QVERIFY(!readCheckpoint(QJsonObject{{u"checkpoint"_s, QJsonObject{{u"id"_s, c.id}}}})); // (no session)
        // The turn kept carries it.
        const std::optional<RemoteTurn> turn = RemoteTurn::fromJson(
                QJsonObject{{u"session"_s, u"s1"_s}, {u"turn"_s, u"tura-1"_s}, {u"state"_s, u"done"_s},
                            {u"checkpoint"_s, QJsonObject{{u"id"_s, c.id}, {u"at"_s, c.at}}}});
        QVERIFY(turn);
        QCOMPARE(turn->checkpoint, c.id);
        QCOMPARE(turn->checkpointAt, c.at);
        QVERIFY(RemoteTurn::fromJson(QJsonObject{{u"state"_s, u"done"_s}})->checkpoint.isEmpty());
    }

    void clientHearsTheCheckpoint()
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
        QList<std::tuple<QString, QString, QJsonObject>> heard;
        connect(&client, &Client::extensionNotified, this,
                [&heard](const QString &key, const QString &method, const QJsonObject &params) {
                    heard.append({key, method, params});
                });
        TurnInput in;
        in.cwd = u"/home/ala/kod/zolw"_s;
        in.firstPrompt << Block::textBlock(u"Napraw testy"_s);
        in.prompt << Block::textBlock(u"Napraw testy"_s);
        in.sessionMeta = QJsonObject{{CodeMetaKey, CodeSession{u"zolw"_s, {}, {}}.toJson()}};
        QVERIFY(client.prompt(u"AI/Wątek żółwia.md"_s, in));
        agentWrites({{u"id"_s, last(u"initialize"_s).value(u"id"_s)},
                     {u"result"_s, QJsonObject{{u"protocolVersion"_s, 1}, {u"agentCapabilities"_s, QJsonObject()}}}});
        const QJsonObject created = last(u"session/new"_s);
        QCOMPARE(created.value(u"params"_s).toObject().value(u"_meta"_s).toObject().value(CodeMetaKey).toObject()
                         .value(u"project"_s)
                         .toString(),
                 u"zolw"_s);
        agentWrites({{u"id"_s, created.value(u"id"_s)}, {u"result"_s, QJsonObject{{u"sessionId"_s, u"s1"_s}}}});
        // The computer's notification: handed on with its conversation …
        agentWrites(checkpointNotification({u"s1"_s, QString(), u"abc123"_s, u"2026-11-26T10:00:00Z"_s}));
        QCOMPARE(heard.size(), 1);
        QCOMPARE(std::get<0>(heard.at(0)), u"AI/Wątek żółwia.md"_s);
        QCOMPARE(std::get<1>(heard.at(0)), QString(CheckpointMethod));
        QCOMPARE(readCheckpoint(std::get<2>(heard.at(0)))->id, u"abc123"_s);
        // … of a session it knows not: no conversation.
        agentWrites(checkpointNotification({u"s9"_s, QString(), u"abc124"_s, QString()}));
        QCOMPARE(heard.size(), 2);
        QVERIFY(std::get<0>(heard.at(1)).isEmpty());
        // Other notifications are not extensions'.
        agentWrites({{u"method"_s, u"other/thing"_s}, {u"params"_s, QJsonObject{{u"sessionId"_s, u"s1"_s}}}});
        QCOMPARE(heard.size(), 2);
        QVERIFY(!sent.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestAcpCodeHost)
#include "tst_acp_codehost.moc"
