// Two-way sync of a CalDAV calendar's tasks with the task lines of a notebook, against FakeCalDav:
// the first sync (the server's tasks land on the calendar's page — subtasks under their parents),
// a sync with nothing to do, edits on either side, both sides in different fields (merged) and in
// the same one (the newer wins, or both stay), deletions both ways with the guard, a recurring task
// completed in Brulion, a second device of the same notebook, events changed in Brulion (the
// outbox and its conflicts), events changed by brulion-cli (its inbox, taken under the device's
// lock), Polish text, broken members, a server that cannot be reached.
#include "caldav/calendarsync.h"
#include "caldav/eventchange.h"
#include "fakecaldav.h"

#include "index/vaultindex.h"
#include "markdown/edit.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace caldavtest;
using namespace Qt::StringLiterals;

namespace {

const QTimeZone Warsaw("Europe/Warsaw");
const QString Page = u"Kalendarze/Praca.md"_s;

QByteArray vtodo(const QString &uid, const QString &lines)
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Nextcloud Tasks//EN\r\nBEGIN:VTODO\r\nUID:"_s + uid + u"\r\n"_s
            + lines + u"END:VTODO\r\nEND:VCALENDAR\r\n"_s)
            .toUtf8();
}

QByteArray vevent(const QString &uid, const QString &lines)
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VEVENT\r\nUID:"_s + uid + u"\r\n"_s + lines
            + u"END:VEVENT\r\nEND:VCALENDAR\r\n"_s)
            .toUtf8();
}

/// A notebook on a device, with its calendars set up for the fake server's "praca".
struct Device {
    std::unique_ptr<QTemporaryDir> own;
    QString root;
    std::unique_ptr<Vault> vault;
    std::unique_ptr<VaultIndex> index;
    std::unique_ptr<CalendarStore> store;
    std::unique_ptr<CalendarSync> sync;
    QDateTime clock{QDate(2026, 11, 20), QTime(10, 0), Warsaw};

    explicit Device(FakeCalDav *server, const QString &notes = {}, ConflictPolicy policy = ConflictPolicy::Newer)
    {
        own = std::make_unique<QTemporaryDir>();
        root = notes.isEmpty() ? own->path() + u"/notes"_s : notes;
        QDir().mkpath(root);
        vault = std::make_unique<Vault>(root, own->path() + u"/state"_s);
        vault->open();
        index = std::make_unique<VaultIndex>(vault->config());
        reindex();
        QObject::connect(vault.get(), &Vault::changed, [this](const QList<VaultChange> &changes) {
            for (const VaultChange &c : changes) {
                if (c.type == ChangeType::Deleted)
                    index->remove(c.path);
                else if (c.path.endsWith(u".md"_s))
                    index->update(c.path, vault->readText(c.path));
            }
        });
        store = std::make_unique<CalendarStore>(own->path() + u"/state/calendars"_s);
        CalendarSettings settings;
        Account account;
        account.id = u"a-1"_s;
        account.server = server->url(server->options().root);
        account.user = server->options().user;
        account.homeUrl = server->url(server->homePath());
        account.conflicts = policy;
        settings.accounts.append(account);
        CalendarEntry entry;
        entry.id = u"c-1"_s;
        entry.account = account.id;
        entry.url = server->calendarUrl(u"praca"_s);
        entry.name = u"Praca"_s;
        entry.syncTasks = true;
        entry.taskPage = Page;
        settings.calendars.append(entry);
        store->saveSettings(settings);
        sync = std::make_unique<CalendarSync>(store.get());
        sync->setVault(vault.get(), index.get());
        sync->setTransports(server, nullptr);
        const QString password = server->options().password;
        sync->setPasswords([password](const QString &) { return password; });
        sync->setZone(Warsaw);
        sync->setClock([this] { return clock; });
    }

    void reindex()
    {
        vault->rescan();
        for (const FileStat &f : vault->markdownFiles())
            index->update(f.path, vault->readText(f.path));
    }
    CalendarRunReport run(bool confirm = false)
    {
        std::optional<CalendarRunReport> report;
        sync->run(true, confirm, [&report](const CalendarRunReport &r) { report = r; });
        for (int i = 0; i < 2000 && !report; ++i)
            QTest::qWait(2);
        if (!report)
            qFatal("calendar sync did not finish");
        reindex();
        return *report;
    }
    QString page() { return vault->readText(Page); }
    void write(const QString &text)
    {
        QVERIFY(vault->writeText(Page, text));
        index->update(Page, text);
    }
    CalendarState state() const { return store->state(u"c-1"_s); }
};

FakeCalDav::Calendar praca()
{
    FakeCalDav::Calendar c;
    c.name = u"Praca"_s;
    c.color = u"#1e88e5"_s;
    return c;
}

QString memberText(FakeCalDav &server, const QString &name)
{
    const auto m = server.member(u"praca"_s, name);
    return m ? QString::fromUtf8(m->data) : QString();
}

/// The member whose UID is `uid` (Brulion names new members after it).
QString memberOfUid(FakeCalDav &server, const QString &uid)
{
    for (const QString &name : server.memberNames(u"praca"_s)) {
        if (memberText(server, name).contains(u"UID:"_s + uid))
            return name;
    }
    return {};
}

} // namespace

