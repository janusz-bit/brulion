// The conversations the desktop app runs, followed and steered from another device (core
// acp/appchats.h): their JSON both ways — what the app writes read back unchanged, odd entries
// dropped (no path, a path out of the AI folder or with "..", a state nobody knows, a path twice),
// texts bounded (an answer kept by its end, a surrogate pair never split, options at most
// MaxQuestionOptions and none twice) — and the device's requests: the long poll's address and time,
// a path in the query percent-encoded, the modes of sending, the host's errors (503 app_unavailable,
// 409 busy) and answers that are not the API's; the agent's options and held messages of a detail,
// pictures sent with a message (bounded, their names only file names), chats/option and chats/resume.
#include "acp/appchats.h"

#include <QJsonArray>
#include <QJsonDocument>
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

    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        pending.append({request, std::move(done)});
    }
    Pending take()
    {
        Q_ASSERT(!pending.isEmpty());
        return pending.takeFirst();
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
};

const QUrl Base(u"http://nixos.tail1ea20e.ts.net:8790"_s);
const QString Path = u"AI/2026-11-26 Popraw testy żółwia.md"_s;

RemoteChats sample()
{
    RemoteChats chats;
    chats.revision = 41;
    RemoteChat running;
    running.path = Path;
    running.name = u"Popraw testy żółwia"_s;
    running.partner = u"acp:claude"_s;
    running.partnerName = u"Claude Code"_s;
    running.state = u"asking"_s;
    running.status = u"Czytam „Dom”…"_s;
    running.question = u"Uruchomić ctest?"_s;
    running.project = u"brulion"_s;
    running.worktree = u"/home/ala/.brulion/worktrees/brulion/popraw-testy"_s;
    running.branch = u"t3/popraw-testy"_s;
    running.messages = 3;
    running.waiting = 1;
    RemoteChat ended;
    ended.held = true; // (stopped there: its message waits)
    ended.waiting = 1;
    ended.path = u"AI/2026-11-25 Plan wyjazdu.md"_s;
    ended.name = u"Plan wyjazdu"_s;
    ended.state = u"idle"_s;
    ended.messages = 8;
    ended.ended = u"2026-11-26T09:12:00"_s;
    chats.chats = {running, ended};
    RemoteChatDetail detail;
    detail.path = Path;
    detail.state = u"asking"_s;
    detail.status = u"Uruchamiam testy"_s;
    detail.partial = u"Zmieniłem **trzy** pliki.\n\n- ąćęłńóśźż"_s;
    detail.thoughts = u"…więc najpierw testy"_s;
    detail.tools = {{u"Read src/turtle.cpp"_s, u"completed"_s, u"read"_s}, {u"ctest -R turtle"_s, u"pending"_s, u"execute"_s}};
    detail.plan = {{u"Przeczytać testy"_s, u"completed"_s}, {u"Poprawić"_s, u"in_progress"_s}};
    detail.question.text = u"Uruchomić ctest?"_s;
    detail.question.details = u"ctest -R turtle --output-on-failure"_s;
    detail.question.options = {u"Allow once"_s, u"Always allow"_s, u"Reject"_s};
    detail.question.file = u"src/turtle.cpp"_s;
    detail.question.oldText = u"int a;\n"_s;
    detail.question.newText = u"int a = 0;\n"_s;
    detail.waiting = {u"A potem zrób commit"_s};
    detail.held = true;
    RemoteChatOption model;
    model.id = u"model"_s;
    model.name = u"Model"_s;
    model.description = u"Który model odpowiada"_s;
    model.current = u"opus"_s;
    model.currentName = u"Opus 5.5"_s;
    model.choices = {{u"opus"_s, u"Opus 5.5"_s, u"Claude"_s}, {u"sonnet"_s, u"Sonnet 5.5"_s, QString()}};
    RemoteChatOption fast;
    fast.id = u"fast"_s;
    fast.name = u"Szybko"_s;
    fast.boolean = true;
    fast.current = u"false"_s;
    detail.options = {model, fast};
    detail.messages = 3;
    chats.detail = detail;
    return chats;
}

} // namespace

