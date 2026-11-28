// The CalDAV client against FakeCalDav: discovery (well-known, principal, home set, a calendar's own
// address, the namespace prefixes servers use), Basic and Digest authentication, a wrong password,
// sync-collection with removed members and a refused token, the ETag fallback of a server without
// it, calendar-multiget and its GET fallback, conditional writes, malformed answers, and where the
// password may travel.
#include "caldav/client.h"
#include "fakecaldav.h"

#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace caldavtest;
using namespace Qt::StringLiterals;

namespace {

QByteArray todo(const QString &uid, QStringView summary)
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:"_s + uid + u"\r\nSUMMARY:"_s + summary.toString()
            + u"\r\nEND:VTODO\r\nEND:VCALENDAR\r\n"_s)
            .toUtf8();
}

template<typename T>
T wait(const std::function<void(std::function<void(const T &)>)> &call)
{
    std::optional<T> result;
    call([&result](const T &value) { result = value; });
    QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 10'000);
    return result.value_or(T{});
}

Discovery discover(CalDavClient &client)
{
    std::optional<Discovery> result;
    client.discover([&result](const Discovery &d) { result = d; });
    for (int i = 0; i < 1000 && !result; ++i)
        QTest::qWait(5);
    return result.value_or(Discovery{});
}

ChangeSet changes(CalDavClient &client, const QUrl &calendar, const QString &token)
{
    std::optional<ChangeSet> result;
    client.changes(calendar, token, [&result](const ChangeSet &c) { result = c; });
    for (int i = 0; i < 1000 && !result; ++i)
        QTest::qWait(5);
    return result.value_or(ChangeSet{});
}

QList<RemoteResource> fetch(CalDavClient &client, const QUrl &calendar, const QStringList &paths,
                            sync::WebDavError *error = nullptr)
{
    std::optional<QList<RemoteResource>> result;
    client.fetch(calendar, paths, [&result, error](const QList<RemoteResource> &r, const sync::WebDavError &e) {
        result = r;
        if (error)
            *error = e;
    });
    for (int i = 0; i < 1000 && !result; ++i)
        QTest::qWait(5);
    return result.value_or(QList<RemoteResource>{});
}

WriteResult put(CalDavClient &client, const QString &path, const QByteArray &data, const QString &ifMatch)
{
    std::optional<WriteResult> result;
    client.put(path, data, ifMatch, [&result](const WriteResult &r) { result = r; });
    for (int i = 0; i < 1000 && !result; ++i)
        QTest::qWait(5);
    return result.value_or(WriteResult{});
}

QStringList paths(const QList<RemoteResource> &list)
{
    QStringList out;
    for (const RemoteResource &r : list)
        out.append(r.path.section(u'/', -1));
    out.sort();
    return out;
}

FakeCalDav::Calendar calendarNamed(const QString &name, const QString &color, const QStringList &components = {})
{
    FakeCalDav::Calendar c;
    c.name = name;
    c.color = color;
    if (!components.isEmpty())
        c.components = components;
    return c;
}

} // namespace

class TestCalDavClient : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void discovery_data();
    void discovery();
    void calendarAddress();
    void wrongPassword();
    void noCalDav();
    void incrementalChanges();
    void refusedToken();
    void noSyncCollection();
    void multigetFallback();
    void conditionalWrites();
    void malformedAnswers();
    void credentialsStayOnTheSite_data();
    void credentialsStayOnTheSite();
};

void TestCalDavClient::discovery_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("digest");
    QTest::addColumn<QString>("prefix");
    QTest::newRow("server root, basic") << u"https://dav.example.pl"_s << false << u"d"_s;
    QTest::newRow("dav root, digest") << u"https://dav.example.pl/dav/"_s << true << u"d"_s;
    QTest::newRow("principal, capital prefix") << u"https://dav.example.pl/dav/principals/jan/"_s << false << u"D"_s;
    QTest::newRow("no prefix") << u"https://dav.example.pl/"_s << true << QString();
}