class TestCalDavTasks : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void firstSyncBringsTheServersTasks();
    void editsOnTheServer();
    void editsInTheNotes();
    void newLinesGoToTheServer();
    void bothSidesInDifferentFields();
    void sameFieldNewerWins();
    void sameFieldKeepBoth();
    void deletionsBothWays();
    void massDeletionWaitsForConfirmation();
    void recurringTaskCompletedHere();
    void secondDevicePairsLines();
    void movedLineKeepsSyncing();
    void movedSubtaskUpdatesRelatedTo();
    void eventsChangedHere();
    void eventsFromTheInbox();
    void brokenMembersAndOffline();
};

void TestCalDavTasks::initTestCase()
{
    if (!Warsaw.isValid())
        QSKIP("no time zone database");
}

void TestCalDavTasks::firstSyncBringsTheServersTasks()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"mleko.ics"_s,
                     vtodo(u"mleko-1"_s, u"SUMMARY:Kupić mleko\r\nDUE;VALUE=DATE:20261121\r\nPRIORITY:1\r\nCATEGORIES:zakupy\r\n"_s));
    server.putMember(u"praca"_s, u"lucja.ics"_s,
                     vtodo(u"040000008200E00074C5B7101A82E008@outlook.com"_s,
                           u"SUMMARY:Zadzwonić do Łucji\\, pilne\r\nDUE;TZID=America/New_York:20261121T120000\r\n"
                           u"BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER;RELATED=END:-PT15M\r\nEND:VALARM\r\n"_s));
    server.putMember(u"praca"_s, u"raport.ics"_s,
                     vtodo(u"raport"_s, u"SUMMARY:Raport\r\nSTATUS:COMPLETED\r\nCOMPLETED:20261118T150000Z\r\n"
                                        u"DESCRIPTION:Wysłany do szefa\\n- kopia do działu\r\n"_s));
    server.putMember(u"praca"_s, u"raport-a.ics"_s,
                     vtodo(u"raport-a"_s, u"SUMMARY:Tabela\r\nRELATED-TO;RELTYPE=PARENT:raport\r\nX-NEXTCLOUD:1\r\n"_s));
    server.putMember(u"praca"_s, u"basen.ics"_s,
                     vtodo(u"basen"_s, u"SUMMARY:Basen\r\nDUE;VALUE=DATE:20261123\r\nRRULE:FREQ=WEEKLY;BYDAY=MO\r\n"_s));
    // Not a task: an event of the same calendar.
    server.putMember(u"praca"_s, u"zebranie.ics"_s,
                     vevent(u"zebranie"_s, u"DTSTART:20261121T090000Z\r\nDTEND:20261121T100000Z\r\nSUMMARY:Zebranie\r\n"_s));
    Device dev(&server);
    const CalendarRunReport report = dev.run();
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    QCOMPARE(report.downloaded, 6); // five tasks, one event
    const QString lucja = blockIdForUid(u"040000008200E00074C5B7101A82E008@outlook.com"_s);
    const QString expected = u"- [ ] Kupić mleko #zakupy ⏫ 📅 2026-11-21 ^"_s + blockIdForUid(u"mleko-1"_s)
                             + u"\n- [ ] Zadzwonić do Łucji, pilne 📅 2026-11-21 18:00 ⏰ 2026-11-21 17:45 ^"_s + lucja
                             + u"\n- [ ] Basen 🔁 every week on Monday 📅 2026-11-23 ^"_s + blockIdForUid(u"basen"_s)
                             + u"\n- [x] Raport ✅ 2026-11-18 ^"_s + blockIdForUid(u"raport"_s)
                             + u"\n  Wysłany do szefa\n  \\- kopia do działu\n\t- [ ] Tabela ^"_s
                             + blockIdForUid(u"raport-a"_s) + u"\n"_s;
    QCOMPARE(dev.page().split(u'\n'), expected.split(u'\n'));
    QCOMPARE(dev.state().tasks.size(), 5);
    QCOMPARE(dev.state().resources.size(), 6);
    // A second run: nothing to do, nothing written anywhere.
    const QString before = dev.page();
    server.clearLog();
    const CalendarRunReport quiet = dev.run();
    QCOMPARE(quiet.downloaded + quiet.uploaded + quiet.deletedLocal + quiet.deletedRemote, 0);
    QCOMPARE(dev.page(), before);
    QCOMPARE(server.count("PUT") + server.count("DELETE"), 0);
    QCOMPARE(server.count("REPORT"), 1); // one sync-collection, no multiget
}

