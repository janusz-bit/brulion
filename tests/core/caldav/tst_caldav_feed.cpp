// ICS subscriptions: addresses as people paste them, fetches with validators (304), redirects by
// hand (never down to http), answers that are no calendar, too large or refused (CORS in the
// browser), and the run that keeps the copy, its name and colour, its errors in the log; the
// store's files.
#include "caldav/calendarsync.h"
#include "caldav/feed.h"

#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

namespace {

/// A transport answering from a function, asynchronously.
class Answers : public sync::HttpTransport {
public:
    std::function<sync::HttpResponse(const sync::HttpRequest &)> answer;
    QList<sync::HttpRequest> seen;
    void send(const sync::HttpRequest &request, sync::HttpCallback done) override
    {
        seen.append(request);
        sync::HttpResponse r = answer(request);
        if (r.body.size() > request.bodyLimit())
            r = sync::HttpResponse::oversized(request);
        QTimer::singleShot(0, [r, done = std::move(done)] { done(r); });
    }
};

sync::HttpResponse ok(const QByteArray &body, const QByteArray &etag = {})
{
    sync::HttpResponse r;
    r.status = 200;
    r.body = body;
    if (!etag.isEmpty())
        r.headers.insert("etag", etag);
    r.headers.insert("last-modified", "Fri, 20 Nov 2026 08:00:00 GMT");
    return r;
}

const QByteArray Feed = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-WR-CALNAME:Święta w Polsce\r\nX-APPLE-CALENDAR-COLOR:#E91E63\r\n"
                        "BEGIN:VEVENT\r\nUID:swieto-1\r\nDTSTART;VALUE=DATE:20261111\r\nSUMMARY:Narodowe Święto Niepodległości\r\n"
                        "RRULE:FREQ=YEARLY\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

FeedResult fetch(FeedFetcher &f, const QUrl &url, const QString &etag = {}, const QString &modified = {})
{
    std::optional<FeedResult> result;
    f.fetch(url, etag, modified, [&result](const FeedResult &r) { result = r; });
    for (int i = 0; i < 500 && !result; ++i)
        QTest::qWait(2);
    return result.value_or(FeedResult{});
}

} // namespace

class TestCalDavFeed : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void addresses_data();
    void addresses();
    void conditionalFetch();
    void redirects();
    void refused();
    void subscriptionRun();
    void oneRunAtATime();
    void storeFiles();
};

void TestCalDavFeed::addresses_data()
{
    QTest::addColumn<QString>("typed");
    QTest::addColumn<QString>("url");
    QTest::newRow("webcal") << u"webcal://p01-calendars.icloud.com/published/2/abc"_s
                            << u"https://p01-calendars.icloud.com/published/2/abc"_s;
    QTest::newRow("webcals") << u"WEBCALS://example.pl/kalendarz.ics"_s << u"https://example.pl/kalendarz.ics"_s;
    QTest::newRow("https with query") << u" https://calendar.google.com/calendar/ical/x%40group/private-ab/basic.ics "_s
                                      << u"https://calendar.google.com/calendar/ical/x%40group/private-ab/basic.ics"_s;
    QTest::newRow("no scheme") << u"example.pl/swieta.ics"_s << u"https://example.pl/swieta.ics"_s;
    QTest::newRow("http") << u"http://intranet.firma.pl/plan.ics"_s << u"http://intranet.firma.pl/plan.ics"_s;
    QTest::newRow("user info dropped") << u"https://jan:haslo@example.pl/k.ics"_s << u"https://example.pl/k.ics"_s;
    QTest::newRow("polish host") << u"https://kalendarz.zażółć.pl/a.ics"_s << u"https://kalendarz.xn--za-6ja4f8n1l.pl/a.ics"_s;
    QTest::newRow("ftp") << u"ftp://example.pl/a.ics"_s << QString();
    QTest::newRow("spaces") << u"https://example.pl/moje święta.ics"_s << QString();
    QTest::newRow("empty") << u"  "_s << QString();
    QTest::newRow("no host") << u"https:///a.ics"_s << QString();
    QTest::newRow("file") << u"file:///etc/passwd"_s << QString();
}