void TestCalDavClient::discovery()
{
    QFETCH(QString, address);
    QFETCH(bool, digest);
    QFETCH(QString, prefix);
    FakeCalDav::Options o;
    o.digest = digest;
    o.prefix = prefix;
    o.wellKnown = address == u"https://dav.example.pl"_s;
    FakeCalDav server(o);
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, u"#1e88e5"_s));
    server.addCalendar(u"zakupy"_s, calendarNamed(u"Zakupy & dom"_s, u"#43a047"_s, {u"VTODO"_s}));
    FakeCalDav::Calendar shared = calendarNamed(u"Święta"_s, QString(), {u"VEVENT"_s});
    shared.readOnly = true;
    server.addCalendar(u"swieta"_s, shared);
    CalDavClient client(&server, QUrl(address), server.options().user, server.options().password);
    const Discovery d = discover(client);
    QVERIFY2(!d.error.isError(), qPrintable(d.error.message));
    QCOMPARE(d.principalUrl, server.url(server.principalPath()));
    QCOMPARE(d.homeUrl, server.url(server.homePath()));
    // The account's own calendar addresses (RFC 6638 calendar-user-address-set): how invitations
    // name it — e-mails without mailto: in lower case, the principal's URI as written.
    QCOMPARE(d.addresses, (QStringList{u"jan.kowalski@example.pl"_s, u"jan@example.pl"_s, server.principalPath()}));
    QCOMPARE(d.calendars.size(), 3); // the inbox is no calendar
    QCOMPARE(d.calendars.at(0).name, u"Praca"_s);
    QCOMPARE(d.calendars.at(0).color, u"#1e88e5"_s);
    QVERIFY(d.calendars.at(0).holdsEvents() && d.calendars.at(0).holdsTasks());
    QCOMPARE(d.calendars.at(1).name, u"Święta"_s);
    QVERIFY(d.calendars.at(1).readOnly);
    QVERIFY(!d.calendars.at(1).holdsTasks());
    QCOMPARE(d.calendars.at(2).name, u"Zakupy & dom"_s);
    QVERIFY(!d.calendars.at(2).holdsEvents());
    QVERIFY(!d.calendars.at(2).syncToken.isEmpty());
    QVERIFY(!d.calendars.at(2).ctag.isEmpty());
    // Every request after the challenge was authorized.
    for (const FakeCalDav::LoggedRequest &r : std::as_const(server.requests)) {
        if (r.path != u"/.well-known/caldav")
            QVERIFY(r.authorized || digest);
    }
}

void TestCalDavClient::calendarAddress()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, u"#1e88e5"_s));
    CalDavClient client(&server, QUrl(server.calendarUrl(u"praca"_s)), server.options().user, server.options().password);
    const Discovery d = discover(client);
    QVERIFY(!d.error.isError());
    QCOMPARE(d.calendars.size(), 1);
    QCOMPARE(d.calendars.first().url, server.calendarUrl(u"praca"_s));
}

void TestCalDavClient::wrongPassword()
{
    for (const bool digest : {false, true}) {
        FakeCalDav::Options o;
        o.digest = digest;
        FakeCalDav server(o);
        CalDavClient client(&server, QUrl(u"https://dav.example.pl/dav/"_s), u"jan"_s, u"złe hasło"_s);
        const Discovery d = discover(client);
        QVERIFY(d.error.is(sync::WebDavErrorCode::Auth));
        QVERIFY(server.requests.size() <= 3); // no endless retries
    }
}