void TestCalDavTasks::editsOnTheServer()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze\r\nDUE;VALUE=DATE:20261121\r\n"_s));
    server.putMember(u"praca"_s, u"b.ics"_s, vtodo(u"b"_s, u"SUMMARY:Drugie\r\n"_s));
    Device dev(&server);
    dev.run();
    // Other lines of the page, written by hand, CRLF line endings.
    QString text = dev.page();
    text.replace(u"\n"_s, u"\r\n"_s);
    text.prepend(u"Notatki o pracy — ręczne.\r\n\r\n"_s);
    text += u"- zwykła lista\r\n"_s;
    dev.write(text);
    dev.run();
    // The server changes one task: only its line changes, byte for byte the rest.
    server.putMember(u"praca"_s, u"a.ics"_s,
                     vtodo(u"a"_s, u"SUMMARY:Pierwsze — poprawione\r\nDUE;VALUE=DATE:20261124\r\nSTATUS:IN-PROCESS\r\n"
                                   u"PRIORITY:5\r\n"_s));
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.downloaded, 1);
    QString expected = text;
    expected.replace(u"- [ ] Pierwsze 📅 2026-11-21"_s, u"- [/] Pierwsze — poprawione 📅 2026-11-24 🔼"_s);
    QCOMPARE(dev.page(), expected);
    QCOMPARE(server.count("PUT"), 0);
}

void TestCalDavTasks::editsInTheNotes()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"a.ics"_s,
                     vtodo(u"a"_s, u"SUMMARY:Pierwsze\r\nDUE;VALUE=DATE:20261121\r\nX-APPLE-SORT-ORDER:7\r\nSEQUENCE:2\r\n"_s));
    Device dev(&server);
    dev.run();
    // Done in Brulion, with another date and a reminder.
    QString text = dev.page();
    text.replace(u"- [ ] Pierwsze 📅 2026-11-21"_s, u"- [x] Pierwsze ✔ 📅 2026-11-22 18:00 ⏰ 2026-11-22 17:00 ✅ 2026-11-20"_s);
    dev.write(text);
    server.clearLog();
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.uploaded, 1);
    QCOMPARE(server.count("PUT"), 1);
    QVERIFY(!server.requests.last().ifMatch.isEmpty() || server.requests.at(server.requests.size() - 1).method != "PUT");
    const QString data = memberText(server, u"a.ics"_s);
    QVERIFY(data.contains(u"SUMMARY:Pierwsze ✔"_s));
    QVERIFY(data.contains(u"STATUS:COMPLETED"_s));
    QVERIFY(data.contains(u"COMPLETED:"_s));
    QVERIFY(data.contains(u"DUE;TZID=Europe/Warsaw:20261122T180000"_s));
    QVERIFY(data.contains(u"BEGIN:VTIMEZONE"_s));
    QVERIFY(data.contains(u"X-APPLE-SORT-ORDER:7"_s)); // what Brulion does not know stays
    QVERIFY(data.contains(u"SEQUENCE:3"_s));
    // The reminder of a done task is not sent (it never rings).
    QVERIFY(!data.contains(u"BEGIN:VALARM"_s));
    // Nothing more to do afterwards.
    server.clearLog();
    const CalendarRunReport quiet = dev.run();
    QCOMPARE(quiet.uploaded + quiet.downloaded, 0);
    QCOMPARE(server.count("PUT"), 0);
}

void TestCalDavTasks::newLinesGoToTheServer()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    Device dev(&server);
    dev.run();
    QVERIFY(!dev.vault->exists(Page)); // nothing came: no page yet
    QDir().mkpath(dev.root + u"/Kalendarze"_s);
    dev.write(u"# Praca\n\n- [ ] Przygotować prezentację 📅 2026-11-25 ⏫\n  - [ ] Slajdy o wynikach\n"
              u"- [x] Stare, zrobione ✅ 2026-11-01\n- [ ] Z własnym id ^moje-id\n- zwykła notatka\n"_s);
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.uploaded, 3);
    const QString page = dev.page();
    // Each line got an id (the done one did not: it is not sent).
    static const QRegularExpression idRe(u"Przygotować prezentację 📅 2026-11-25 ⏫ \\^([a-z0-9]{12})\n"_s);
    const auto m = idRe.match(page);
    QVERIFY2(m.hasMatch(), qPrintable(page));
    QVERIFY(page.contains(u"- [x] Stare, zrobione ✅ 2026-11-01\n"_s));
    QVERIFY(page.contains(u"- [ ] Z własnym id ^moje-id\n"_s));
    QVERIFY(page.startsWith(u"# Praca\n\n"_s));
    const QString parentUid = uidForBlockId(m.captured(1));
    const QString parentMember = memberOfUid(server, parentUid);
    QVERIFY(!parentMember.isEmpty());
    QVERIFY(memberText(server, parentMember).contains(u"SUMMARY:Przygotować prezentację"_s));
    QVERIFY(memberText(server, parentMember).contains(u"PRIORITY:1"_s));
    const QString childMember = memberOfUid(server, u"brulion-"_s);
    Q_UNUSED(childMember);
    bool childFound = false;
    for (const QString &name : server.memberNames(u"praca"_s)) {
        const QString t = memberText(server, name);
        if (t.contains(u"SUMMARY:Slajdy o wynikach"_s)) {
            childFound = true;
            QVERIFY(t.contains(u"RELATED-TO;RELTYPE=PARENT:"_s + parentUid));
        }
    }
    QVERIFY(childFound);
    QVERIFY(!memberOfUid(server, u"brulion-moje-id"_s).isEmpty());
    QCOMPARE(server.memberNames(u"praca"_s).size(), 3);
    // Next time nothing goes again.
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0);
    QCOMPARE(dev.page(), page);
}

