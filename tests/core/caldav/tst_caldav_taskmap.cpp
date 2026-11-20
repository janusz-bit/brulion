// Tasks of CalDAV as task lines: fields read from a line and from a VTODO, a line edited token by
// token (the rest kept byte for byte), a VTODO patched (what Brulion does not know stays), new
// lines and VTODOs, how a line keeps its UID, the VTIMEZONE of the device's zone.
#include "caldav/taskmap.h"

#include "markdown/parse.h"
#include "markdown/tokens.h"

#include <QJsonDocument>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

namespace {

const QTimeZone Warsaw("Europe/Warsaw");

Block taskBlock(const QString &line)
{
    const ParsedNote note = markdown::parseNote(u"a.md"_s, line);
    for (const Block &b : note.blocks) {
        if (b.task)
            return b;
    }
    return {};
}

TaskFields fromTodo(const QString &todoLines, const QString &extra = {})
{
    const QByteArray ics = (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"_s + extra + u"BEGIN:VTODO\r\nUID:t1\r\n"_s + todoLines
                            + u"END:VTODO\r\nEND:VCALENDAR\r\n"_s)
                                   .toUtf8();
    const QList<io::ical::Component> roots = io::ical::parse(ics);
    ZoneTable zones;
    for (const io::ical::Component &r : roots)
        zones.addCalendar(r);
    return fieldsFromTodo(*firstTodo(roots), zones, Warsaw, QTime(9, 0));
}

} // namespace

class TestCalDavTaskMap : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void fieldsFromLine();
    void fieldsFromVtodo();
    void newLine();
    void applyToBody_data();
    void applyToBody();
    void lineStaysLineAfterEdits();
    void patchKeepsUnknown();
    void newTodoHasZone();
    void uids();
    void json();
    void normalisation();
};

void TestCalDavTaskMap::initTestCase()
{
    if (!Warsaw.isValid())
        QSKIP("no time zone database");
}

void TestCalDavTaskMap::fieldsFromLine()
{
    const Block b = taskBlock(u"- [/] Zadzwonić do #Ania jutro #praca #pilne ⏫ 🔁 every week 📅 2026-11-21 18:00 "
                              u"⏳ 2026-11-20 ⏰ 2026-11-21 17:45 ⏰! 2026-11-21 17:55 ^c1\n"
                              u"  Numer: 600 100 200\n  \\- nie zapomnieć\n  status:: ważne\n"_s);
    QVERIFY(b.task);
    const TaskFields f = fieldsFromBlock(b, QTime(9, 0));
    QCOMPARE(f.summary, u"Zadzwonić do #Ania jutro"_s); // a tag in the sentence stays text
    QCOMPARE(f.categories, (QStringList{u"praca"_s, u"pilne"_s}));
    QCOMPARE(f.status, TaskStatus::Doing);
    QCOMPARE(f.priority, Priority::High);
    QCOMPARE(f.recurrence, u"every week"_s);
    QCOMPARE(f.due, (DateSpec{QDate(2026, 11, 21), QTime(18, 0)}));
    QCOMPARE(f.start, (DateSpec{QDate(2026, 11, 20), std::nullopt}));
    QCOMPARE(f.reminders.size(), 2);
    QVERIFY(f.reminders.at(1).alarm);
    QCOMPARE(f.description, u"Numer: 600 100 200\n- nie zapomnieć"_s);
}

