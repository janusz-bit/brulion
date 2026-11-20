// Workspace.calendars (workspacecalendars.h) on a real notebook against an in-memory CalDAV server
// and a feed: a subscription (webcal://) fetched, its events in eventsBetween() with its colour,
// hidden and removed; an account found by discovery and added with its password in an owner-only
// file, its tasks on the calendar's page, its events shown; an event created, one occurrence of a
// series left out, the journal's line about an event; errors said in words.
#include "workspace.h"

#include "caldav/caldav.h"
#include "fakecaldav.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceCalendars;
using caldavtest::FakeCalDav;

namespace {

/// A feed server answering from memory.
class Feeds : public sync::HttpTransport {
public:
    QByteArray body;
    int status = 200;
    int asked = 0;
    void send(const sync::HttpRequest &, sync::HttpCallback done) override
    {
        ++asked;
        sync::HttpResponse r;
        r.status = status;
        r.body = body;
        r.headers.insert("etag", "\"f1\"");
        QTimer::singleShot(0, [r, done = std::move(done)] { done(r); });
    }
};

const QByteArray Feed = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nX-WR-CALNAME:Święta\r\nBEGIN:VEVENT\r\nUID:s1\r\n"
                        "DTSTART;VALUE=DATE:20261111\r\nRRULE:FREQ=YEARLY\r\nSUMMARY:Święto Niepodległości\r\nEND:VEVENT\r\n"
                        "END:VCALENDAR\r\n";

QByteArray vevent(const QString &uid, const QString &lines)
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:"_s + uid + u"\r\n"_s + lines
            + u"END:VEVENT\r\nEND:VCALENDAR\r\n"_s)
            .toUtf8();
}

QByteArray vtodo(const QString &uid, const QString &lines)
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nUID:"_s + uid + u"\r\n"_s + lines
            + u"END:VTODO\r\nEND:VCALENDAR\r\n"_s)
            .toUtf8();
}

} // namespace

class TestWorkspaceCalendars : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void init();
    void cleanup();
    void subscription();
    void accountTasksAndEvents();

private:
    void waitIdle();
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Workspace> m_ws;
    FakeCalDav m_server;
    Feeds m_feeds;
};

void TestWorkspaceCalendars::init()
{
    if (!QTimeZone("Europe/Warsaw").isValid())
        QSKIP("no time zone database");
    m_dir = std::make_unique<QTemporaryDir>();
    QDir().mkpath(m_dir->filePath(u"notes"_s));
    m_ws = std::make_unique<Workspace>();
    m_ws->setClock([] { return QDateTime(QDate(2026, 11, 20), QTime(10, 0), QTimeZone("Europe/Warsaw")); });
    WorkspaceCalendars *cal = m_ws->calendars();
    cal->setTransports(&m_server, &m_feeds);
    cal->setSecretsFolder(m_dir->filePath(u"secrets"_s));
    cal->setAutomatic(false);
    cal->setOnlineCheck([] { return true; });
    cal->setZone(QTimeZone("Europe/Warsaw"));
    cal->setLanguage(u"pl"_s);
    QVERIFY(m_ws->open(m_dir->filePath(u"notes"_s), m_dir->filePath(u"state"_s)));
    QVERIFY(cal->available());
}

void TestWorkspaceCalendars::cleanup()
{
    m_ws.reset();
    m_dir.reset();
}