void TestCalDavTasks::bothSidesInDifferentFields()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze\r\nDUE;VALUE=DATE:20261121\r\n"_s));
    Device dev(&server);
    dev.run();
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze (serwer)\r\nDUE;VALUE=DATE:20261121\r\n"_s));
    QString text = dev.page();
    text.replace(u"📅 2026-11-21"_s, u"📅 2026-11-30 ⏳ 2026-11-29"_s);
    dev.write(text);
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.merged, 1);
    QVERIFY(report.conflicts.isEmpty());
    QVERIFY(dev.page().startsWith(u"- [ ] Pierwsze (serwer) 📅 2026-11-30 ⏳ 2026-11-29 ^"_s));
    const QString data = memberText(server, u"a.ics"_s);
    QVERIFY(data.contains(u"SUMMARY:Pierwsze (serwer)"_s));
    QVERIFY(data.contains(u"DUE;VALUE=DATE:20261130"_s));
    QVERIFY(data.contains(u"DTSTART;VALUE=DATE:20261129"_s));
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0);
}

void TestCalDavTasks::sameFieldNewerWins()
{
    for (const bool serverNewer : {true, false}) {
        FakeCalDav server;
        server.addCalendar(u"praca"_s, praca());
        server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze\r\n"_s));
        Device dev(&server);
        dev.run();
        QString text = dev.page();
        text.replace(u"Pierwsze"_s, u"Pierwsze (notatki)"_s);
        dev.write(text);
        // The note was written now; the server says when it changed its copy.
        const QString stamp = serverNewer ? u"20991231T000000Z"_s : u"20000101T000000Z"_s;
        server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze (serwer)\r\nLAST-MODIFIED:"_s + stamp + u"\r\n"_s));
        const CalendarRunReport report = dev.run();
        QCOMPARE(report.conflicts, QStringList{u"Pierwsze (notatki)"_s});
        const QString winner = serverNewer ? u"Pierwsze (serwer)"_s : u"Pierwsze (notatki)"_s;
        QVERIFY2(dev.page().startsWith(u"- [ ] "_s + winner + u" ^"_s), qPrintable(dev.page()));
        QVERIFY(memberText(server, u"a.ics"_s).contains(u"SUMMARY:"_s + winner));
        bool logged = false;
        for (const LogEntry &e : dev.store->log())
            logged = logged || (e.kind == LogEntry::Kind::Conflict && e.code == u"newer");
        QVERIFY(logged);
    }
}

void TestCalDavTasks::sameFieldKeepBoth()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze\r\n"_s));
    Device dev(&server, {}, ConflictPolicy::KeepBoth);
    dev.run();
    QString text = dev.page();
    text.replace(u"Pierwsze"_s, u"Pierwsze (notatki)"_s);
    dev.write(text);
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze (serwer)\r\n"_s));
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.conflicts.size(), 1);
    const QStringList lines = dev.page().split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.at(0).startsWith(u"- [ ] Pierwsze (notatki) ^"_s));
    QVERIFY(lines.at(1).startsWith(u"- [ ] Pierwsze (serwer) ^"_s));
    QVERIFY(memberText(server, u"a.ics"_s).contains(u"SUMMARY:Pierwsze (notatki)"_s));
    QCOMPARE(server.memberNames(u"praca"_s).size(), 2);
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0);
}

void TestCalDavTasks::deletionsBothWays()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    for (const QString &n : {u"a"_s, u"b"_s, u"c"_s, u"d"_s})
        server.putMember(u"praca"_s, n + u".ics"_s, vtodo(n, u"SUMMARY:Zadanie "_s + n + u"\r\n"_s));
    Device dev(&server);
    dev.run();
    QStringList lines = dev.page().split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 4);
    // a: deleted on the server, unchanged here -> its line goes.
    server.removeMember(u"praca"_s, u"a.ics"_s);
    // b: deleted here, unchanged on the server -> deleted there.
    // c: deleted on the server, but changed here -> written there again.
    // d: deleted here, but changed on the server -> its line comes back.
    server.removeMember(u"praca"_s, u"c.ics"_s);
    server.putMember(u"praca"_s, u"d.ics"_s, vtodo(u"d"_s, u"SUMMARY:Zadanie d, zmienione\r\n"_s));
    QString text;
    for (const QString &l : std::as_const(lines)) {
        if (l.contains(u"Zadanie b"_s) || l.contains(u"Zadanie d"_s))
            continue;
        text += (l.contains(u"Zadanie c"_s) ? QString(l).replace(u"Zadanie c"_s, u"Zadanie c, ważne"_s) : l) + u'\n';
    }
    dev.write(text);
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.deletedLocal, 1);
    QCOMPARE(report.deletedRemote, 1);
    const QString page = dev.page();
    QVERIFY(!page.contains(u"Zadanie a"_s));
    QVERIFY(!page.contains(u"Zadanie b"_s));
    QVERIFY(page.contains(u"Zadanie c, ważne"_s));
    QVERIFY(page.contains(u"Zadanie d, zmienione"_s));
    QVERIFY(!server.member(u"praca"_s, u"b.ics"_s));
    QVERIFY(server.member(u"praca"_s, u"c.ics"_s));
    QVERIFY(memberText(server, u"c.ics"_s).contains(u"SUMMARY:Zadanie c\\, ważne"_s));
    QCOMPARE(dev.state().tasks.size(), 2);
    server.clearLog();
    const CalendarRunReport quiet = dev.run();
    QCOMPARE(quiet.deletedLocal + quiet.deletedRemote + quiet.uploaded + quiet.downloaded, 0);
}