void TestCalDavTaskMap::fieldsFromVtodo()
{
    const QString zone = u"BEGIN:VTIMEZONE\r\nTZID:Moja strefa\r\nBEGIN:STANDARD\r\nDTSTART:19700101T000000\r\n"
                         u"TZOFFSETFROM:-0500\r\nTZOFFSETTO:-0500\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"_s;
    TaskFields f = fromTodo(u"SUMMARY:Kupić mleko\\, masło\r\nSTATUS:NEEDS-ACTION\r\nDUE;TZID=America/New_York:20261121T120000\r\n"
                            u"DTSTART;VALUE=DATE:20261120\r\nPRIORITY:2\r\nCATEGORIES:Zakupy,Dom\r\nCATEGORIES:Ważne\r\n"
                            u"RRULE:FREQ=WEEKLY;BYDAY=MO,FR\r\nDESCRIPTION:Pierwsza\\nDruga\r\n"
                            u"RELATED-TO;RELTYPE=PARENT:rodzic\r\n"
                            u"BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER;RELATED=END:-PT15M\r\nEND:VALARM\r\n"
                            u"BEGIN:VALARM\r\nACTION:AUDIO\r\nTRIGGER;VALUE=DATE-TIME:20261121T160000Z\r\nEND:VALARM\r\n"
                            u"BEGIN:VALARM\r\nACTION:EMAIL\r\nTRIGGER:-P1D\r\nEND:VALARM\r\n"_s);
    QCOMPARE(f.summary, u"Kupić mleko, masło"_s);
    QCOMPARE(f.status, TaskStatus::Todo);
    QCOMPARE(f.due, (DateSpec{QDate(2026, 11, 21), QTime(18, 0)}));
    QCOMPARE(f.start, (DateSpec{QDate(2026, 11, 20), std::nullopt}));
    QCOMPARE(f.priority, Priority::High);
    QCOMPARE(f.categories, (QStringList{u"Zakupy"_s, u"Dom"_s, u"Ważne"_s}));
    QCOMPARE(f.recurrence, u"every week on Monday, Friday"_s);
    QCOMPARE(f.description, u"Pierwsza\nDruga"_s);
    QCOMPARE(f.parentUid, u"rodzic"_s);
    QCOMPARE(f.reminders.size(), 2); // the e-mail alarm is not Brulion's
    QCOMPARE(f.reminders.at(0).at, (DateSpec{QDate(2026, 11, 21), QTime(17, 45)}));
    QCOMPARE(f.reminders.at(1).at, (DateSpec{QDate(2026, 11, 21), QTime(17, 0)}));
    QVERIFY(f.reminders.at(1).alarm);
    // Completed, a custom zone, a summary with token characters.
    f = fromTodo(u"SUMMARY:Raport 📅 2026-01-01 ^abc\r\nSTATUS:COMPLETED\r\nCOMPLETED:20261119T230500Z\r\n"
                 u"DUE;TZID=Moja strefa:20261119T120000\r\nPRIORITY:9\r\n"_s,
                 zone);
    QCOMPARE(f.summary, u"Raport 2026-01-01"_s);
    QCOMPARE(f.status, TaskStatus::Done);
    QCOMPARE(f.completed, QDate(2026, 11, 20)); // 23:05 UTC is past midnight in Warsaw
    QCOMPARE(f.due, (DateSpec{QDate(2026, 11, 19), QTime(18, 0)}));
    QCOMPARE(f.priority, Priority::Low);
    // A rule Brulion cannot write keeps its simplest form.
    f = fromTodo(u"SUMMARY:X\r\nRRULE:FREQ=MONTHLY;BYDAY=2TU\r\n"_s);
    QCOMPARE(f.recurrence, QString());
    f = fromTodo(u"SUMMARY:X\r\nRRULE:FREQ=DAILY;COUNT=5\r\nSTATUS:IN-PROCESS\r\n"_s);
    QCOMPARE(f.recurrence, u"every day"_s);
    QCOMPARE(f.status, TaskStatus::Doing);
    f = fromTodo(u"SUMMARY:X\r\nSTATUS:CANCELLED\r\n"_s);
    QCOMPARE(f.status, TaskStatus::Cancelled);
}