void TestCalDavFeed::addresses()
{
    QFETCH(QString, typed);
    QFETCH(QString, url);
    QString error;
    const QUrl got = feedUrl(typed, &error);
    if (url.isEmpty()) {
        QVERIFY(!got.isValid());
        QVERIFY(!error.isEmpty());
    } else {
        QVERIFY2(got.isValid(), qPrintable(error));
        QCOMPARE(got.toString(QUrl::FullyEncoded), url);
    }
}

void TestCalDavFeed::conditionalFetch()
{
    Answers net;
    net.answer = [](const sync::HttpRequest &r) {
        if (r.header("if-none-match") == "\"v1\"") {
            sync::HttpResponse nm;
            nm.status = 304;
            return nm;
        }
        return ok(Feed, "\"v1\"");
    };
    FeedFetcher fetcher(&net);
    const QUrl url(u"https://example.pl/swieta.ics"_s);
    FeedResult first = fetch(fetcher, url);
    QVERIFY(first.ok());
    QCOMPARE(first.data, Feed);
    QCOMPARE(first.etag, u"\"v1\""_s);
    QCOMPARE(first.lastModified, u"Fri, 20 Nov 2026 08:00:00 GMT"_s);
    QCOMPARE(net.seen.last().header("user-agent"), QByteArray("Brulion"));
    QVERIFY(net.seen.last().header("authorization").isEmpty());
    FeedResult second = fetch(fetcher, url, first.etag, first.lastModified);
    QVERIFY(second.ok());
    QVERIFY(second.notModified);
    QCOMPARE(net.seen.last().header("if-modified-since"), QByteArray("Fri, 20 Nov 2026 08:00:00 GMT"));
    // In the browser: no User-Agent of the page's own.
    fetcher.setBrowser(true);
    fetch(fetcher, url);
    QVERIFY(!net.seen.last().hasHeader("user-agent"));
}

void TestCalDavFeed::redirects()
{
    Answers net;
    net.answer = [](const sync::HttpRequest &r) {
        sync::HttpResponse moved;
        moved.status = 302;
        if (r.url.host() == u"stary.example.pl") {
            moved.headers.insert("location", "https://nowy.example.com/kalendarz.ics");
            return moved;
        }
        if (r.url.host() == u"pętla.example.pl") {
            moved.headers.insert("location", "/znowu");
            return moved;
        }
        if (r.url.host() == u"dol.example.pl") {
            moved.headers.insert("location", "http://dol.example.pl/k.ics");
            return moved;
        }
        return ok(Feed);
    };
    FeedFetcher fetcher(&net);
    FeedResult r = fetch(fetcher, QUrl(u"https://stary.example.pl/k.ics"_s));
    QVERIFY(r.ok());
    QCOMPARE(r.url.host(), u"nowy.example.com"_s); // another server: fine, no credentials travel
    QVERIFY(!fetch(fetcher, QUrl(u"https://pętla.example.pl/k.ics"_s)).ok());
    QVERIFY(!fetch(fetcher, QUrl(u"https://dol.example.pl/k.ics"_s)).ok());
}

void TestCalDavFeed::refused()
{
    Answers net;
    QByteArray body;
    int status = 200;
    QString networkError;
    net.answer = [&](const sync::HttpRequest &) {
        sync::HttpResponse r;
        r.status = status;
        r.body = body;
        r.networkError = networkError;
        return r;
    };
    FeedFetcher fetcher(&net);
    const QUrl url(u"https://example.pl/k.ics"_s);
    body = "<!DOCTYPE html><html><body>Zaloguj się</body></html>";
    QVERIFY(fetch(fetcher, url).error.is(sync::WebDavErrorCode::Parse));
    status = 404;
    QVERIFY(fetch(fetcher, url).error.is(sync::WebDavErrorCode::NotFound));
    status = 401;
    QVERIFY(fetch(fetcher, url).error.is(sync::WebDavErrorCode::Auth));
    status = 200;
    fetcher.setMaxBytes(100);
    body = Feed;
    QVERIFY(fetch(fetcher, url).error.is(sync::WebDavErrorCode::Oversized));
    fetcher.setMaxBytes(FeedFetcher::DefaultMaxBytes);
    status = 0;
    networkError = u"Network error"_s;
    FeedResult r = fetch(fetcher, url);
    QVERIFY(r.error.is(sync::WebDavErrorCode::Network));
    QVERIFY(!r.maybeCors);
    fetcher.setBrowser(true);
    QVERIFY(fetch(fetcher, url).maybeCors);
    // A BOM and blank lines before the calendar are fine.
    status = 200;
    networkError.clear();
    body = "\xEF\xBB\xBF\r\n\r\n" + Feed;
    QVERIFY(fetch(fetcher, url).ok());
    QVERIFY(looksLikeCalendar("begin:vcalendar\r\n"));
    QVERIFY(!looksLikeCalendar("BEGIN:VCARD\r\n"));
    QVERIFY(!looksLikeCalendar(""));
}