void TestCalDavTasks::massDeletionWaitsForConfirmation()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    for (int i = 0; i < 10; ++i)
        server.putMember(u"praca"_s, u"t%1.ics"_s.arg(i), vtodo(u"t%1"_s.arg(i), u"SUMMARY:Zadanie %1\r\n"_s.arg(i)));
    Device dev(&server);
    dev.run();
    // The page emptied (another program, a broken sync): nothing is deleted on the server yet.
    dev.write(u"# Praca\n"_s);
    CalendarRunReport report = dev.run();
    QVERIFY(report.needsConfirmation);
    QCOMPARE(report.pendingDeletions.size(), 10);
    QCOMPARE(server.memberNames(u"praca"_s).size(), 10);
    QCOMPARE(report.deletedRemote, 0);
    // Confirmed: done.
    report = dev.run(true);
    QVERIFY(!report.needsConfirmation);
    QCOMPARE(report.deletedRemote, 10);
    QVERIFY(server.memberNames(u"praca"_s).isEmpty());
    // A calendar suddenly empty on the server: nothing removed from the notes.
    FakeCalDav other;
    other.addCalendar(u"praca"_s, praca());
    for (int i = 0; i < 8; ++i)
        other.putMember(u"praca"_s, u"t%1.ics"_s.arg(i), vtodo(u"t%1"_s.arg(i), u"SUMMARY:Zadanie %1\r\n"_s.arg(i)));
    other.options().syncCollection = false; // a listing every time
    Device dev2(&other);
    dev2.run();
    const QString before = dev2.page();
    for (int i = 0; i < 8; ++i)
        other.removeMember(u"praca"_s, u"t%1.ics"_s.arg(i));
    report = dev2.run();
    QVERIFY(report.error.isError());
    QCOMPARE(dev2.page(), before);
}

void TestCalDavTasks::recurringTaskCompletedHere()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"basen.ics"_s,
                     vtodo(u"basen"_s, u"SUMMARY:Basen\r\nDUE;VALUE=DATE:20261123\r\nRRULE:FREQ=WEEKLY\r\n"_s));
    Device dev(&server);
    dev.run();
    const QString id = blockIdForUid(u"basen"_s);
    // Completed in Brulion: the next occurrence is written above it (SYNTAX §5).
    const QString completed = markdown::setTaskStatus(dev.page(), 0, TaskStatus::Done, QDate(2026, 11, 20));
    QVERIFY(completed.contains(u"📅 2026-11-30"_s));
    dev.write(completed);
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.uploaded, 1);
    const QStringList lines = dev.page().split(u'\n', Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QVERIFY2(lines.at(0).startsWith(u"- [ ] Basen 🔁 every week 📅 2026-11-30"_s) && lines.at(0).endsWith(u" ^"_s + id),
             qPrintable(lines.at(0)));
    QVERIFY(lines.at(1).startsWith(u"- [x] Basen"_s));
    QVERIFY(!lines.at(1).contains(u'^'));
    // The server's task goes on with the next date (as task apps keep a series).
    const QString data = memberText(server, u"basen.ics"_s);
    QVERIFY(data.contains(u"DUE;VALUE=DATE:20261130"_s));
    QVERIFY(!data.contains(u"STATUS:COMPLETED"_s));
    QCOMPARE(server.memberNames(u"praca"_s).size(), 1);
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0); // the done line stays the note's record
}

void TestCalDavTasks::secondDevicePairsLines()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze\r\n"_s));
    Device laptop(&server);
    laptop.write(u"- [ ] Z laptopa\n"_s);
    laptop.run();
    const QString page = laptop.page();
    // The phone gets the same notes (Syncthing) and syncs the calendar for the first time.
    QTemporaryDir phoneNotes;
    QDir().mkpath(phoneNotes.path() + u"/Kalendarze"_s);
    QFile f(phoneNotes.path() + u'/' + Page);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(page.toUtf8());
    f.close();
    Device phone(&server, phoneNotes.path());
    server.clearLog();
    const CalendarRunReport report = phone.run();
    QCOMPARE(report.uploaded, 0);
    QCOMPARE(server.count("PUT"), 0);
    QCOMPARE(phone.page(), page);
    QCOMPARE(phone.state().tasks.size(), 2);
    QCOMPARE(server.memberNames(u"praca"_s).size(), 2);
}