void TestCalDavClient::noCalDav()
{
    FakeCalDav::Options o;
    o.wellKnown = false;
    FakeCalDav server(o);
    server.hook = [](const sync::HttpRequest &, const QString &path) -> std::optional<sync::HttpResponse> {
        if (path == u"/" || path.isEmpty()) {
            sync::HttpResponse r;
            r.status = 200;
            r.body = "<html><body>Witaj na stronie</body></html>";
            return r;
        }
        return std::nullopt;
    };
    CalDavClient client(&server, QUrl(u"https://dav.example.pl"_s), u"jan"_s, server.options().password);
    const Discovery d = discover(client);
    QVERIFY(d.error.is(sync::WebDavErrorCode::NotFound));
    // A host that does not answer: a network error.
    CalDavClient nowhere(&server, QUrl(u"https://nieistnieje.example.com"_s), u"jan"_s, u"x"_s);
    QVERIFY(discover(nowhere).error.is(sync::WebDavErrorCode::Network));
}

void TestCalDavClient::incrementalChanges()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, {}));
    server.putMember(u"praca"_s, u"a.ics"_s, todo(u"a"_s, u"Pierwsze"));
    server.putMember(u"praca"_s, u"b.ics"_s, todo(u"b"_s, u"Drugie"));
    CalDavClient client(&server, QUrl(server.calendarUrl(u"praca"_s)), u"jan"_s, server.options().password);
    const QUrl url(server.calendarUrl(u"praca"_s));
    // The first run: every member, and a token.
    ChangeSet first = changes(client, url, {});
    QVERIFY(!first.error.isError());
    QVERIFY(first.complete);
    QCOMPARE(paths(first.changed), (QStringList{u"a.ics"_s, u"b.ics"_s}));
    QVERIFY(!first.syncToken.isEmpty());
    // Changes since: one changed, one removed, one new — nothing else.
    server.putMember(u"praca"_s, u"a.ics"_s, todo(u"a"_s, u"Pierwsze, zmienione"));
    server.removeMember(u"praca"_s, u"b.ics"_s);
    server.putMember(u"praca"_s, u"zażółć.ics"_s, todo(u"c"_s, u"Trzecie"));
    server.clearLog();
    ChangeSet next = changes(client, url, first.syncToken);
    QVERIFY(!next.error.isError());
    QVERIFY(!next.complete);
    QCOMPARE(paths(next.changed), (QStringList{u"a.ics"_s, u"zażółć.ics"_s}));
    QCOMPARE(next.removed.size(), 1);
    QVERIFY(next.removed.first().endsWith(u"/b.ics"_s));
    QCOMPARE(server.count("REPORT"), 1);
    QCOMPARE(server.count("PROPFIND"), 0);
    // The data of the changed ones in one multiget.
    server.clearLog();
    const QList<RemoteResource> data = fetch(client, url, {next.changed.at(0).path, next.changed.at(1).path});
    QCOMPARE(data.size(), 2);
    QCOMPARE(server.count("REPORT"), 1);
    QVERIFY(data.at(0).data.contains("BEGIN:VTODO"));
    QVERIFY(!data.at(0).etag.isEmpty());
    // Nothing changed: nothing listed.
    const ChangeSet quiet = changes(client, url, next.syncToken);
    QVERIFY(quiet.changed.isEmpty() && quiet.removed.isEmpty());
}

void TestCalDavClient::refusedToken()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, {}));
    server.putMember(u"praca"_s, u"a.ics"_s, todo(u"a"_s, u"Pierwsze"));
    CalDavClient client(&server, QUrl(server.calendarUrl(u"praca"_s)), u"jan"_s, server.options().password);
    const QUrl url(server.calendarUrl(u"praca"_s));
    const ChangeSet first = changes(client, url, {});
    server.expireTokens();
    const ChangeSet again = changes(client, url, first.syncToken);
    QVERIFY(!again.error.isError());
    QVERIFY(again.complete); // every member listed again: the caller compares the ETags
    QCOMPARE(paths(again.changed), QStringList{u"a.ics"_s});
    QVERIFY(!again.syncToken.isEmpty());
    QVERIFY(again.syncToken != first.syncToken);
}