class TestAcpAppChats : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void roundTrip()
    {
        const RemoteChats chats = sample();
        const std::optional<RemoteChats> read = RemoteChats::fromJson(chats.toJson());
        QVERIFY(read);
        QCOMPARE(*read, chats);
        QVERIFY(read->chats.at(0).active());
        QVERIFY(!read->chats.at(1).active());
        // Through bytes too (as the host passes it on).
        const QJsonObject again = QJsonDocument::fromJson(QJsonDocument(chats.toJson()).toJson()).object();
        QCOMPARE(*RemoteChats::fromJson(again), chats);
    }

    void noRevision()
    {
        QVERIFY(!RemoteChats::fromJson(QJsonObject{}));
        QVERIFY(!RemoteChats::fromJson(QJsonObject{{u"revision"_s, u"7"_s}}));
        QVERIFY(!RemoteChats::fromJson(QJsonObject{{u"revision"_s, -1}}));
        const std::optional<RemoteChats> empty = RemoteChats::fromJson(QJsonObject{{u"revision"_s, 0}});
        QVERIFY(empty);
        QVERIFY(empty->chats.isEmpty());
        QVERIFY(!empty->detail);
    }

    void paths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("ok");
        QTest::newRow("conversation") << Path << true;
        QTest::newRow("lower case folder") << u"ai/Rozmowa.md"_s << true;
        QTest::newRow("deeper") << u"AI/Wątki/Rozmowa.md"_s << true;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("no .md") << u"AI/Rozmowa"_s << false;
        QTest::newRow("another folder") << u"Journal/2026-11-26.md"_s << false;
        QTest::newRow("the folder alone") << u"AI.md"_s << false;
        QTest::newRow("absolute") << u"/AI/Rozmowa.md"_s << false;
        QTest::newRow("dot dot") << u"AI/../Sekrety.md"_s << false;
        QTest::newRow("dot") << u"AI/./Rozmowa.md"_s << false;
        QTest::newRow("empty part") << u"AI//Rozmowa.md"_s << false;
        QTest::newRow("backslash") << u"AI\\..\\Sekrety.md"_s << false;
        QTest::newRow("nul") << (u"AI/Roz"_s + QChar(u'\0') + u"mowa.md"_s) << false;
        QTest::newRow("too long") << (u"AI/"_s + QString(5000, u'ż') + u".md"_s) << false;
    }
    void paths()
    {
        QFETCH(QString, path);
        QFETCH(bool, ok);
        QCOMPARE(isRemoteChatPath(path), ok);
    }

    void oddEntriesDropped()
    {
        QJsonArray list;
        list.append(QJsonObject{{u"path"_s, Path}, {u"state"_s, u"running"_s}});
        list.append(QJsonObject{{u"path"_s, Path.toUpper()}, {u"state"_s, u"running"_s}}); // (the same page)
        list.append(QJsonObject{{u"path"_s, u"Notatki/Hasła.md"_s}, {u"state"_s, u"running"_s}});
        list.append(QJsonObject{{u"path"_s, u"AI/Inna.md"_s}, {u"state"_s, u"thinking"_s}});
        list.append(QJsonObject{{u"state"_s, u"running"_s}});
        list.append(u"AI/Napis.md"_s);
        list.append(QJsonObject{{u"path"_s, u"AI/Druga.md"_s}, {u"state"_s, u"failed"_s}, {u"messages"_s, -4}});
        const std::optional<RemoteChats> read =
                RemoteChats::fromJson(QJsonObject{{u"revision"_s, 3}, {u"chats"_s, list}});
        QVERIFY(read);
        QCOMPARE(read->chats.size(), 2);
        QCOMPARE(read->chats.at(0).path, Path);
        QCOMPARE(read->chats.at(0).name, u"2026-11-26 Popraw testy żółwia"_s); // (none given: the file's)
        QCOMPARE(read->chats.at(1).path, u"AI/Druga.md"_s);
        QCOMPARE(read->chats.at(1).messages, 0);
        // A detail of another folder: none.
        const std::optional<RemoteChats> detail = RemoteChats::fromJson(QJsonObject{
                {u"revision"_s, 3}, {u"detail"_s, QJsonObject{{u"path"_s, u"Hasła.md"_s}, {u"state"_s, u"idle"_s}}}});
        QVERIFY(detail);
        QVERIFY(!detail->detail);
    }

    void manyChats()
    {
        QJsonArray list;
        for (int i = 0; i < MaxRemoteChats + 20; ++i)
            list.append(QJsonObject{{u"path"_s, u"AI/Rozmowa %1.md"_s.arg(i)}, {u"state"_s, u"running"_s}});
        const std::optional<RemoteChats> read =
                RemoteChats::fromJson(QJsonObject{{u"revision"_s, 1}, {u"chats"_s, list}});
        QCOMPARE(read->chats.size(), MaxRemoteChats);
    }

    void bounded()
    {
        // An answer longer than MaxLiveTextChars: its end, said to be cut; a surrogate pair whole.
        const QString emoji = u"😀"_s;
        QString partial = u"początek "_s + QString(MaxLiveTextChars, u'x');
        partial.insert(partial.size() - MaxLiveTextChars, emoji); // (the cut falls inside the pair)
        QJsonArray options;
        for (int i = 0; i < 12; ++i)
            options.append(u"Opcja %1"_s.arg(i % 10));
        const QJsonObject o{{u"path"_s, Path},
                            {u"state"_s, u"asking"_s},
                            {u"partial"_s, partial},
                            {u"question"_s, QJsonObject{{u"text"_s, u"Wolno?\nreszta"_s}, {u"options"_s, options}}}};
        const std::optional<RemoteChatDetail> d = remoteChatDetailFromJson(o);
        QVERIFY(d);
        QVERIFY(d->partialCut);
        QVERIFY(d->partial.size() <= MaxLiveTextChars);
        QVERIFY(!d->partial.at(0).isLowSurrogate());
        QCOMPARE(d->question.options.size(), MaxQuestionOptions);
        QCOMPARE(d->question.options.first(), u"Opcja 0"_s);
        QCOMPARE(d->question.text, u"Wolno? reszta"_s); // (one line)
        // A question without options is none.
        const std::optional<RemoteChatDetail> none = remoteChatDetailFromJson(QJsonObject{
                {u"path"_s, Path}, {u"state"_s, u"running"_s}, {u"question"_s, QJsonObject{{u"text"_s, u"?"_s}}}});
        QVERIFY(none->question.isEmpty());
        QVERIFY(none->question.text.isEmpty());
    }

    void keepEndOfText()
    {
        bool cut = true;
        QCOMPARE(keepEnd(u"żółw"_s, 10, &cut), u"żółw"_s);
        QVERIFY(!cut);
        QCOMPARE(keepEnd(u"zażółć gęślą"_s, 5, &cut), u"gęślą"_s);
        QVERIFY(cut);
        QCOMPARE(keepEnd(u"a😀b"_s, 2), u"b"_s); // (half a pair left out)
    }

    void fetchChats()
    {
        FakeHost host;
        std::optional<RemoteChats> got;
        RemoteError error;
        fetchRemoteChats(&host, Base, u"bat_x"_s, 40, 99, Path, [&](const RemoteChats &c, const RemoteError &e) {
            got = c;
            error = e;
        });
        FakeHost::Pending p = host.take();
        QCOMPARE(p.request.method, "GET"_ba);
        QCOMPARE(p.endpoint(), u"chats"_s);
        QCOMPARE(p.query().queryItemValue(u"since"_s), u"40"_s);
        QCOMPARE(p.query().queryItemValue(u"wait"_s), QString::number(RemoteWaitSeconds)); // (at most)
        QCOMPARE(p.query().queryItemValue(u"path"_s, QUrl::FullyDecoded), Path);
        QVERIFY(p.request.timeoutMs > RemoteWaitSeconds * 1000);
        QVERIFY(p.request.headers.contains(std::pair(QByteArray("Authorization"), QByteArray("Bearer bat_x"))));
        p.done(FakeHost::json(200, sample().toJson()));
        QVERIFY(!error.failed());
        QCOMPARE(*got, sample());

        // No path, no wait: neither in the query beyond wait=0.
        fetchRemoteChats(&host, Base, u"bat_x"_s, -1, 0, QString(), [&](const RemoteChats &c, const RemoteError &e) {
            got = c;
            error = e;
        });
        p = host.take();
        QCOMPARE(p.query().queryItemValue(u"since"_s), u"-1"_s);
        QCOMPARE(p.query().queryItemValue(u"wait"_s), u"0"_s);
        QVERIFY(!p.query().hasQueryItem(u"path"_s));
        // The app is not running: the host's error.
        p.done(FakeHost::error(503, u"app_unavailable"_s, u"The app does not answer"_s));
        QCOMPARE(error.kind, RemoteError::Kind::Server);
        QCOMPARE(error.code, u"app_unavailable"_s);

        // An answer that is not the API's.
        fetchRemoteChats(&host, Base, u"bat_x"_s, 0, 5, QString(), [&](const RemoteChats &c, const RemoteError &e) {
            got = c;
            error = e;
        });
        host.take().done(FakeHost::json(200, QJsonObject{{u"chats"_s, QJsonArray()}}));
        QCOMPARE(error.kind, RemoteError::Kind::Malformed);
    }

    void page()
    {
        FakeHost host;
        QString text;
        RemoteError error;
        fetchRemoteChatPage(&host, Base, u"bat_x"_s, Path, [&](const QString &t, const RemoteError &e) {
            text = t;
            error = e;
        });
        FakeHost::Pending p = host.take();
        QCOMPARE(p.endpoint(), u"chats/page"_s);
        QCOMPARE(p.query().queryItemValue(u"path"_s, QUrl::FullyDecoded), Path);
        QVERIFY(p.request.maxBodyBytes >= MaxRemotePageBytes);
        p.done(FakeHost::json(200, QJsonObject{{u"path"_s, Path}, {u"text"_s, u"- 🧑 Cześć\n"_s}}));
        QVERIFY(!error.failed());
        QCOMPARE(text, u"- 🧑 Cześć\n"_s);
        fetchRemoteChatPage(&host, Base, u"bat_x"_s, Path, [&](const QString &t, const RemoteError &e) {
            text = t;
            error = e;
        });
        host.take().done(FakeHost::json(200, QJsonObject{{u"path"_s, Path}}));
        QCOMPARE(error.kind, RemoteError::Kind::Malformed);
    }

    void send_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<QString>("expected");
        QTest::newRow("now") << int(SendMode::Now) << QString();
        QTest::newRow("queue") << int(SendMode::Queue) << u"queue"_s;
        QTest::newRow("steer") << int(SendMode::Steer) << u"steer"_s;
    }
    void send()
    {
        QFETCH(int, mode);
        QFETCH(QString, expected);
        FakeHost host;
        std::optional<RemoteError> error;
        sendRemoteChat(&host, Base, u"bat_x"_s, Path, u"A teraz commit, proszę"_s, SendMode(mode), {},
                       [&](const RemoteError &e) { error = e; });
        FakeHost::Pending p = host.take();
        QCOMPARE(p.request.method, "POST"_ba);
        QCOMPARE(p.endpoint(), u"chats/send"_s);
        QCOMPARE(p.body().value(u"path"_s).toString(), Path);
        QCOMPARE(p.body().value(u"text"_s).toString(), u"A teraz commit, proszę"_s);
        QCOMPARE(p.body().value(u"mode"_s).toString(), expected);
        QVERIFY(!p.body().contains(u"pictures"_s)); // (none: not named)
        p.done(FakeHost::error(409, u"busy"_s, u"Its turn runs"_s));
        QVERIFY(error);
        QCOMPARE(error->kind, RemoteError::Kind::Refused);
        QCOMPARE(error->code, u"busy"_s);
    }

    void startStopAnswer()
    {
        FakeHost host;
        QString path;
        std::optional<RemoteError> error;
        startRemoteThread(&host, Base, u"bat_x"_s, u"brulion"_s, u"Napraw testy"_s, u"acp:claude"_s, QString(),
                          QString(), {}, [&](const QString &p, const RemoteError &e) {
                              path = p;
                              error = e;
                          });
        FakeHost::Pending p = host.take();
        QCOMPARE(p.endpoint(), u"chats/start"_s);
        QCOMPARE(p.body(), (QJsonObject{{u"project"_s, u"brulion"_s},
                                         {u"text"_s, u"Napraw testy"_s},
                                         {u"partner"_s, u"acp:claude"_s}}));
        p.done(FakeHost::json(200, QJsonObject{{u"path"_s, Path}}));
        QCOMPARE(path, Path);
        QVERIFY(!error->failed());
        // A path that is no conversation's: refused as malformed.
        startRemoteThread(&host, Base, u"bat_x"_s, u"brulion"_s, u"x"_s, QString(), u"/w"_s, u"t3/x"_s, {},
                          [&](const QString &p, const RemoteError &e) {
                              path = p;
                              error = e;
                          });
        p = host.take();
        QCOMPARE(p.body().value(u"worktree"_s).toString(), u"/w"_s);
        QCOMPARE(p.body().value(u"branch"_s).toString(), u"t3/x"_s);
        p.done(FakeHost::json(200, QJsonObject{{u"path"_s, u"../etc/passwd"_s}}));
        QCOMPARE(error->kind, RemoteError::Kind::Malformed);
        QVERIFY(path.isEmpty());

        stopRemoteChat(&host, Base, u"bat_x"_s, Path, [&](const RemoteError &e) { error = e; });
        p = host.take();
        QCOMPARE(p.endpoint(), u"chats/stop"_s);
        QCOMPARE(p.body(), (QJsonObject{{u"path"_s, Path}}));
        p.done(FakeHost::json(200, QJsonObject{{u"stopped"_s, true}}));
        QVERIFY(!error->failed());

        answerRemoteChat(&host, Base, u"bat_x"_s, Path, u"Allow once"_s, [&](const RemoteError &e) { error = e; });
        p = host.take();
        QCOMPARE(p.endpoint(), u"chats/answer"_s);
        QCOMPARE(p.body(), (QJsonObject{{u"path"_s, Path}, {u"option"_s, u"Allow once"_s}}));
        p.done(FakeHost::error(409, u"no_question"_s, u"Nothing is asked"_s));
        QCOMPARE(error->code, u"no_question"_s);
    }

    void optionsBounded()
    {
        // An option without an id is none; one of no choices and not boolean is none too; choices
        // at most MaxOptionChoices, none twice; options at most MaxLiveOptions.
        QJsonArray choices;
        for (int i = 0; i < MaxOptionChoices + 5; ++i)
            choices.append(QJsonObject{{u"value"_s, u"m%1"_s.arg(i % (MaxOptionChoices + 2))}, {u"name"_s, u"Model %1"_s.arg(i)}});
        choices.append(QJsonObject{{u"name"_s, u"bez wartości"_s}});
        QJsonArray options;
        options.append(QJsonObject{{u"name"_s, u"Bez id"_s}, {u"choices"_s, choices}});
        options.append(QJsonObject{{u"id"_s, u"pusta"_s}, {u"name"_s, u"Pusta"_s}});
        for (int i = 0; i < MaxLiveOptions + 3; ++i)
            options.append(QJsonObject{{u"id"_s, u"o%1"_s.arg(i)}, {u"name"_s, u"Opcja\n%1"_s.arg(i)}, {u"choices"_s, choices}});
        const std::optional<RemoteChatDetail> d = remoteChatDetailFromJson(
                QJsonObject{{u"path"_s, Path}, {u"state"_s, u"idle"_s}, {u"options"_s, options}});
        QVERIFY(d);
        QCOMPARE(d->options.size(), MaxLiveOptions);
        QCOMPARE(d->options.first().id, u"o0"_s);
        QCOMPARE(d->options.first().name, u"Opcja 0"_s); // (one line)
        QCOMPARE(d->options.first().choices.size(), MaxOptionChoices);
        QVERIFY(!remoteChatOptionFromJson(QJsonObject{{u"id"_s, u"x"_s}}));
        QVERIFY(remoteChatOptionFromJson(QJsonObject{{u"id"_s, u"x"_s}, {u"boolean"_s, true}}));
    }

    void pictures_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<int>("count"); // -1: refused
        const QString jpeg = QString::fromLatin1(QByteArray("\xff\xd8\xff zdjęcie").toBase64());
        const auto one = [](const QString &name, const QJsonValue &data) {
            return QJsonObject{{u"name"_s, name}, {u"data"_s, data}};
        };
        QTest::newRow("none") << QJsonValue() << 0;
        QTest::newRow("null") << QJsonValue(QJsonValue::Null) << 0;
        QTest::newRow("empty list") << QJsonValue(QJsonArray()) << 0;
        QTest::newRow("one") << QJsonValue(QJsonArray{one(u"Paragon żabki.jpg"_s, jpeg)}) << 1;
        QTest::newRow("png upper case") << QJsonValue(QJsonArray{one(u"EKRAN.PNG"_s, jpeg)}) << 1;
        QTest::newRow("not a list") << QJsonValue(u"x"_s) << -1;
        QTest::newRow("no object") << QJsonValue(QJsonArray{u"a.jpg"_s}) << -1;
        QTest::newRow("no picture") << QJsonValue(QJsonArray{one(u"skrypt.sh"_s, jpeg)}) << -1;
        QTest::newRow("no name") << QJsonValue(QJsonArray{one(QString(), jpeg)}) << -1;
        QTest::newRow("no data") << QJsonValue(QJsonArray{one(u"a.jpg"_s, QString())}) << -1;
        QTest::newRow("data no string") << QJsonValue(QJsonArray{one(u"a.jpg"_s, 7)}) << -1;
        QTest::newRow("not base64") << QJsonValue(QJsonArray{one(u"a.jpg"_s, u"!!!*"_s)}) << -1;
        QJsonArray many;
        for (int i = 0; i <= MaxRemotePictures; ++i)
            many.append(one(u"a%1.jpg"_s.arg(i), jpeg));
        QTest::newRow("too many") << QJsonValue(many) << -1;
        const QString big = QString::fromLatin1(QByteArray(MaxRemotePictureBytes + 1, 'x').toBase64());
        QTest::newRow("one too large") << QJsonValue(QJsonArray{one(u"a.jpg"_s, big)}) << -1;
        const QString half = QString::fromLatin1(QByteArray(MaxRemotePictureBytes, 'x').toBase64());
        QTest::newRow("two at the limit") << QJsonValue(QJsonArray{one(u"a.jpg"_s, half), one(u"b.jpg"_s, half)}) << 2;
        QTest::newRow("all too large")
                << QJsonValue(QJsonArray{one(u"a.jpg"_s, half), one(u"b.jpg"_s, half), one(u"c.jpg"_s, jpeg)}) << -1;
    }
    void pictures()
    {
        QFETCH(QJsonValue, value);
        QFETCH(int, count);
        QString why;
        const std::optional<QList<RemotePicture>> read = remotePicturesFromJson(value, &why);
        QCOMPARE(read ? int(read->size()) : -1, count);
        QCOMPARE(why.isEmpty(), count >= 0);
    }

    void picturesRoundTrip()
    {
        const QList<RemotePicture> sent{{u"Paragon żabki.jpg"_s, QByteArray("\xff\xd8\xff\x00\x01", 5)},
                                        {u"ekran.png"_s, QByteArray("\x89PNG")}};
        QCOMPARE(remotePicturesFromJson(toJson(sent)), std::optional(sent));
        // A name's folders are dropped: only its file name is taken.
        const QJsonArray odd{QJsonObject{{u"name"_s, u"../../.ssh/id.jpg"_s},
                                         {u"data"_s, QString::fromLatin1(QByteArray("x").toBase64())}},
                             QJsonObject{{u"name"_s, u"C:\\Users\\a.jpg"_s},
                                         {u"data"_s, QString::fromLatin1(QByteArray("y").toBase64())}}};
        const std::optional<QList<RemotePicture>> read = remotePicturesFromJson(odd);
        QVERIFY(read);
        QCOMPARE(read->at(0).name, u"id.jpg"_s);
        QCOMPARE(read->at(1).name, u"a.jpg"_s);
    }

    void sendWithPictures()
    {
        FakeHost host;
        std::optional<RemoteError> error;
        const QList<RemotePicture> pictures{{u"zdjęcie.jpg"_s, QByteArray("\xff\xd8")}};
        sendRemoteChat(&host, Base, u"bat_x"_s, Path, u"Co tu widać?"_s, SendMode::Queue, pictures,
                       [&](const RemoteError &e) { error = e; });
        FakeHost::Pending p = host.take();
        QCOMPARE(remotePicturesFromJson(p.body().value(u"pictures"_s)), std::optional(pictures));
        QCOMPARE(p.body().value(u"mode"_s).toString(), u"queue"_s);
        p.done(FakeHost::json(200, QJsonObject{{u"path"_s, Path}}));
        QVERIFY(!error->failed());
        startRemoteThread(&host, Base, u"bat_x"_s, u"brulion"_s, u"Jak na zdjęciu"_s, QString(), QString(), QString(),
                          pictures, [&](const QString &, const RemoteError &e) { error = e; });
        p = host.take();
        QCOMPARE(p.endpoint(), u"chats/start"_s);
        QCOMPARE(remotePicturesFromJson(p.body().value(u"pictures"_s)), std::optional(pictures));
    }

    void optionAndResume()
    {
        FakeHost host;
        std::optional<RemoteError> error;
        setRemoteChatOption(&host, Base, u"bat_x"_s, Path, u"model"_s, u"sonnet"_s,
                            [&](const RemoteError &e) { error = e; });
        FakeHost::Pending p = host.take();
        QCOMPARE(p.request.method, "POST"_ba);
        QCOMPARE(p.endpoint(), u"chats/option"_s);
        QCOMPARE(p.body(), (QJsonObject{{u"path"_s, Path}, {u"option"_s, u"model"_s}, {u"value"_s, u"sonnet"_s}}));
        p.done(FakeHost::error(400, u"bad_option"_s, u"No such option"_s));
        QCOMPARE(error->code, u"bad_option"_s);

        resumeRemoteChat(&host, Base, u"bat_x"_s, Path, [&](const RemoteError &e) { error = e; });
        p = host.take();
        QCOMPARE(p.endpoint(), u"chats/resume"_s);
        QCOMPARE(p.body(), (QJsonObject{{u"path"_s, Path}}));
        p.done(FakeHost::json(200, QJsonObject{{u"resumed"_s, true}}));
        QVERIFY(!error->failed());
    }
};

QTEST_GUILESS_MAIN(TestAcpAppChats)
#include "tst_acp_appchats.moc"