void TestCalDavFeed::subscriptionRun()
{
    QTemporaryDir dir;
    CalendarStore store(dir.path() + u"/calendars"_s);
    CalendarSettings settings;
    Subscription s;
    s.id = u"s-1"_s;
    s.url = u"https://example.pl/swieta.ics"_s;
    s.refreshMinutes = 60;
    settings.subscriptions.append(s);
    store.saveSettings(settings);
    Answers net;
    int answers = 0;
    net.answer = [&answers](const sync::HttpRequest &r) {
        ++answers;
        if (!r.header("if-none-match").isEmpty()) {
            sync::HttpResponse nm;
            nm.status = 304;
            return nm;
        }
        return ok(Feed, "\"v1\"");
    };
    CalendarSync sync(&store);
    sync.setTransports(nullptr, &net);
    QDateTime now(QDate(2026, 11, 20), QTime(10, 0));
    sync.setClock([&now] { return now; });
    const auto run = [&sync](bool manual) {
        std::optional<CalendarRunReport> report;
        sync.run(manual, false, [&report](const CalendarRunReport &r) { report = r; });
        for (int i = 0; i < 500 && !report; ++i)
            QTest::qWait(2);
        return report.value_or(CalendarRunReport{});
    };
    CalendarRunReport report = run(false);
    QCOMPARE(report.refreshed, 1);
    QCOMPARE(store.feedData(u"s-1"_s), Feed);
    const Subscription *named = store.settings().subscription(u"s-1"_s);
    QCOMPARE(named->name, u"Święta w Polsce"_s);
    QCOMPARE(named->color, u"#e91e63"_s);
    QCOMPARE(store.feedMeta(u"s-1"_s).events, 1);
    // Not due yet: nothing is asked.
    now = now.addSecs(30 * 60);
    run(false);
    QCOMPARE(answers, 1);
    // Due: asked with the validators, unchanged (304).
    now = now.addSecs(31 * 60);
    report = run(false);
    QCOMPARE(answers, 2);
    QCOMPARE(report.refreshed, 1);
    QCOMPARE(store.feedData(u"s-1"_s), Feed);
    // By hand: always asked.
    run(true);
    QCOMPARE(answers, 3);
    // A failure: the copy stays, the error is kept and logged once.
    net.answer = [](const sync::HttpRequest &) {
        sync::HttpResponse r;
        r.status = 503;
        return r;
    };
    report = run(true);
    QCOMPARE(report.failed, QStringList{u"s-1"_s});
    QCOMPARE(store.feedMeta(u"s-1"_s).error, u"server"_s);
    QCOMPARE(store.feedData(u"s-1"_s), Feed);
    bool logged = false;
    for (const LogEntry &e : store.log())
        logged = logged || (e.kind == LogEntry::Kind::Error && e.code == u"server");
    QVERIFY(logged);
    // The events of the copy.
    const CalendarData data = parseCalendarData(store.feedData(u"s-1"_s));
    QCOMPARE(occurrences(data, QDate(2027, 11, 1), QDate(2027, 11, 30), QTimeZone::UTC).size(), 1);
}