void TestCalDavTaskMap::newLine()
{
    TaskFields f;
    f.summary = u"Zapłacić rachunek za prąd"_s;
    f.categories = {u"Dom i ogród"_s, u"2026"_s, u"rachunki"_s};
    f.priority = Priority::Medium;
    f.recurrence = u"every month on the 10th"_s;
    f.start = DateSpec{QDate(2026, 12, 1), std::nullopt};
    f.due = DateSpec{QDate(2026, 12, 10), QTime(12, 0)};
    f.reminders = {{DateSpec{QDate(2026, 12, 10), QTime(9, 0)}, false}};
    f.description = u"Konto: 12 3456\n- przelewem\n# nie gotówką"_s;
    const QString body = newTaskBody(f, u"cabc123def456"_s);
    QCOMPARE(body, u"[ ] Zapłacić rachunek za prąd #Dom-i-ogród #rachunki 🔼 🔁 every month on the 10th ⏳ 2026-12-01 "
                   u"📅 2026-12-10 12:00 ⏰ 2026-12-10 09:00 ^cabc123def456\nKonto: 12 3456\n\\- przelewem\n\\# nie gotówką"_s);
    // Read back, the line has the same fields (categories as tags).
    const Block b = taskBlock(u"- "_s + QString(body).replace(u"\n"_s, u"\n  "_s));
    TaskFields back = fieldsFromBlock(b, QTime(9, 0));
    QCOMPARE(b.id, u"cabc123def456"_s);
    TaskFields expected = f;
    expected.categories = {u"Dom-i-ogród"_s, u"rachunki"_s};
    QCOMPARE(changedFields(back, expected), 0u);
    // Done: ✅ and no reminders.
    f.status = TaskStatus::Done;
    f.completed = QDate(2026, 12, 9);
    QVERIFY(newTaskBody(f, u"x"_s).contains(u"✅ 2026-12-09 ^x"_s));
    QVERIFY(!newTaskBody(f, u"x"_s).contains(u"⏰"_s));
}

void TestCalDavTaskMap::applyToBody_data()
{
    QTest::addColumn<QString>("body");
    QTest::addColumn<unsigned>("mask");
    QTest::addColumn<QString>("expected");
    TaskFields f;
    // (The fields each row sets are described by its mask; the values come from `values()` below.)
    QTest::newRow("done") << u"[ ] Kupić mleko 📅 2026-11-21 ^c1"_s << unsigned(FieldStatus)
                          << u"[x] Kupić mleko 📅 2026-11-21 ✅ 2026-11-20 ^c1"_s;
    QTest::newRow("logseq keyword") << u"TODO Kupić mleko ^c1"_s << unsigned(FieldStatus) << u"DONE Kupić mleko ✅ 2026-11-20 ^c1"_s;
    QTest::newRow("due changed") << u"[ ] Kupić mleko 📅 2026-11-21 ^c1"_s << unsigned(FieldDue)
                                 << u"[ ] Kupić mleko 📅 2026-11-22 18:00 ^c1"_s;
    QTest::newRow("due added") << u"[ ] Kupić mleko ^c1"_s << unsigned(FieldDue) << u"[ ] Kupić mleko 📅 2026-11-22 18:00 ^c1"_s;
    QTest::newRow("start on 🛫") << u"[ ] Kupić mleko 🛫 2026-11-01 ^c1"_s << unsigned(FieldStart)
                                 << u"[ ] Kupić mleko 🛫 2026-11-19 ^c1"_s;
    QTest::newRow("priority removed") << u"[ ] Kupić mleko ⏫ 📅 2026-11-21 ^c1"_s << unsigned(FieldPriority)
                                      << u"[ ] Kupić mleko 📅 2026-11-21 ^c1"_s;
    QTest::newRow("summary, tokens and tags kept") << u"[ ] Kupić mleko #zakupy ⏫ 📅 2026-11-21 ^c1"_s << unsigned(FieldSummary)
                                                   << u"[ ] Kupić mleko i chleb #zakupy ⏫ 📅 2026-11-21 ^c1"_s;
    QTest::newRow("categories") << u"[ ] Kupić mleko #zakupy ⏫ ^c1"_s << unsigned(FieldCategories)
                                << u"[ ] Kupić mleko #dom #Ważne ⏫ ^c1"_s;
    QTest::newRow("reminders replaced") << u"[ ] Kupić mleko ⏰ 2026-11-21 09:00 ^c1"_s << unsigned(FieldReminders)
                                        << u"[ ] Kupić mleko ⏰! 2026-11-22 17:30 ^c1"_s;
    QTest::newRow("recurrence") << u"[ ] Basen 🔁 every week 📅 2026-11-23 ^c1"_s << unsigned(FieldRecurrence)
                                << u"[ ] Basen 🔁 every 2 weeks 📅 2026-11-23 ^c1"_s;
    QTest::newRow("description, properties kept") << u"[ ] Kupić mleko ^c1\nstara linia\nstatus:: ważne"_s
                                                  << unsigned(FieldDescription)
                                                  << u"[ ] Kupić mleko ^c1\nstatus:: ważne\nnowa\n\\- lista"_s;
    QTest::newRow("cancelled") << u"[ ] Kupić mleko ^c1"_s << unsigned(FieldStatus) << u"[-] Kupić mleko ❌ 2026-11-20 ^c1"_s;
    Q_UNUSED(f);
}