void TestCalDavClient::noSyncCollection()
{
    FakeCalDav::Options o;
    o.syncCollection = false;
    FakeCalDav server(o);
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, {}));
    server.putMember(u"praca"_s, u"a.ics"_s, todo(u"a"_s, u"Pierwsze"));
    CalDavClient client(&server, QUrl(server.calendarUrl(u"praca"_s)), u"jan"_s, server.options().password);
    const QUrl url(server.calendarUrl(u"praca"_s));
    const ChangeSet first = changes(client, url, {});
    QVERIFY(first.complete);
    QVERIFY(first.syncToken.isEmpty()); // no token: the ETags are compared every time
    QCOMPARE(paths(first.changed), QStringList{u"a.ics"_s});
    // Asked with a token anyway (an old one): the listing again.
    const ChangeSet second = changes(client, url, u"http://dav.example.pl/sync/1"_s);
    QVERIFY(second.complete);
}

void TestCalDavClient::multigetFallback()
{
    FakeCalDav::Options o;
    o.multiget = false;
    FakeCalDav server(o);
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, {}));
    server.putMember(u"praca"_s, u"a.ics"_s, todo(u"a"_s, u"Pierwsze"));
    server.putMember(u"praca"_s, u"b.ics"_s, todo(u"b"_s, u"Drugie"));
    CalDavClient client(&server, QUrl(server.calendarUrl(u"praca"_s)), u"jan"_s, server.options().password);
    const QUrl url(server.calendarUrl(u"praca"_s));
    const ChangeSet all = changes(client, url, {});
    server.clearLog();
    const QString gone = all.changed.first().path.section(u'/', 0, -2) + u"/zniknął.ics"_s;
    const QList<RemoteResource> data = fetch(client, url, {all.changed.at(0).path, all.changed.at(1).path, gone});
    QCOMPARE(paths(data), (QStringList{u"a.ics"_s, u"b.ics"_s}));
    QCOMPARE(server.count("GET"), 3);
}

void TestCalDavClient::conditionalWrites()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, {}));
    CalDavClient client(&server, QUrl(server.calendarUrl(u"praca"_s)), u"jan"_s, server.options().password);
    const QString path = server.homePath() + u"praca/nowe zadanie.ics"_s;
    // A new member: If-None-Match.
    const WriteResult created = put(client, path, todo(u"n"_s, u"Nowe"), {});
    QVERIFY(!created.error.isError());
    QVERIFY(!created.etag.isEmpty());
    QCOMPARE(server.requests.last().ifNoneMatch, QByteArray("*"));
    // Once more as new: it exists.
    QVERIFY(put(client, path, todo(u"n"_s, u"Nowe"), {}).error.is(sync::WebDavErrorCode::Precondition));
    // An update with the right ETag, then with a stale one.
    const WriteResult updated = put(client, path, todo(u"n"_s, u"Zmienione"), created.etag);
    QVERIFY(!updated.error.isError());
    QCOMPARE(server.requests.last().ifMatch, '"' + created.etag.toUtf8() + '"');
    QVERIFY(put(client, path, todo(u"n"_s, u"Stare"), created.etag).error.is(sync::WebDavErrorCode::Precondition));
    QCOMPARE(server.member(u"praca"_s, u"nowe zadanie.ics"_s)->data, todo(u"n"_s, u"Zmienione"));
    // Delete with a stale ETag: refused; with the right one: gone; again: fine.
    std::optional<sync::WebDavError> removed;
    client.remove(path, created.etag, [&removed](const sync::WebDavError &e) { removed = e; });
    QTRY_VERIFY(removed);
    QVERIFY(removed->is(sync::WebDavErrorCode::Precondition));
    removed.reset();
    client.remove(path, updated.etag, [&removed](const sync::WebDavError &e) { removed = e; });
    QTRY_VERIFY(removed);
    QVERIFY(!removed->isError());
    removed.reset();
    client.remove(path, updated.etag, [&removed](const sync::WebDavError &e) { removed = e; });
    QTRY_VERIFY(removed);
    QVERIFY(!removed->isError());
    // A read-only calendar refuses writes.
    FakeCalDav::Calendar ro = calendarNamed(u"Święta"_s, {});
    ro.readOnly = true;
    server.addCalendar(u"swieta"_s, ro);
    QVERIFY(put(client, server.homePath() + u"swieta/x.ics"_s, todo(u"x"_s, u"X"), {}).error.is(sync::WebDavErrorCode::Forbidden));
    // Without an ETag on PUT answers: empty (the caller fetches it later).
    server.options().putEtag = false;
    const WriteResult noTag = put(client, server.homePath() + u"praca/y.ics"_s, todo(u"y"_s, u"Y"), {});
    QVERIFY(!noTag.error.isError());
    QVERIFY(noTag.etag.isEmpty());
}