void TestCalDavFeed::oneRunAtATime()
{
    // The app and Android's background check over one notebook's calendars: the second waits for
    // the next turn (its run ends at once, cancelled), and reads what the first wrote.
    QTemporaryDir dir;
    CalendarStore first(dir.path() + u"/calendars"_s);
    CalendarSettings settings;
    Subscription s;
    s.id = u"s-1"_s;
    s.url = u"https://example.pl/swieta.ics"_s;
    settings.subscriptions.append(s);
    first.saveSettings(settings);
    CalendarStore second(dir.path() + u"/calendars"_s);
    Answers net;
    net.answer = [](const sync::HttpRequest &) { return ok(Feed); };
    CalendarSync a(&first);
    CalendarSync b(&second);
    a.setTransports(nullptr, &net);
    b.setTransports(nullptr, &net);
    std::optional<CalendarRunReport> ra;
    std::optional<CalendarRunReport> rb;
    a.run(true, false, [&ra](const CalendarRunReport &r) { ra = r; });
    b.run(true, false, [&rb](const CalendarRunReport &r) { rb = r; });
    QTRY_VERIFY(ra && rb);
    QVERIFY(!ra->cancelled);
    QCOMPARE(ra->refreshed, 1);
    QVERIFY(rb->cancelled);
    QCOMPARE(rb->refreshed, 0);
    // The other one's next run reads the store again: the feed's name arrived meanwhile.
    rb.reset();
    net.answer = [](const sync::HttpRequest &) {
        sync::HttpResponse nm;
        nm.status = 304;
        return nm;
    };
    b.run(false, false, [&rb](const CalendarRunReport &r) { rb = r; });
    QTRY_VERIFY(rb);
    QVERIFY(!rb->cancelled);
    QCOMPARE(second.settings().subscription(u"s-1"_s)->name, u"Święta w Polsce"_s);
}

void TestCalDavFeed::storeFiles()
{
    QTemporaryDir dir;
    CalendarSettings settings;
    Account a;
    a.id = u"a-1"_s;
    a.server = u"https://chmura.example.pl"_s;
    a.user = u"jan"_s;
    a.conflicts = ConflictPolicy::KeepBoth;
    a.intervalMinutes = 30;
    settings.accounts.append(a);
    CalendarEntry c;
    c.id = u"c-1"_s;
    c.account = u"a-1"_s;
    c.url = u"https://chmura.example.pl/remote.php/dav/calendars/jan/praca/"_s;
    c.name = u"Praca — ważne"_s;
    c.components = {u"VTODO"_s};
    c.syncTasks = true;
    c.taskPage = u"Kalendarze/Praca.md"_s;
    settings.calendars.append(c);
    CalendarEntry orphan = c;
    orphan.id = u"c-2"_s;
    orphan.account = u"nie-ma"_s;
    settings.calendars.append(orphan);
    {
        CalendarStore store(dir.path() + u"/calendars"_s);
        QVERIFY(store.saveSettings(settings));
        CalendarState st;
        st.syncToken = u"tok"_s;
        st.resources.insert(u"/a.ics"_s, Resource{u"e1"_s, u"a"_s, u"VTODO"_s, "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n"});
        TrackedTask t;
        t.path = u"/a.ics"_s;
        t.blockId = u"ca"_s;
        t.local.summary = u"Żółw"_s;
        st.tasks.insert(u"a"_s, t);
        QVERIFY(store.saveState(u"c-1"_s, st));
        LogEntry e;
        e.at = QDateTime(QDate(2026, 11, 20), QTime(10, 0), QTimeZone::UTC);
        e.kind = LogEntry::Kind::Conflict;
        e.text = u"Zadanie"_s;
        store.append(e);
    }
    CalendarStore again(dir.path() + u"/calendars"_s);
    QCOMPARE(again.settings().accounts, settings.accounts);
    QCOMPARE(again.settings().calendars.size(), 1); // the calendar of an unknown account is dropped
    QCOMPARE(again.settings().calendars.first(), c);
    QCOMPARE(again.state(u"c-1"_s).syncToken, u"tok"_s);
    QCOMPARE(again.state(u"c-1"_s).tasks.value(u"a"_s).local.summary, u"Żółw"_s);
    QCOMPARE(again.log().size(), 1);
    QCOMPARE(again.log().first().kind, LogEntry::Kind::Conflict);
    again.forget(u"c-1"_s);
    QVERIFY(again.state(u"c-1"_s).tasks.isEmpty());
    // Owner-only files: the folder holds the state of private calendars.
    QCOMPARE(QFileInfo(dir.path() + u"/calendars/calendars.json"_s).permissions() & (QFile::ReadOther | QFile::ReadGroup),
             QFile::Permissions());
    // A broken file: empty settings, no crash.
    QFile broken(dir.path() + u"/calendars/calendars.json"_s);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("{ to nie jest json");
    broken.close();
    CalendarStore damaged(dir.path() + u"/calendars"_s);
    QVERIFY(damaged.settings().accounts.isEmpty());
    QVERIFY(CalendarStore::newId(u's').startsWith(u"s-"_s));
}

QTEST_GUILESS_MAIN(TestCalDavFeed)
#include "tst_caldav_feed.moc"