void TestCalDavTaskMap::applyToBody()
{
    QFETCH(QString, body);
    QFETCH(unsigned, mask);
    QFETCH(QString, expected);
    TaskFields f;
    f.summary = u"Kupić mleko i chleb"_s;
    f.status = QString::fromUtf8(QTest::currentDataTag()) == u"cancelled" ? TaskStatus::Cancelled : TaskStatus::Done;
    f.completed = QDate(2026, 11, 20);
    f.due = DateSpec{QDate(2026, 11, 22), QTime(18, 0)};
    f.start = DateSpec{QDate(2026, 11, 19), std::nullopt};
    f.priority = Priority::None;
    f.categories = {u"dom"_s, u"Ważne"_s};
    f.reminders = {{DateSpec{QDate(2026, 11, 22), QTime(17, 30)}, true}};
    f.recurrence = u"every 2 weeks"_s;
    f.description = u"nowa\n- lista"_s;
    if (mask & FieldReminders)
        f.status = TaskStatus::Todo;
    QCOMPARE(applyFieldsToBody(body, f, mask, QDate(2026, 11, 20)), expected);
}

void TestCalDavTaskMap::lineStaysLineAfterEdits()
{
    // Every field at once: the line still parses to them.
    TaskFields f;
    f.summary = u"Napisać list, ważny"_s;
    f.status = TaskStatus::Doing;
    f.due = DateSpec{QDate(2027, 1, 5), std::nullopt};
    f.priority = Priority::Low;
    f.categories = {u"listy"_s};
    f.reminders = {{DateSpec{QDate(2027, 1, 4), QTime(8, 0)}, false}};
    f.recurrence = u"every year"_s;
    const QString body = applyFieldsToBody(u"[ ] Stare ⏫ ^cid"_s, f, AllFields, QDate(2026, 11, 20));
    const Block b = taskBlock(u"- "_s + body);
    QCOMPARE(b.id, u"cid"_s);
    QCOMPARE(changedFields(fieldsFromBlock(b, QTime(9, 0)), f), 0u);
}