void TestCalDavTasks::movedLineKeepsSyncing()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze\r\n"_s));
    Device dev(&server);
    dev.run();
    // The line moved into a journal: still the same task.
    const QString line = dev.page().trimmed();
    QDir().mkpath(dev.root + u"/journals"_s);
    QVERIFY(dev.vault->writeText(u"journals/2026-11-20.md"_s, u"- Dzisiaj\n  "_s + line + u"\n"_s));
    dev.write(QString());
    dev.reindex();
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Pierwsze, z serwera\r\n"_s));
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.deletedRemote, 0);
    QVERIFY(dev.vault->readText(u"journals/2026-11-20.md"_s).contains(u"  - [ ] Pierwsze, z serwera ^"_s));
    QVERIFY(dev.page().isEmpty());
    QCOMPARE(dev.state().tasks.value(u"a"_s).note, u"journals/2026-11-20.md"_s);
}

void TestCalDavTasks::movedSubtaskUpdatesRelatedTo()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"a.ics"_s, vtodo(u"a"_s, u"SUMMARY:Ścieżka żółwia\r\n"_s));
    server.putMember(u"praca"_s, u"b.ics"_s, vtodo(u"b"_s, u"SUMMARY:Zażółć gęślą\r\n"_s));
    server.putMember(u"praca"_s, u"c.ics"_s,
                     vtodo(u"c"_s, u"SUMMARY:Jaźń\r\nRELATED-TO;RELTYPE=PARENT:a\r\nRELATED-TO;RELTYPE=SIBLING:x\r\n"_s));
    Device dev(&server);
    QVERIFY(!dev.run().error.isError());
    const QString ia = blockIdForUid(u"a"_s), ib = blockIdForUid(u"b"_s), ic = blockIdForUid(u"c"_s);
    QVERIFY(dev.page().contains(u"\t- [ ] Jaźń ^"_s + ic));
    QCOMPARE(dev.state().tasks.value(u"c"_s).local.parentUid, u"a"_s);

    // Nothing changed: nothing sent.
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0);

    // The subtask moved under another task.
    dev.write(u"- [ ] Ścieżka żółwia ^"_s + ia + u"\n- [ ] Zażółć gęślą ^"_s + ib + u"\n\t- [ ] Jaźń ^"_s + ic + u"\n"_s);
    server.clearLog();
    CalendarRunReport report = dev.run();
    QVERIFY2(!report.error.isError(), qPrintable(report.error.message));
    QCOMPARE(server.count("PUT"), 1);
    QCOMPARE(report.uploaded, 1);
    QString c = memberText(server, u"c.ics"_s);
    QVERIFY(c.contains(u"RELATED-TO;RELTYPE=PARENT:b"_s));
    QVERIFY(!c.contains(u"RELTYPE=PARENT:a"_s));
    QVERIFY(c.contains(u"RELATED-TO;RELTYPE=SIBLING:x"_s)); // other relations stay
    QVERIFY(c.contains(u"SUMMARY:Jaźń"_s));
    QCOMPARE(dev.state().tasks.value(u"c"_s).local.parentUid, u"b"_s);
    QCOMPARE(dev.state().tasks.value(u"c"_s).remote.parentUid, u"b"_s);
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0);

    // Out to the top level: the parent relation goes, the line stays where it is.
    dev.write(u"- [ ] Ścieżka żółwia ^"_s + ia + u"\n- [ ] Zażółć gęślą ^"_s + ib + u"\n- [ ] Jaźń ^"_s + ic + u"\n"_s);
    report = dev.run();
    QCOMPARE(report.uploaded, 1);
    c = memberText(server, u"c.ics"_s);
    QVERIFY(!c.contains(u"RELTYPE=PARENT"_s));
    QVERIFY(c.contains(u"RELTYPE=SIBLING:x"_s));
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0);

    // A top-level task under another one.
    dev.write(u"- [ ] Ścieżka żółwia ^"_s + ia + u"\n\t- [ ] Zażółć gęślą ^"_s + ib + u"\n- [ ] Jaźń ^"_s + ic + u"\n"_s);
    dev.run();
    QVERIFY(memberText(server, u"b.ics"_s).contains(u"RELATED-TO;RELTYPE=PARENT:a"_s));

    // Moved here and its text edited on the server in the same round: both are kept.
    dev.write(u"- [ ] Ścieżka żółwia ^"_s + ia + u"\n\t- [ ] Jaźń ^"_s + ic + u"\n- [ ] Zażółć gęślą ^"_s + ib + u"\n"_s);
    server.putMember(u"praca"_s, u"c.ics"_s, vtodo(u"c"_s, u"SUMMARY:Jaźń 2\r\nRELATED-TO;RELTYPE=SIBLING:x\r\n"_s));
    report = dev.run();
    QVERIFY2(report.conflicts.isEmpty(), "a move and an edit of different fields are no conflict");
    c = memberText(server, u"c.ics"_s);
    QVERIFY(c.contains(u"SUMMARY:Jaźń 2"_s));
    QVERIFY(c.contains(u"RELATED-TO;RELTYPE=PARENT:a"_s));
    QVERIFY(dev.page().contains(u"Jaźń 2 ^"_s + ic));
    QVERIFY(!memberText(server, u"b.ics"_s).contains(u"RELTYPE=PARENT"_s));

    // The server moved a task: its line stays (nesting is decided in the notes), and the move is
    // not undone there.
    server.putMember(u"praca"_s, u"c.ics"_s,
                     vtodo(u"c"_s, u"SUMMARY:Jaźń 2\r\nRELATED-TO;RELTYPE=PARENT:b\r\n"_s));
    server.clearLog();
    dev.run();
    QCOMPARE(server.count("PUT"), 0);
    QVERIFY(memberText(server, u"c.ics"_s).contains(u"RELTYPE=PARENT:b"_s));
    QVERIFY(dev.page().contains(u"\t- [ ] Jaźń 2 ^"_s + ic));
}