void TestWorkspaceCalendars::waitIdle()
{
    WorkspaceCalendars *cal = m_ws->calendars();
    QTRY_VERIFY_WITH_TIMEOUT(!cal->running(), 10'000);
    QTest::qWait(20);
    QTRY_VERIFY_WITH_TIMEOUT(!cal->running(), 10'000);
}

void TestWorkspaceCalendars::subscription()
{
    WorkspaceCalendars *cal = m_ws->calendars();
    m_feeds.body = Feed;
    QSignalSpy errors(cal, &WorkspaceCalendars::errorOccurred);
    QVERIFY(cal->addSubscription(u"ftp://zle"_s, {}, {}).isEmpty());
    QCOMPARE(errors.size(), 1);
    QSignalSpy events(cal, &WorkspaceCalendars::eventsChanged);
    const QString id = cal->addSubscription(u"webcal://example.pl/swieta.ics"_s, {}, {});
    QVERIFY(!id.isEmpty());
    waitIdle();
    QVERIFY(cal->addSubscription(u"https://example.pl/swieta.ics"_s, {}, {}).isEmpty()); // the same again
    QTRY_VERIFY(!events.isEmpty());
    const QList<planning::ExternalEvent> list = cal->eventsBetween(QDate(2026, 11, 1), QDate(2026, 11, 30));
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().title, u"Święto Niepodległości"_s);
    QCOMPARE(list.first().calendarName, u"Święta"_s);
    QVERIFY(list.first().allDay);
    QVERIFY(list.first().readOnly);
    QCOMPARE(list.first().color, WorkspaceCalendars::palette().first());
    const QVariantMap details = cal->eventDetails(list.first().key);
    QCOMPARE(details.value(u"title"_s).toString(), u"Święto Niepodległości"_s);
    QVERIFY(!details.value(u"canEdit"_s).toBool());
    QCOMPARE(details.value(u"rule"_s).toString(), u"every year"_s);
    QCOMPARE(cal->journalLine(list.first().key), u"- Święto Niepodległości (Święta)"_s);
    QCOMPARE(cal->calendars().size(), 1);
    QVERIFY(!cal->calendars().first().toMap().value(u"statusText"_s).toString().isEmpty());
    // Hidden: no events; colour; removed.
    cal->setCalendarVisible(id, false);
    QVERIFY(cal->eventsBetween(QDate(2026, 11, 1), QDate(2026, 11, 30)).isEmpty());
    cal->setCalendarVisible(id, true);
    QVERIFY(cal->updateSubscription(id, {{u"color"_s, u"#123456"_s}}));
    QCOMPARE(cal->eventsBetween(QDate(2026, 11, 1), QDate(2026, 11, 30)).first().color, u"#123456"_s);
    // A server error: said in words, the copy stays.
    m_feeds.status = 404;
    cal->refresh(id);
    waitIdle();
    const QVariantMap row = cal->subscriptions().first().toMap();
    QCOMPARE(row.value(u"error"_s).toString(), u"not-found"_s);
    QVERIFY(!row.value(u"errorText"_s).toString().isEmpty());
    QCOMPARE(cal->eventsBetween(QDate(2026, 11, 1), QDate(2026, 11, 30)).size(), 1);
    QVERIFY(!cal->log().isEmpty());
    QVERIFY(cal->removeCalendar(id));
    QVERIFY(!cal->hasCalendars());
    QVERIFY(cal->eventsBetween(QDate(2026, 11, 1), QDate(2026, 11, 30)).isEmpty());
}

void TestWorkspaceCalendars::accountTasksAndEvents()
{
    WorkspaceCalendars *cal = m_ws->calendars();
    FakeCalDav::Calendar praca;
    praca.name = u"Praca"_s;
    praca.color = u"#e53935"_s;
    m_server.addCalendar(u"praca"_s, praca);
    FakeCalDav::Calendar zakupy;
    zakupy.name = u"Zakupy"_s;
    zakupy.components = {u"VTODO"_s};
    m_server.addCalendar(u"zakupy"_s, zakupy);
    m_server.putMember(u"praca"_s, u"standup.ics"_s,
                       vevent(u"standup"_s, u"DTSTART;TZID=Europe/Warsaw:20261116T093000\r\n"
                                            u"DTEND;TZID=Europe/Warsaw:20261116T094500\r\nRRULE:FREQ=DAILY;COUNT=10\r\n"
                                            u"SUMMARY:Stand-up\r\n"_s));
    m_server.putMember(u"zakupy"_s, u"mleko.ics"_s, vtodo(u"mleko"_s, u"SUMMARY:Mleko\r\nDUE;VALUE=DATE:20261121\r\n"_s));
    // Discovery with a wrong password: said in words.
    QSignalSpy found(cal, &WorkspaceCalendars::discovered);
    cal->discover(m_server.url(u"/"_s), u"jan"_s, u"złe"_s);
    QTRY_COMPARE(found.size(), 1);
    QVERIFY(!found.first().at(0).toBool());
    QVERIFY(!found.first().at(1).toString().isEmpty());
    cal->discover(m_server.url(u"/"_s), u"jan"_s, m_server.options().password);
    QTRY_COMPARE(found.size(), 2);
    QVERIFY(found.last().at(0).toBool());
    const QVariantList calendars = found.last().at(2).toList();
    QCOMPARE(calendars.size(), 2);
    QCOMPARE(cal->defaultChoice(calendars, false).size(), 1); // Praca: events
    QCOMPARE(cal->defaultChoice(calendars, true).size(), 1);  // Zakupy: tasks only
    const QString pracaUrl = calendars.at(0).toMap().value(u"url"_s).toString();
    const QString zakupyUrl = calendars.at(1).toMap().value(u"url"_s).toString();
    QCOMPARE(calendars.at(1).toMap().value(u"taskPage"_s).toString(), u"Kalendarze/Zakupy.md"_s);
    const QString account = cal->addAccount(m_server.url(u"/"_s), u"jan"_s, m_server.options().password, {pracaUrl},
                                            {zakupyUrl});
    QVERIFY(!account.isEmpty());
    // The password: owner-only, not in the settings.
    const QString secret = m_dir->filePath(u"secrets/caldav-"_s + account);
    QVERIFY(QFile::exists(secret));
#ifndef Q_OS_WIN // (NTFS rights, which QFile's permission bits do not show)
    QCOMPARE(QFile::permissions(secret) & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
#endif
    QVERIFY(!QFile(m_dir->filePath(u"state/calendars/calendars.json"_s)).readAll().contains("hasło"));
    waitIdle();
    QTRY_VERIFY(QFile::exists(m_dir->filePath(u"notes/Kalendarze/Zakupy.md"_s)));
    QVERIFY(m_ws->read(u"Kalendarze/Zakupy.md"_s).startsWith(u"- [ ] Mleko 📅 2026-11-21 ^c"_s));
    QCOMPARE(cal->accounts().size(), 1);
    QVERIFY(cal->accounts().first().toMap().value(u"hasPassword"_s).toBool());
    QCOMPARE(cal->writableCalendars().size(), 1);
    // The stand-up's occurrences, in its calendar's colour.
    QList<planning::ExternalEvent> week = cal->eventsBetween(QDate(2026, 11, 16), QDate(2026, 11, 22));
    QCOMPARE(week.size(), 7);
    QCOMPARE(week.first().color, u"#e53935"_s);
    QCOMPARE(week.first().start, QTime(9, 30));
    QCOMPARE(week.first().end, QTime(9, 45));
    QVERIFY(week.first().recurring);
    QVERIFY(!week.first().readOnly);
    // One occurrence left out: the series gets an EXDATE on the server.
    QVERIFY(cal->deleteEvent(week.at(2).key, true));
    QCOMPARE(cal->eventsBetween(QDate(2026, 11, 16), QDate(2026, 11, 22)).size(), 6);
    waitIdle();
    QTRY_VERIFY(m_server.member(u"praca"_s, u"standup.ics"_s)->data.contains("EXDATE;TZID=Europe/Warsaw:20261118T093000"));
    // A new event: shown at once, on the server after the run.
    const QString calendarId = cal->writableCalendars().first().toMap().value(u"id"_s).toString();
    QVERIFY(cal->saveEvent({{u"calendarId"_s, calendarId},
                            {u"title"_s, u"Obiad z Zosią"_s},
                            {u"location"_s, u"Kraków"_s},
                            {u"startDate"_s, u"2026-11-21"_s},
                            {u"startTime"_s, u"13:00"_s},
                            {u"endDate"_s, u"2026-11-21"_s},
                            {u"endTime"_s, u"14:30"_s},
                            {u"reminderMinutes"_s, 15}}));
    QList<planning::ExternalEvent> saturday = cal->eventsBetween(QDate(2026, 11, 21), QDate(2026, 11, 21));
    QCOMPARE(saturday.size(), 2);
    waitIdle();
    bool onServer = false;
    for (const QString &name : m_server.memberNames(u"praca"_s)) {
        const QByteArray data = m_server.member(u"praca"_s, name)->data;
        if (data.contains("SUMMARY:Obiad z Zosią")) {
            onServer = true;
            QVERIFY(data.contains("DTSTART;TZID=Europe/Warsaw:20261121T130000"));
            QVERIFY(data.contains("TRIGGER:-PT15M"));
            QVERIFY(data.contains("LOCATION:Kraków"));
        }
    }
    QVERIFY(onServer);
    // Its details, edited for all: the server's copy changes.
    saturday = cal->eventsBetween(QDate(2026, 11, 21), QDate(2026, 11, 21));
    QString key;
    for (const planning::ExternalEvent &e : saturday) {
        if (e.title == u"Obiad z Zosią")
            key = e.key;
    }
    const QVariantMap details = cal->eventDetails(key);
    QCOMPARE(details.value(u"startTime"_s).toString(), u"13:00"_s);
    QCOMPARE(details.value(u"reminderMinutes"_s).toInt(), 15);
    QVERIFY(details.value(u"canEdit"_s).toBool());
    QCOMPARE(cal->journalLine(key), u"- 13:00–14:30 Obiad z Zosią (Praca)\n  location:: Kraków"_s);
    QVariantMap edit = details;
    edit.insert(u"title"_s, u"Obiad z Zosią i Kubą"_s);
    QVERIFY(cal->saveEvent(edit));
    waitIdle();
    bool changed = false;
    for (const QString &name : m_server.memberNames(u"praca"_s))
        changed = changed || m_server.member(u"praca"_s, name)->data.contains("SUMMARY:Obiad z Zosią i Kubą");
    QVERIFY(changed);
    // An event needs a title.
    QSignalSpy errors(cal, &WorkspaceCalendars::errorOccurred);
    QVERIFY(!cal->saveEvent({{u"calendarId"_s, calendarId}, {u"startDate"_s, u"2026-11-21"_s}}));
    QCOMPARE(errors.size(), 1);
    // The account goes with its password.
    QVERIFY(cal->removeAccount(account));
    QVERIFY(!QFile::exists(secret));
    QVERIFY(cal->eventsBetween(QDate(2026, 11, 16), QDate(2026, 11, 22)).isEmpty());
    // The notes stay.
    QVERIFY(m_ws->read(u"Kalendarze/Zakupy.md"_s).contains(u"Mleko"_s));
}

QTEST_MAIN(TestWorkspaceCalendars)
#include "tst_workspace_calendars.moc"
