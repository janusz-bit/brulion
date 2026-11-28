// The CalDAV client and the task sync against a real Radicale (scripts/caldav-radicale-test.sh
// starts one from nixpkgs and sets BRULION_RADICALE_URL, _USER and _PASSWORD; without them every
// test is skipped — ctest runs it that way). What only a real server tells: its discovery, its XML,
// its ETags and sync tokens, what it keeps of what Brulion writes — tasks both ways, and events
// brulion-cli asks for through the inbox (caldav/eventchange.h) reaching the server and coming
// back to another device.
#include "caldav/calendarsync.h"
#include "caldav/eventchange.h"

#include "index/vaultindex.h"
#include "sync/http.h"
#include "vault/vault.h"

#include <QDir>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

class TestCalDavRadicale : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void discoveryAndTwoWaySync();
    void eventsThroughTheInbox();

private:
    /// The server's calendar called `name` (discovery), or nullopt.
    std::optional<CalendarInfo> calendarNamed(sync::HttpTransport *transport, const QString &name, QString *home);
    QString m_url;
    QString m_user;
    QString m_password;
};

void TestCalDavRadicale::initTestCase()
{
    m_url = qEnvironmentVariable("BRULION_RADICALE_URL");
    m_user = qEnvironmentVariable("BRULION_RADICALE_USER");
    m_password = qEnvironmentVariable("BRULION_RADICALE_PASSWORD");
    if (m_url.isEmpty())
        QSKIP("no Radicale (scripts/caldav-radicale-test.sh runs one)");
}