void TestCalDavTasks::eventsChangedHere()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    const QString etag = server.putMember(u"praca"_s, u"zebranie.ics"_s,
                                          vevent(u"zebranie"_s, u"DTSTART:20261121T090000Z\r\nSUMMARY:Zebranie\r\n"
                                                                u"LAST-MODIFIED:20261101T000000Z\r\n"_s));
    Device dev(&server);
    dev.run();
    QCOMPARE(dev.state().resources.size(), 1);
    // A new event and an edit, queued (offline) and sent with the next run.
    EventChange create;
    create.kind = EventChange::Kind::Create;
    create.uid = u"nowe-wydarzenie"_s;
    create.data = vevent(u"nowe-wydarzenie"_s, u"DTSTART:20261122T090000Z\r\nSUMMARY:Śniadanie\r\n"_s);
    QVERIFY(dev.sync->queueEvent(u"c-1"_s, create));
    EventChange edit;
    edit.kind = EventChange::Kind::Update;
    edit.uid = u"zebranie"_s;
    edit.path = server.homePath() + u"praca/zebranie.ics"_s;
    edit.data = vevent(u"zebranie"_s, u"DTSTART:20261121T100000Z\r\nSUMMARY:Zebranie (przesunięte)\r\n"_s);
    QVERIFY(dev.sync->queueEvent(u"c-1"_s, edit));
    QCOMPARE(dev.state().outbox.size(), 2);
    QCOMPARE(dev.state().resources.size(), 2); // shown at once
    const CalendarRunReport report = dev.run();
    QCOMPARE(report.uploaded, 2);
    QVERIFY(dev.state().outbox.isEmpty());
    QVERIFY(memberText(server, u"zebranie.ics"_s).contains(u"przesunięte"_s));
    QVERIFY(memberText(server, u"nowe-wydarzenie.ics"_s).contains(u"Śniadanie"_s));
    // Changed on the server meanwhile (newer): the server's version stays, the conflict is logged.
    edit.data = vevent(u"zebranie"_s, u"DTSTART:20261121T110000Z\r\nSUMMARY:Zebranie (moje)\r\n"_s);
    edit.at = QDateTime(QDate(2026, 11, 20), QTime(9, 0), QTimeZone::UTC);
    edit.ifMatch.clear();
    QVERIFY(dev.sync->queueEvent(u"c-1"_s, edit));
    server.putMember(u"praca"_s, u"zebranie.ics"_s,
                     vevent(u"zebranie"_s, u"DTSTART:20261121T120000Z\r\nSUMMARY:Zebranie (serwer)\r\n"
                                           u"LAST-MODIFIED:20261120T100000Z\r\n"_s));
    const CalendarRunReport conflict = dev.run();
    QCOMPARE(conflict.conflicts.size(), 1);
    QVERIFY(memberText(server, u"zebranie.ics"_s).contains(u"(serwer)"_s));
    QVERIFY(dev.state().resources.value(server.homePath() + u"praca/zebranie.ics"_s).data.contains("(serwer)"));
    // Deleted here: deleted there.
    EventChange del;
    del.kind = EventChange::Kind::Delete;
    del.uid = u"nowe-wydarzenie"_s;
    del.path = server.homePath() + u"praca/nowe-wydarzenie.ics"_s;
    QVERIFY(dev.sync->queueEvent(u"c-1"_s, del));
    dev.run();
    QVERIFY(!server.member(u"praca"_s, u"nowe-wydarzenie.ics"_s));
    Q_UNUSED(etag);
}