void TestCalDavTaskMap::patchKeepsUnknown()
{
    const QByteArray ics = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Nextcloud Tasks//EN\r\nBEGIN:VTODO\r\nUID:t1\r\n"
                           "SUMMARY:Stare\r\nX-APPLE-SORT-ORDER:42\r\nSEQUENCE:3\r\nCLASS:PRIVATE\r\nPERCENT-COMPLETE:40\r\n"
                           "ATTACH:https://example.pl/plik.pdf\r\nBEGIN:VALARM\r\nACTION:EMAIL\r\nTRIGGER:-P1D\r\n"
                           "SUMMARY:x\r\nEND:VALARM\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT5M\r\nEND:VALARM\r\n"
                           "END:VTODO\r\nEND:VCALENDAR\r\n";
    TaskFields f;
    f.summary = u"Nowe; z przecinkiem, i ąę"_s;
    f.status = TaskStatus::Done;
    f.completed = QDate(2026, 11, 20);
    f.reminders = {{DateSpec{QDate(2026, 11, 21), QTime(9, 0)}, false}};
    TodoContext ctx;
    ctx.now = QDateTime(QDate(2026, 11, 20), QTime(10, 0), Warsaw);
    ctx.zone = Warsaw;
    const QByteArray patched = patchTodo(ics, f, FieldSummary | FieldStatus | FieldReminders, ctx);
    QVERIFY(!patched.isEmpty());
    const QString text = QString::fromUtf8(patched);
    QVERIFY(text.contains(u"X-APPLE-SORT-ORDER:42"_s));
    QVERIFY(text.contains(u"CLASS:PRIVATE"_s));
    QVERIFY(text.contains(u"ATTACH:https://example.pl/plik.pdf"_s));
    QVERIFY(text.contains(u"ACTION:EMAIL"_s));
    QVERIFY(!text.contains(u"TRIGGER:-PT5M"_s)); // Brulion's alarms replaced
    QVERIFY(!text.contains(u"TRIGGER;VALUE=DATE-TIME"_s)); // a done task rings no more
    QVERIFY(text.contains(u"SUMMARY:Nowe\\; z przecinkiem\\, i ąę"_s));
    QVERIFY(text.contains(u"STATUS:COMPLETED"_s));
    QVERIFY(text.contains(u"COMPLETED:20261120T090000Z"_s));
    QVERIFY(text.contains(u"PERCENT-COMPLETE:100"_s));
    QVERIFY(text.contains(u"SEQUENCE:4"_s));
    QVERIFY(text.contains(u"PRODID:-//Nextcloud Tasks//EN"_s));
    // Read back: the fields written.
    const QList<io::ical::Component> roots = io::ical::parse(patched);
    const TaskFields back = fieldsFromTodo(*firstTodo(roots), ZoneTable(), Warsaw, QTime(9, 0));
    QCOMPARE(changedFields(back, f) & (FieldSummary | FieldStatus | FieldReminders | FieldCompleted), 0u);
    // Reopened: COMPLETED goes, the old PERCENT-COMPLETE of 100 too.
    f.status = TaskStatus::Todo;
    const QString reopened = QString::fromUtf8(patchTodo(patched, f, FieldStatus | FieldReminders, ctx));
    QVERIFY(reopened.contains(u"TRIGGER;VALUE=DATE-TIME:20261121T080000Z"_s));
    QVERIFY(!reopened.contains(u"COMPLETED:"_s));
    QVERIFY(!reopened.contains(u"PERCENT-COMPLETE"_s));
    QVERIFY(reopened.contains(u"STATUS:NEEDS-ACTION"_s));
    QVERIFY(patchTodo("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n", f, FieldStatus, ctx).isEmpty());
}

void TestCalDavTaskMap::newTodoHasZone()
{
    TaskFields f;
    f.summary = u"Spotkanie"_s;
    f.due = DateSpec{QDate(2026, 11, 21), QTime(18, 0)};
    f.parentUid = u"rodzic-1"_s;
    TodoContext ctx;
    ctx.now = QDateTime(QDate(2026, 11, 20), QTime(10, 0), Warsaw);
    ctx.zone = Warsaw;
    const QByteArray ics = newTodo(u"brulion-abc"_s, f, ctx);
    const QString text = QString::fromUtf8(ics);
    QVERIFY(text.contains(u"BEGIN:VTIMEZONE\r\nTZID:Europe/Warsaw"_s));
    QVERIFY(text.contains(u"DUE;TZID=Europe/Warsaw:20261121T180000"_s));
    QVERIFY(text.contains(u"RELATED-TO;RELTYPE=PARENT:rodzic-1"_s));
    QVERIFY(text.contains(u"UID:brulion-abc"_s));
    QVERIFY(text.contains(u"STATUS:NEEDS-ACTION"_s));
    // The VTIMEZONE written reads back as Warsaw's rules (no database needed to read it).
    const QList<io::ical::Component> roots = io::ical::parse(ics);
    ZoneTable zones;
    zones.addCalendar(roots.first());
    const ZoneDefinition *zone = zones.definition(u"Europe/Warsaw"_s);
    QVERIFY(zone);
    QCOMPARE(zone->offsetAtUtc(QDateTime(QDate(2027, 7, 1), QTime(12, 0), QTimeZone::UTC).toMSecsSinceEpoch()), 7200);
    QCOMPARE(zone->offsetAtUtc(QDateTime(QDate(2027, 12, 1), QTime(12, 0), QTimeZone::UTC).toMSecsSinceEpoch()), 3600);
    // UTC: no zone.
    ctx.zone = QTimeZone(QTimeZone::UTC);
    QVERIFY(!QString::fromUtf8(newTodo(u"u"_s, f, ctx)).contains(u"VTIMEZONE"_s));
}