void TestCalDavClient::malformedAnswers()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, calendarNamed(u"Praca"_s, {}));
    const QUrl url(server.calendarUrl(u"praca"_s));
    CalDavClient client(&server, url, u"jan"_s, server.options().password);
    const QList<QByteArray> answers = {
            "<html><body>Zaloguj się</body></html>",
            "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/dav/",
            "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\"><d:response></d:response></d:wrong>",
            QByteArray(),
            "\x00\x01\x02 to nie XML",
    };
    for (const QByteArray &body : answers) {
        server.hook = [body](const sync::HttpRequest &, const QString &) -> std::optional<sync::HttpResponse> {
            sync::HttpResponse r;
            r.status = 207;
            r.body = body;
            return r;
        };
        const ChangeSet c = changes(client, url, {});
        QVERIFY(c.error.isError());
        QVERIFY(c.error.is(sync::WebDavErrorCode::Parse) || c.changed.isEmpty());
    }
    // A server error is an error.
    server.hook = [](const sync::HttpRequest &, const QString &) -> std::optional<sync::HttpResponse> {
        sync::HttpResponse r;
        r.status = 503;
        return r;
    };
    QVERIFY(changes(client, url, {}).error.is(sync::WebDavErrorCode::Server));
}

void TestCalDavClient::credentialsStayOnTheSite_data()
{
    QTest::addColumn<QString>("server");
    QTest::addColumn<QString>("target");
    QTest::addColumn<bool>("allowed");
    QTest::newRow("same") << u"https://dav.example.pl/dav/"_s << u"https://dav.example.pl/x"_s << true;
    QTest::newRow("same, default port") << u"https://dav.example.pl/"_s << u"https://dav.example.pl:443/x"_s << true;
    QTest::newRow("other port") << u"https://dav.example.pl/"_s << u"https://dav.example.pl:8443/x"_s << false;
    QTest::newRow("icloud partition") << u"https://caldav.icloud.com/"_s << u"https://p42-caldav.icloud.com/1/calendars/"_s
                                      << true;
    QTest::newRow("other site") << u"https://caldav.icloud.com/"_s << u"https://icloud.com.evil.example/"_s << false;
    QTest::newRow("country domain") << u"https://dav.firma.com.pl/"_s << u"https://kalendarz.firma.com.pl/"_s << true;
    QTest::newRow("not the whole public suffix") << u"https://dav.firma.com.pl/"_s << u"https://inna.com.pl/"_s << false;
    QTest::newRow("http elsewhere") << u"http://dav.example.pl/"_s << u"http://inny.example.pl/"_s << false;
    QTest::newRow("down to http") << u"https://dav.example.pl/"_s << u"http://dav.example.pl/"_s << false;
}

void TestCalDavClient::credentialsStayOnTheSite()
{
    QFETCH(QString, server);
    QFETCH(QString, target);
    QFETCH(bool, allowed);
    QCOMPARE(mayCarryCredentials(QUrl(server), QUrl(target)), allowed);
}

QTEST_GUILESS_MAIN(TestCalDavClient)
#include "tst_caldav_client.moc"