void TestCalDavTasks::eventsFromTheInbox()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"stand.ics"_s,
                     vevent(u"stand"_s, u"DTSTART;TZID=Europe/Warsaw:20261116T093000\r\nDTEND;TZID=Europe/Warsaw:20261116T094500\r\n"
                                        u"RRULE:FREQ=DAILY;COUNT=5\r\nSUMMARY:Stand-up\r\n"_s));
    Device dev(&server);
    dev.run();
    const QString folder = dev.store->folder();
    int minute = 0;
    const auto post = [&folder, &minute](EventIntent i) {
        i.at = QDateTime(QDate(2026, 11, 20), QTime(9, minute++), QTimeZone::UTC);
        i.id = newIntentId(i.at);
        if (i.calendarId.isEmpty())
            i.calendarId = u"c-1"_s;
        return postIntent(folder, i);
    };
    // brulion-cli asked: a meeting, one stand-up left out, an event of a calendar gone, a change of
    // an event that is not there — and a file that is no change at all.
    EventIntent create;
    create.kind = EventIntent::Kind::Create;
    create.uid = u"brulion-event-cli"_s;
    create.patch.title = u"Spotkanie z Łucją"_s;
    create.patch.times.start = EventMoment(DateSpec{QDate(2026, 11, 23), QTime(10, 0)});
    create.patch.reminderMinutes = 15;
    QVERIFY(post(create));
    EventIntent skip;
    skip.kind = EventIntent::Kind::Delete;
    skip.uid = u"stand"_s;
    skip.instance = instanceFromText(u"20261118T093000@Europe/Warsaw"_s);
    QVERIFY(post(skip));
    EventIntent gone = create;
    gone.calendarId = u"c-9"_s;
    QVERIFY(post(gone));
    EventIntent missing;
    missing.uid = u"brak"_s;
    missing.patch.title = u"Nic"_s;
    QVERIFY(post(missing));
    QFile broken(inboxFolder(folder) + u"/20261120T095900000-zepsuty.json"_s);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("{\"kind\": \"cre");
    broken.close();
    // Taken at once, before any run: shown, waiting to be sent, the inbox empty; the refused ones
    // in the log with what was wrong.
    QSignalSpy changed(dev.sync.get(), &CalendarSync::calendarChanged);
    const int logged = dev.store->log().size();
    QCOMPARE(dev.sync->takeInbox(), 2);
    QVERIFY(pendingIntents(folder).isEmpty());
    QCOMPARE(dev.state().outbox.size(), 2);
    QVERIFY(!changed.isEmpty());
    const QList<LogEntry> log = dev.store->log().mid(logged);
    QCOMPARE(log.size(), 3);
    for (const LogEntry &e : log) {
        QCOMPARE(e.kind, LogEntry::Kind::Error);
        QCOMPARE(e.code, u"cli-change"_s);
    }
    QVERIFY(log.at(0).text.contains(u"No calendar"_s));
    QVERIFY(log.at(1).text.contains(u"No event"_s));
    QCOMPARE(log.at(1).name, u"Praca"_s);
    QVERIFY(log.at(2).text.contains(u"zepsuty"_s));
    // Sent with the next run.
    dev.run();
    QVERIFY(dev.state().outbox.isEmpty());
    const QString made = memberOfUid(server, u"brulion-event-cli"_s);
    QVERIFY(memberText(server, made).contains(u"SUMMARY:Spotkanie z Łucją"_s));
    QVERIFY(memberText(server, made).contains(u"DTSTART;TZID=Europe/Warsaw:20261123T100000"_s));
    QVERIFY(memberText(server, made).contains(u"TRIGGER:-PT15M"_s));
    QVERIFY(memberText(server, u"stand.ics"_s).contains(u"EXDATE;TZID=Europe/Warsaw:20261118T093000"_s));
    // Taken twice (its file came back): still one event; the run takes it as it starts.
    QVERIFY(post(create));
    dev.run();
    QVERIFY(pendingIntents(folder).isEmpty());
    int copies = 0;
    for (const QString &name : server.memberNames(u"praca"_s))
        copies += memberText(server, name).contains(u"UID:brulion-event-cli"_s) ? 1 : 0;
    QCOMPARE(copies, 1);
    // Another process of the device syncs (it holds the lock): the inbox waits for it.
    EventIntent rename;
    rename.uid = u"brulion-event-cli"_s;
    rename.patch.title = u"Spotkanie z Łucją i Kubą"_s;
    QVERIFY(post(rename));
    {
        QLockFile other(folder + u"/sync.lock"_s);
        QVERIFY(other.tryLock(0));
        QCOMPARE(dev.sync->takeInbox(), 0);
        QCOMPARE(pendingIntents(folder).size(), 1);
    }
    QCOMPARE(dev.sync->takeInbox(), 1);
    dev.run();
    QVERIFY(memberText(server, made).contains(u"SUMMARY:Spotkanie z Łucją i Kubą"_s));
}

void TestCalDavTasks::brokenMembersAndOffline()
{
    FakeCalDav server;
    server.addCalendar(u"praca"_s, praca());
    server.putMember(u"praca"_s, u"zly.ics"_s, "to nie jest kalendarz");
    server.putMember(u"praca"_s, u"bez-uid.ics"_s, "BEGIN:VCALENDAR\r\nBEGIN:VTODO\r\nSUMMARY:Bez UID\r\nEND:VTODO\r\nEND:VCALENDAR\r\n");
    server.putMember(u"praca"_s, u"ok.ics"_s, vtodo(u"ok"_s, u"SUMMARY:Dobre\r\n"_s));
    Device dev(&server);
    const CalendarRunReport report = dev.run();
    QVERIFY(!report.error.isError());
    QCOMPARE(dev.page().count(u"- [ ]"_s), 1);
    // The server cannot be reached: an error, the notes as they were.
    const QString before = dev.page();
    server.options().origin = u"https://gdzie-indziej.example.pl"_s;
    const CalendarRunReport offline = dev.run();
    QVERIFY(offline.error.is(sync::WebDavErrorCode::Network));
    QCOMPARE(dev.page(), before);
    QCOMPARE(dev.state().error, u"network"_s);
    // No password: an error of its own.
    dev.sync->setPasswords([](const QString &) { return QString(); });
    const CalendarRunReport noPassword = dev.run();
    QCOMPARE(noPassword.failed, QStringList{u"c-1"_s});
}

QTEST_GUILESS_MAIN(TestCalDavTasks)
#include "tst_caldav_tasks.moc"