void TestCalDavTaskMap::uids()
{
    QCOMPARE(blockIdForUid(u"brulion-k3x9a2b4c6d8"_s), u"k3x9a2b4c6d8"_s);
    QCOMPARE(uidForBlockId(u"k3x9a2b4c6d8"_s), u"brulion-k3x9a2b4c6d8"_s);
    const QString id = blockIdForUid(u"040000008200E00074C5B7101A82E00800000000@google.com"_s);
    QCOMPARE(id.size(), 13);
    QVERIFY(id.startsWith(u'c'));
    QVERIFY(markdown::isValidBlockId(id));
    QCOMPARE(blockIdForUid(u"040000008200E00074C5B7101A82E00800000000@google.com"_s), id); // stable
    QVERIFY(blockIdForUid(u"zażółć-gęślą"_s) != id);
    QVERIFY(blockIdForUid(u"brulion-"_s).startsWith(u'c'));       // nothing after the prefix
    QVERIFY(blockIdForUid(u"brulion-a b"_s).startsWith(u'c'));    // not an id
    const QString fresh = newTaskBlockId();
    QCOMPARE(fresh.size(), 12);
    QVERIFY(markdown::isValidBlockId(fresh));
    QVERIFY(fresh != newTaskBlockId());
}

void TestCalDavTaskMap::json()
{
    TaskFields f;
    f.summary = u"Żółw"_s;
    f.status = TaskStatus::Deferred;
    f.due = DateSpec{QDate(2026, 11, 21), QTime(18, 0)};
    f.start = DateSpec{QDate(2026, 11, 20), std::nullopt};
    f.priority = Priority::Highest;
    f.completed = QDate(2026, 11, 19);
    f.recurrence = u"every day"_s;
    f.reminders = {{DateSpec{QDate(2026, 11, 21), QTime(9, 0)}, true}};
    f.categories = {u"a"_s};
    f.description = u"opis\nlinia"_s;
    f.parentUid = u"p"_s;
    QCOMPARE(TaskFields::fromJson(f.toJson()), f);
    QCOMPARE(TaskFields::fromJson(QJsonDocument::fromJson("{\"status\": 99, \"due\": \"jutro\"}").object()).status,
             TaskStatus::Deferred);
}

void TestCalDavTaskMap::normalisation()
{
    TaskFields a;
    a.summary = u"  Kupić   mleko "_s;
    a.status = TaskStatus::Deferred;
    a.priority = Priority::Highest;
    a.completed = QDate(2026, 1, 1); // not done: no completion date
    a.categories = {u"Zakupy"_s, u"dom"_s};
    a.recurrence = u"co tydzień"_s;
    TaskFields b;
    b.summary = u"Kupić mleko"_s;
    b.status = TaskStatus::Todo;
    b.priority = Priority::High;
    b.categories = {u"DOM"_s, u"zakupy"_s};
    b.recurrence = u"every week"_s;
    QCOMPARE(changedFields(a, b), 0u);
    b.priority = Priority::Medium;
    QCOMPARE(changedFields(a, b), unsigned(FieldPriority));
    const TaskFields merged = withFields(a, b, FieldPriority);
    QCOMPARE(merged.priority, Priority::Medium);
    QCOMPARE(merged.summary, a.summary);
}

QTEST_GUILESS_MAIN(TestCalDavTaskMap)
#include "tst_caldav_taskmap.moc"
