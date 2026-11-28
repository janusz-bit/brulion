// Code mode's threads on another computer (core acp/codehost.h): the slug of ids and worktrees (the
// app's rule, Polish letters without their marks), project ids, the --projects file (entries that are
// none dropped, ids once, the limit), a new worktree's folder and branch (a free name), what a
// thread's session names in `_meta` — written and taken out again (other keys kept, an empty one
// dropped, a malformed one taken out and said so, other methods untouched) —, a turn's checkpoint as
// a notification and back (ids that are none refused), the turn kept with its checkpoint, and the
// client's side: an extension's notification handed on with its conversation (acp/acp.h). What a
// device asks of git there — a commit, a push, a pull request, a turn reverted —: written and read
// back, every refusal (an empty message, paths out of the work tree or into .git, a base that is no
// branch, a checkpoint that is none, sizes), the answers read, a pull request's number, and the
// request sent (its endpoint, its body) or — refused — never sent; a project's branches there.
#include "acp/acp.h"
#include "acp/codehost.h"
#include "sync/http.h"

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

    void gitRequests()
    {
        using Kind = RemoteGitRequest::Kind;
        for (const Kind k : {Kind::Commit, Kind::Push, Kind::PullRequest, Kind::Revert})
            QCOMPARE(RemoteGitRequest::kindOf(RemoteGitRequest::endpoint(k)), std::optional<Kind>(k));
        QCOMPARE(RemoteGitRequest::endpoint(Kind::PullRequest), u"pr"_s);
        QVERIFY(!RemoteGitRequest::kindOf(u"worktrees"_s));
        QVERIFY(!RemoteGitRequest::kindOf(QString()));
        // Written and read back, each kind with what it takes (nothing else).
        RemoteGitRequest commit;
        commit.worktree = u"/home/ala/drzewa/zolw/napraw"_s;
        commit.message = u"Napraw błąd żółwia\n\nTreść"_s;
        commit.files = {u"src/żółw.cpp"_s, u"Dom.md"_s};
        QCOMPARE(RemoteGitRequest::fromJson(Kind::Commit, commit.toJson()).value_or(RemoteGitRequest{}), commit);
        QCOMPARE(commit.problem(), QString());
        RemoteGitRequest pr;
        pr.kind = Kind::PullRequest;
        pr.title = u"Rabat 10%"_s;
        pr.body = u"Opis zmian"_s;
        pr.base = u"main"_s;
        pr.draft = true;
        QCOMPARE(RemoteGitRequest::fromJson(Kind::PullRequest, pr.toJson()).value_or(RemoteGitRequest{}), pr);
        QVERIFY(!pr.toJson().contains(u"message"_s));
        RemoteGitRequest revert;
        revert.kind = Kind::Revert;
        revert.checkpoint = u"0123456789abcdef0123456789abcdef"_s;
        QCOMPARE(RemoteGitRequest::fromJson(Kind::Revert, revert.toJson()).value_or(RemoteGitRequest{}), revert);
        QVERIFY(!revert.toJson().contains(u"title"_s));
        RemoteGitRequest push;
        push.kind = Kind::Push;
        QCOMPARE(push.toJson(), QJsonObject());
        QCOMPARE(RemoteGitRequest::fromJson(Kind::Push, QJsonObject()).value_or(RemoteGitRequest{}), push);
        // A worktree as it is cleaned; no object: refused.
        QCOMPARE(RemoteGitRequest::fromJson(Kind::Push, QJsonObject{{u"worktree"_s, u"/a/b/../c/"_s}})->worktree,
                 u"/a/c"_s);
        QString code;
        QVERIFY(!RemoteGitRequest::fromJson(Kind::Push, QJsonArray{}, &code));
        QCOMPARE(code, u"bad_request"_s);
    }

    void gitRefusals_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QJsonObject>("body");
        QTest::addColumn<QString>("code");
        using Kind = RemoteGitRequest::Kind;
        const auto k = [](Kind kind) { return int(kind); };
        const QJsonObject ok{{u"message"_s, u"Napraw"_s}};
        const auto with = [&ok](const QString &key, const QJsonValue &v) {
            QJsonObject o = ok;
            o.insert(key, v);
            return o;
        };
        QTest::newRow("no message") << k(Kind::Commit) << QJsonObject() << u"bad_message"_s;
        QTest::newRow("blank message") << k(Kind::Commit) << QJsonObject{{u"message"_s, u" \n\t "_s}}
                                       << u"bad_message"_s;
        QTest::newRow("message not text") << k(Kind::Commit) << QJsonObject{{u"message"_s, 7}} << u"bad_message"_s;
        QTest::newRow("long message") << k(Kind::Commit)
                                      << QJsonObject{{u"message"_s, QString(MaxCommitMessageChars + 1, u'ż')}}
                                      << u"bad_message"_s;
        QTest::newRow("relative worktree")
                << k(Kind::Commit) << with(u"worktree"_s, u"drzewa/x"_s) << u"bad_worktree"_s;
        QTest::newRow("worktree NUL") << k(Kind::Push) << QJsonObject{{u"worktree"_s, u"/a\0b"_s}} << u"bad_worktree"_s;
        QTest::newRow("worktree not text") << k(Kind::Push) << QJsonObject{{u"worktree"_s, true}} << u"bad_worktree"_s;
        QTest::newRow("files not a list") << k(Kind::Commit) << with(u"files"_s, u"Dom.md"_s) << u"bad_files"_s;
        QTest::newRow("a file not text") << k(Kind::Commit) << with(u"files"_s, QJsonArray{3}) << u"bad_files"_s;
        QTest::newRow("dot dot") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u"../obcy"_s}) << u"bad_files"_s;
        QTest::newRow("inner dot dot") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u"a/../../b"_s})
                                       << u"bad_files"_s;
        QTest::newRow("absolute") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u"/etc/passwd"_s})
                                  << u"bad_files"_s;
        QTest::newRow("backslash") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u"a\\..\\b"_s}) << u"bad_files"_s;
        QTest::newRow(".git") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u".git/config"_s}) << u"bad_files"_s;
        QTest::newRow(".git alone") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u".git"_s}) << u"bad_files"_s;
        QTest::newRow(".GIT inside") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u"a/.GIT/hooks/x"_s})
                                     << u"bad_files"_s;
        QTest::newRow("empty file") << k(Kind::Commit) << with(u"files"_s, QJsonArray{QString()}) << u"bad_files"_s;
        QTest::newRow("file NUL") << k(Kind::Commit) << with(u"files"_s, QJsonArray{u"a\0b"_s}) << u"bad_files"_s;
        QTest::newRow("long file") << k(Kind::Commit)
                                   << with(u"files"_s, QJsonArray{QString(MaxRemotePathChars + 1, u'a')})
                                   << u"bad_files"_s;
        QJsonArray many;
        for (int i = 0; i <= MaxRemoteChanges; ++i)
            many.append(u"plik-%1.md"_s.arg(i));
        many.removeLast();
        QTest::newRow("as many files as taken") << k(Kind::Commit) << with(u"files"_s, many) << QString();
        many.append(u"jeszcze.md"_s);
        QTest::newRow("too many files") << k(Kind::Commit) << with(u"files"_s, many) << u"bad_files"_s;
        QTest::newRow("no title") << k(Kind::PullRequest) << QJsonObject() << u"bad_title"_s;
        QTest::newRow("blank title") << k(Kind::PullRequest) << QJsonObject{{u"title"_s, u"   "_s}} << u"bad_title"_s;
        QTest::newRow("long title") << k(Kind::PullRequest)
                                    << QJsonObject{{u"title"_s, QString(MaxPullRequestTitleChars + 1, u'a')}}
                                    << u"bad_title"_s;
        QTest::newRow("long body") << k(Kind::PullRequest)
                                   << QJsonObject{{u"title"_s, u"t"_s},
                                                  {u"body"_s, QString(MaxPullRequestBodyChars + 1, u'a')}}
                                   << u"bad_body"_s;
        QTest::newRow("base option") << k(Kind::PullRequest)
                                     << QJsonObject{{u"title"_s, u"t"_s}, {u"base"_s, u"--repo=x"_s}} << u"bad_base"_s;
        QTest::newRow("base dots") << k(Kind::PullRequest) << QJsonObject{{u"title"_s, u"t"_s}, {u"base"_s, u"a..b"_s}}
                                   << u"bad_base"_s;
        QTest::newRow("base space") << k(Kind::PullRequest) << QJsonObject{{u"title"_s, u"t"_s}, {u"base"_s, u"a b"_s}}
                                    << u"bad_base"_s;
        QTest::newRow("base long") << k(Kind::PullRequest)
                                   << QJsonObject{{u"title"_s, u"t"_s}, {u"base"_s, QString(251, u'a')}}
                                   << u"bad_base"_s;
        QTest::newRow("no checkpoint") << k(Kind::Revert) << QJsonObject() << u"bad_checkpoint"_s;
        QTest::newRow("checkpoint path") << k(Kind::Revert) << QJsonObject{{u"checkpoint"_s, u"../x"_s}}
                                         << u"bad_checkpoint"_s;
        QTest::newRow("checkpoint Polish")
                << k(Kind::Revert) << QJsonObject{{u"checkpoint"_s, u"żółw"_s}} << u"bad_checkpoint"_s;
        // Fine: what each takes.
        QTest::newRow("commit") << k(Kind::Commit)
                                << with(u"files"_s, QJsonArray{u"src/a b/żółw.md"_s, u".gitignore"_s}) << QString();
        QTest::newRow("commit all") << k(Kind::Commit) << ok << QString();
        QTest::newRow("push") << k(Kind::Push) << QJsonObject{{u"worktree"_s, u"/home/ala/x"_s}} << QString();
        QTest::newRow("pr") << k(Kind::PullRequest)
                            << QJsonObject{{u"title"_s, u"Żółw"_s}, {u"base"_s, u"release/2.0"_s}} << QString();
        QTest::newRow("revert") << k(Kind::Revert) << QJsonObject{{u"checkpoint"_s, u"abc-12_x"_s}} << QString();
    }
    void gitRefusals()
    {
        QFETCH(int, kind);
        QFETCH(QJsonObject, body);
        QFETCH(QString, code);
        QString got = u"-"_s;
        const std::optional<RemoteGitRequest> r = RemoteGitRequest::fromJson(RemoteGitRequest::Kind(kind), body, &got);
        QCOMPARE(bool(r), code.isEmpty());
        QCOMPARE(got, code);
        if (r)
            QCOMPARE(r->problem(), QString());
    }

    void gitResults()
    {
        RemoteGitResult r;
        r.message = u"Zatwierdzono"_s;
        r.commit = u"0123abc"_s;
        r.branch = u"brulion/żółw"_s;
        r.remote = u"origin"_s;
        r.url = u"https://github.com/ja/repo/pull/17"_s;
        r.number = 17;
        r.restored = {u"Dom.md"_s};
        r.removed = {u"Nowy.md"_s};
        r.kept = {u"build/x"_s};
        QCOMPARE(RemoteGitResult::fromJson(r.toJson()), r);
        QCOMPARE(RemoteGitResult::fromJson(QJsonObject()), RemoteGitResult{});
        QVERIFY(!RemoteGitResult{}.toJson().contains(u"number"_s));
        // (What is no text left out of a list; at most MaxRemoteChanges.)
        QJsonArray many{3, u"a"_s};
        for (int i = 0; i < MaxRemoteChanges + 5; ++i)
            many.append(u"p%1"_s.arg(i));
        const RemoteGitResult read = RemoteGitResult::fromJson(QJsonObject{{u"restored"_s, many}});
        QCOMPARE(read.restored.size(), MaxRemoteChanges);
        QCOMPARE(read.restored.first(), u"a"_s);
        // (Texts of the host cut to their sizes.)
        const RemoteGitResult cut = RemoteGitResult::fromJson(
                QJsonObject{{u"commit"_s, QString(150, u'a')}, {u"message"_s, QString(2500, u'ż')}, {u"number"_s, -3}});
        QCOMPARE(cut.commit.size(), 100);
        QCOMPARE(cut.message.size(), 2000);
        QCOMPARE(cut.number, 0);
    }

    void pullRequestNumbers_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<int>("number");
        QTest::newRow("github") << u"https://github.com/ja/repo/pull/17"_s << 17;
        QTest::newRow("slash after") << u"https://github.com/ja/repo/pull/9/"_s << 9;
        QTest::newRow("files tab") << u"https://github.com/ja/repo/pull/123/files"_s << 123;
        QTest::newRow("merge request") << u"https://example.com/x/-/merge_requests/4"_s << 0;
        QTest::newRow("no number") << u"https://github.com/ja/repo/pull/abc"_s << 0;
        QTest::newRow("empty") << QString() << 0;
        QTest::newRow("zero") << u"https://github.com/ja/repo/pull/0"_s << 0;
        QTest::newRow("too big") << u"https://github.com/ja/repo/pull/99999999999"_s << 0;
    }
    void pullRequestNumbers()
    {
        QFETCH(QString, url);
        QFETCH(int, number);
        QCOMPARE(pullRequestNumber(url), number);
    }

    void branches()
    {
        // A project's branches there (a new worktree's base picked on the phone): written by the host,
        // read back bounded — no name, a name twice, a name that is no branch's left out.
        QList<RemoteBranch> sent{{u"main"_s, false, true, true},
                                 {u"żółw/rabat"_s, false, false, false},
                                 {u"origin/obcy"_s, true, false, false}};
        QJsonArray list = remoteBranchesJson(sent);
        list.append(QJsonObject{{u"name"_s, u"main"_s}});
        list.append(QJsonObject{{u"name"_s, u"-rf"_s}});
        list.append(QJsonObject{{u"remote"_s, true}});
        QCOMPARE(remoteBranchesFromJson(QJsonObject{{u"branches"_s, list}}), sent);
        QJsonArray many;
        for (int i = 0; i < MaxRemoteBranches + 5; ++i)
            many.append(QJsonObject{{u"name"_s, u"b%1"_s.arg(i)}});
        QCOMPARE(remoteBranchesFromJson(QJsonObject{{u"branches"_s, many}}).size(), MaxRemoteBranches);

        struct Transport : sync::HttpTransport {
            QList<sync::HttpRequest> sent;
            sync::HttpResponse answer;
            void send(const sync::HttpRequest &request, sync::HttpCallback done) override
            {
                sent.append(request);
                done(answer);
            }
        } transport;
        transport.answer.status = 200;
        transport.answer.headers.insert("content-type"_ba, "application/json"_ba);
        transport.answer.body = QJsonDocument(QJsonObject{{u"branches"_s, remoteBranchesJson(sent)}}).toJson();
        QList<RemoteBranch> got;
        RemoteError error{RemoteError::Kind::Network, 0, {}, {}};
        fetchRemoteBranches(&transport, QUrl(u"https://pi.example.ts.net:8790"_s), u"bat_x"_s, u"zolw"_s,
                            [&](const QList<RemoteBranch> &b, const RemoteError &e) {
                                got = b;
                                error = e;
                            });
        QCOMPARE(transport.sent.first().method, "GET"_ba);
        QVERIFY(transport.sent.first().url.path().endsWith(u"/projects/zolw/branches"_s));
        QVERIFY(!error.failed());
        QCOMPARE(got, sent);
        transport.answer.status = 404;
        transport.answer.body = R"({"error":{"code":"not_found","message":"Not found"}})";
        fetchRemoteBranches(&transport, QUrl(u"https://pi.example.ts.net:8790"_s), u"zolw"_s, u"zolw"_s,
                            [&](const QList<RemoteBranch> &b, const RemoteError &e) {
                                got = b;
                                error = e;
                            });
        QCOMPARE(error.status, 404);
        QVERIFY(got.isEmpty());
    }

    void gitSent()
    {
        // The device's side: sent to the endpoint with its body; an answer or an error read.
        struct Transport : sync::HttpTransport {
            QList<sync::HttpRequest> sent;
            sync::HttpResponse answer;
            void send(const sync::HttpRequest &request, sync::HttpCallback done) override
            {
                sent.append(request);
                done(answer);
            }
        } transport;
        transport.answer.status = 200;
        transport.answer.headers.insert("content-type"_ba, "application/json"_ba);
        transport.answer.body = R"({"commit":"abc","branch":"main","message":"Committed abc"})";
        const QUrl base(u"https://pi.example.ts.net:8790"_s);
        RemoteGitResult result;
        RemoteError error{RemoteError::Kind::Network, 0, {}, {}};
        const auto ask = [&](const RemoteGitRequest &request) {
            remoteGit(&transport, base, u"bat_x"_s, u"zolw"_s, request,
                      [&](const RemoteGitResult &r, const RemoteError &e) {
                          result = r;
                          error = e;
                      });
        };
        RemoteGitRequest commit;
        commit.message = u"Napraw żółwia"_s;
        ask(commit);
        QCOMPARE(transport.sent.size(), 1);
        QCOMPARE(transport.sent.first().method, "POST"_ba);
        QVERIFY2(transport.sent.first().url.path().endsWith(u"/projects/zolw/commit"_s),
                 qPrintable(transport.sent.first().url.path()));
        QCOMPARE(QJsonDocument::fromJson(transport.sent.first().body).object(), commit.toJson());
        QVERIFY(transport.sent.first().timeoutMs > 60000); // (the repository's hooks run)
        QVERIFY(!error.failed());
        QCOMPARE(result.commit, u"abc"_s);
        QCOMPARE(result.branch, u"main"_s);
        QCOMPARE(result.message, u"Committed abc"_s);
        // A pull request: its number from the address when the host gave none.
        transport.answer.body = R"({"url":"https://github.com/ja/repo/pull/21"})";
        RemoteGitRequest pr;
        pr.kind = RemoteGitRequest::Kind::PullRequest;
        pr.title = u"Tytuł"_s;
        ask(pr);
        QVERIFY(transport.sent.last().url.path().endsWith(u"/projects/zolw/pr"_s));
        QVERIFY(transport.sent.last().timeoutMs > 120000); // (a push and gh over the network)
        QCOMPARE(result.number, 21);
        // The host's refusal.
        transport.answer.status = 409;
        transport.answer.body = R"({"error":{"code":"git_failed","message":"Nothing to commit"}})";
        ask(commit);
        QCOMPARE(error.kind, RemoteError::Kind::Refused);
        QCOMPARE(error.code, u"git_failed"_s);
        QCOMPARE(error.detail, u"Nothing to commit"_s);
        // Refused here: never sent.
        const qsizetype before = transport.sent.size();
        commit.message = u"   "_s;
        error = {};
        ask(commit);
        QCOMPARE(transport.sent.size(), before);
        QCOMPARE(error.kind, RemoteError::Kind::Refused);
        QCOMPARE(error.status, 400);
        QCOMPARE(error.code, u"bad_message"_s);
    }
};

QTEST_GUILESS_MAIN(TestAcpCodeHost)
#include "tst_acp_codehost.moc"