void TestCalDavRadicale::discoveryAndTwoWaySync()
{
    sync::NetworkHttpTransport transport;
    CalDavClient client(&transport, QUrl(m_url), m_user, m_password);
    std::optional<Discovery> found;
    client.discover([&found](const Discovery &d) { found = d; });
    QTRY_VERIFY_WITH_TIMEOUT(found.has_value(), 20'000);
    QVERIFY2(!found->error.isError(), qPrintable(found->error.message));
    const auto calendar = std::ranges::find(found->calendars, u"Zadania"_s, &CalendarInfo::name);
    QVERIFY(calendar != found->calendars.end());
    QVERIFY(calendar->holdsTasks());
    QVERIFY(!calendar->syncToken.isEmpty());
    QVERIFY(found->addresses.contains(u"/"_s + m_user + u"/"_s)); // (Radicale: the principal only)

    QTemporaryDir dir;
    QDir().mkpath(dir.filePath(u"notes"_s));
    Vault vault(dir.filePath(u"notes"_s), dir.filePath(u"state"_s));
    QVERIFY(vault.open());
    VaultIndex index(vault.config());
    CalendarStore store(dir.filePath(u"state/calendars"_s));
    CalendarSettings settings;
    Account account;
    account.id = u"a-1"_s;
    account.server = m_url;
    account.user = m_user;
    account.homeUrl = found->homeUrl;
    settings.accounts.append(account);
    CalendarEntry entry;
    entry.id = u"c-1"_s;
    entry.account = account.id;
    entry.url = calendar->url;
    entry.name = calendar->name;
    entry.syncTasks = true;
    entry.taskPage = u"Kalendarze/Zadania.md"_s;
    settings.calendars.append(entry);
    store.saveSettings(settings);
    CalendarSync sync(&store);
    sync.setVault(&vault, &index);
    sync.setTransports(&transport, nullptr);
    const QString password = m_password;
    sync.setPasswords([password](const QString &) { return password; });
    const auto run = [&sync]() {
        std::optional<CalendarRunReport> report;
        sync.run(true, false, [&report](const CalendarRunReport &r) { report = r; });
        for (int i = 0; i < 3000 && !report; ++i)
            QTest::qWait(10);
        if (!report)
            qFatal("the calendar sync did not finish");
        return *report;
    };
    // The first run: what the script put there arrives.
    CalendarRunReport report = run();
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    const QString page = vault.readText(entry.taskPage);
    QVERIFY2(page.contains(u"- [ ] Kupić żółty ser 📅 2026-11-21 ^c"_s), qPrintable(page));
    // A new line and an edit go to the server; a second run changes nothing.
    QString text = page;
    text.replace(u"Kupić żółty ser"_s, u"Kupić żółty ser i masło"_s);
    text += u"- [ ] Zadzwonić do Łucji 📅 2026-11-22 18:00 ⏰ 2026-11-22 17:30\n"_s;
    QVERIFY(vault.writeText(entry.taskPage, text));
    report = run();
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    QCOMPARE(report.uploaded, 2);
    report = run();
    QCOMPARE(report.uploaded + report.downloaded, 0);
    // The server has them as Brulion wrote them (read back through a fresh token).
    std::optional<ChangeSet> got;
    client.changes(QUrl(calendar->url), {}, [&got](const ChangeSet &c) { got = c; });
    QTRY_VERIFY_WITH_TIMEOUT(got.has_value(), 20'000);
    QStringList paths;
    for (const RemoteResource &r : got->changed)
        paths.append(r.path);
    std::optional<QList<RemoteResource>> data;
    client.fetch(QUrl(calendar->url), paths, [&data](const QList<RemoteResource> &r, const sync::WebDavError &) { data = r; });
    QTRY_VERIFY_WITH_TIMEOUT(data.has_value(), 20'000);
    QByteArray joined;
    for (const RemoteResource &r : *data)
        joined += r.data;
    QVERIFY(joined.contains("SUMMARY:Kupić żółty ser i masło"));
    QVERIFY(joined.contains("SUMMARY:Zadzwonić do Łucji"));
    QVERIFY(joined.contains("DUE;TZID="));

    // Another app moves the call under the cheese (RELATED-TO on the server): the line follows.
    const RemoteResource *cheese = nullptr;
    const RemoteResource *call = nullptr;
    for (const RemoteResource &r : *data)
        (r.data.contains("SUMMARY:Zadzwonić do Łucji") ? call : cheese) = &r;
    QVERIFY(cheese && call);
    const QRegularExpression uidLine(u"\r?\nUID:([^\r\n]+)"_s);
    const QString cheeseUid = uidLine.match(QString::fromUtf8(cheese->data)).captured(1);
    QVERIFY(!cheeseUid.isEmpty());
    QByteArray moved = call->data;
    const QByteArray relation = "RELATED-TO;RELTYPE=PARENT:" + cheeseUid.toUtf8() + "\r\nEND:VTODO";
    moved.replace(QByteArray("END:VTODO"), relation);
    std::optional<WriteResult> written;
    client.put(call->path, moved, call->etag, [&written](const WriteResult &w) { written = w; });
    QTRY_VERIFY_WITH_TIMEOUT(written.has_value(), 20'000);
    QVERIFY2(!written->error.isError(), qPrintable(written->error.message));
    report = run();
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    const QString nested = vault.readText(entry.taskPage);
    QVERIFY2(nested.contains(QRegularExpression(u"- \\[ \\] Kupić żółty ser i masło[^\n]*\n\t- \\[ \\] Zadzwonić do Łucji"_s)),
             qPrintable(nested));
    report = run();
    QCOMPARE(report.uploaded + report.downloaded, 0);
}

std::optional<CalendarInfo> TestCalDavRadicale::calendarNamed(sync::HttpTransport *transport, const QString &name,
                                                              QString *home)
{
    CalDavClient client(transport, QUrl(m_url), m_user, m_password);
    std::optional<Discovery> found;
    client.discover([&found](const Discovery &d) { found = d; });
    for (int i = 0; i < 2000 && !found; ++i)
        QTest::qWait(10);
    if (!found || found->error.isError())
        return std::nullopt;
    *home = found->homeUrl;
    const auto it = std::ranges::find(found->calendars, name, &CalendarInfo::name);
    return it == found->calendars.end() ? std::nullopt : std::optional<CalendarInfo>(*it);
}

void TestCalDavRadicale::eventsThroughTheInbox()
{
    // Two devices of one account: A takes what brulion-cli left in its inbox and sends it, B only
    // syncs and sees it come back as A wrote it.
    sync::NetworkHttpTransport transport;
    QString home;
    const auto calendar = calendarNamed(&transport, u"Spotkania"_s, &home);
    QVERIFY2(calendar, "the script makes a calendar of events called Spotkania");
    const QTimeZone warsaw("Europe/Warsaw");
    struct Device {
        QTemporaryDir dir;
        std::unique_ptr<CalendarStore> store;
        std::unique_ptr<CalendarSync> sync;
    };
    const auto device = [&](Device &d) {
        d.store = std::make_unique<CalendarStore>(d.dir.filePath(u"calendars"_s));
        CalendarSettings settings;
        Account account;
        account.id = u"a-1"_s;
        account.server = m_url;
        account.user = m_user;
        account.homeUrl = home;
        settings.accounts.append(account);
        CalendarEntry entry;
        entry.id = u"c-1"_s;
        entry.account = account.id;
        entry.url = calendar->url;
        entry.name = calendar->name;
        settings.calendars.append(entry);
        d.store->saveSettings(settings);
        d.sync = std::make_unique<CalendarSync>(d.store.get());
        d.sync->setTransports(&transport, nullptr);
        d.sync->setZone(warsaw);
        const QString password = m_password;
        d.sync->setPasswords([password](const QString &) { return password; });
    };
    const auto run = [](Device &d) {
        std::optional<CalendarRunReport> report;
        d.sync->run(true, false, [&report](const CalendarRunReport &r) { report = r; });
        for (int i = 0; i < 3000 && !report; ++i)
            QTest::qWait(10);
        if (!report)
            qFatal("the calendar sync did not finish");
        return *report;
    };
    const auto shown = [&warsaw](const Device &d, QDate from, QDate to) {
        const CalendarData data = eventsOf(d.store->state(u"c-1"_s));
        QStringList out;
        for (const Occurrence &o : occurrences(data, from, to, warsaw))
            out.append(o.start.date.toString(Qt::ISODate) + u' ' + o.start.time.toString(u"HH:mm"_s) + u' '
                       + data.events.at(o.event).summary);
        return out;
    };
    Device a;
    Device b;
    device(a);
    device(b);
    CalendarRunReport report = run(a);
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    QCOMPARE(shown(a, QDate(2026, 12, 1), QDate(2026, 12, 3)).size(), 3); // the script's daily stand-up
    // brulion-cli asked: a lunch, the stand-up of 2 December an hour later, the one of 3 December
    // left out.
    QDateTime at = QDateTime::currentDateTimeUtc();
    const auto post = [&a, &at](EventIntent i) {
        i.calendarId = u"c-1"_s;
        i.at = at;
        i.id = newIntentId(at);
        at = at.addMSecs(1);
        return postIntent(a.store->folder(), i);
    };
    EventIntent lunch;
    lunch.kind = EventIntent::Kind::Create;
    lunch.uid = newEventUid();
    lunch.patch.title = u"Obiad z Zosią — żurek"_s;
    lunch.patch.location = u"Kraków, Rynek Główny"_s;
    lunch.patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 4), QTime(13, 0)});
    lunch.patch.times.end = EventMoment(QTime(14, 30));
    lunch.patch.reminderMinutes = 15;
    QVERIFY(post(lunch));
    EventIntent later;
    later.kind = EventIntent::Kind::Change;
    later.uid = u"stand"_s;
    later.instance = instanceFromText(u"20261202T093000@Europe/Warsaw"_s);
    later.patch.times.start = EventMoment(QTime(10, 30));
    QVERIFY(post(later));
    EventIntent skip;
    skip.kind = EventIntent::Kind::Delete;
    skip.uid = u"stand"_s;
    skip.instance = instanceFromText(u"20261203T093000@Europe/Warsaw"_s);
    QVERIFY(post(skip));
    report = run(a);
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    QCOMPARE(report.uploaded, 2); // the lunch, and the stand-up's member once with both changes
    QVERIFY(pendingIntents(a.store->folder()).isEmpty());
    QVERIFY(a.store->state(u"c-1"_s).outbox.isEmpty());
    // Back on the other device, as written.
    report = run(b);
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    const QStringList expected{u"2026-12-01 09:30 Stand-up"_s, u"2026-12-02 10:30 Stand-up"_s,
                               u"2026-12-04 09:30 Stand-up"_s, u"2026-12-04 13:00 Obiad z Zosią — żurek"_s};
    QCOMPARE(shown(b, QDate(2026, 12, 1), QDate(2026, 12, 4)), expected);
    const CalendarData there = eventsOf(b.store->state(u"c-1"_s));
    const auto fields = eventFields(there, lunch.uid, std::nullopt, warsaw);
    QVERIFY(fields);
    QCOMPARE(fields->edit.location, u"Kraków, Rynek Główny"_s);
    QCOMPARE(fields->edit.endTime, QTime(14, 30));
    QCOMPARE(fields->edit.reminderMinutes, 15);
    // Moved a day on, then deleted: both reach the other device.
    EventIntent move;
    move.kind = EventIntent::Kind::Change;
    move.uid = lunch.uid;
    move.patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 5), std::nullopt});
    QVERIFY(post(move));
    QCOMPARE(a.sync->takeInbox(), 1);
    run(a);
    run(b);
    QCOMPARE(shown(b, QDate(2026, 12, 5), QDate(2026, 12, 5)).last(), u"2026-12-05 13:00 Obiad z Zosią — żurek"_s);
    EventIntent gone;
    gone.kind = EventIntent::Kind::Delete;
    gone.uid = lunch.uid;
    QVERIFY(post(gone));
    run(a);
    run(b);
    QVERIFY(!eventFields(eventsOf(b.store->state(u"c-1"_s)), lunch.uid, std::nullopt, warsaw));
}

QTEST_GUILESS_MAIN(TestCalDavRadicale)
#include "tst_caldav_radicale.moc"
