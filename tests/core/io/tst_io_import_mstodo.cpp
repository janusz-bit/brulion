// Microsoft To Do importer: Graph's todoTask fields as task lines (status, importance, dates in
// the vault's zone, recurrence, reminders, categories), notes, steps, links and attachments,
// the input shapes (Brulion's download bundle, Graph Explorer's lists?$expand=tasks, one list's
// tasks, several files, a ZIP), paging that left tasks out, lists and malformed input. The
// fixtures are synthetic, built from Microsoft Graph's todoTask / todoTaskList documentation.
#include "iotest.h"

#include "centraleuropeantime.h"
#include "io/importer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

const QString Frontmatter = u"---\nsource: Microsoft To Do\n---\n"_s;

QJsonObject parse(const QString &json)
{
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        qWarning() << "bad fixture" << json << error.errorString();
    return doc.object();
}

QJsonObject list(const QString &name, const QJsonArray &tasks, const QString &wellknown = u"none"_s)
{
    return QJsonObject{{u"id"_s, QString(u"AAMkAG"_s + name)},
                       {u"displayName"_s, name},
                       {u"isOwner"_s, true},
                       {u"isShared"_s, false},
                       {u"wellknownListName"_s, wellknown},
                       {u"tasks"_s, tasks}};
}

/// Brulion's download bundle (the mstodo module writes it).
QByteArray bundle(const QJsonArray &lists)
{
    return QJsonDocument(QJsonObject{{u"brulion"_s, u"mstodo-export"_s},
                                     {u"version"_s, 1},
                                     {u"exported"_s, u"2026-10-27T10:00:00Z"_s},
                                     {u"lists"_s, lists}})
            .toJson();
}

/// Graph Explorer's `GET /me/todo/lists?$expand=tasks`.
QByteArray explorer(const QJsonArray &lists, const QString &nextLink = {})
{
    QJsonObject o{{u"@odata.context"_s, u"https://graph.microsoft.com/v1.0/$metadata#users('x')/todo/lists(tasks())"_s},
                  {u"value"_s, lists}};
    if (!nextLink.isEmpty())
        o.insert(u"@odata.nextLink"_s, nextLink);
    return QJsonDocument(o).toJson();
}

ImportOptions optionsIn(const QString &zone)
{
    ImportOptions o = iotest::options();
    o.timeZone = QTimeZone(zone.toUtf8());
    return o;
}

ImportResult importTasks(const QJsonArray &tasks, const QString &zone = u"Europe/Warsaw"_s)
{
    return runImport(*importerById(u"mstodo"),
                     {iotest::bytes(u"Microsoft To Do.json"_s, bundle({list(u"Zadania"_s, tasks)}))}, optionsIn(zone));
}

/// The page body without the frontmatter, without the final line break.
QString body(const ImportResult &r, const QString &path = u"Zadania.md"_s)
{
    QString text = iotest::textAt(r, path);
    if (text.startsWith(Frontmatter))
        text.remove(0, Frontmatter.size());
    if (text.endsWith(u'\n'))
        text.chop(1);
    return text;
}

bool hasWarning(const ImportResult &r, const QString &part)
{
    for (const QString &w : r.warnings) {
        if (w.contains(part))
            return true;
    }
    return false;
}

} // namespace

class TestIoImportMsTodo : public QObject {
    Q_OBJECT

public:
    /// A system zone with DST, so tests catch conversions done in the system zone instead of
    /// ImportOptions::timeZone.
    static void initMain() { brulion::test::useCentralEuropeanTime(); }

private Q_SLOTS:
    void taskLine_data()
    {
        QTest::addColumn<QString>("task");
        QTest::addColumn<QString>("zone");
        QTest::addColumn<QString>("expected");
        const QString warsaw = u"Europe/Warsaw"_s;
        const QString newYork = u"America/New_York"_s;
        QTest::newRow("not started") << u"{\"title\": \"Kupić mleko\", \"status\": \"notStarted\", \"importance\": \"normal\"}"_s
                                     << warsaw << u"- [ ] Kupić mleko"_s;
        QTest::newRow("in progress") << u"{\"title\": \"Raport\", \"status\": \"inProgress\"}"_s << warsaw
                                     << u"- [/] Raport"_s;
        QTest::newRow("waiting on others") << u"{\"title\": \"Odpowiedź Józefa\", \"status\": \"waitingOnOthers\"}"_s
                                           << warsaw << u"- [>] Odpowiedź Józefa"_s;
        QTest::newRow("deferred") << u"{\"title\": \"Malowanie\", \"status\": \"deferred\"}"_s << warsaw
                                  << u"- [>] Malowanie"_s;
        QTest::newRow("unknown status") << u"{\"title\": \"Coś\", \"status\": \"somethingNew\"}"_s << warsaw
                                        << u"- [ ] Coś"_s;
        // Completion is an instant: 22:30 UTC on 20 October is 00:30 on the 21st in Warsaw.
        QTest::newRow("completed") << u"{\"title\": \"Zapłacić czynsz\", \"status\": \"completed\", "
                                      u"\"completedDateTime\": {\"dateTime\": \"2026-10-20T22:30:00.0000000\", "
                                      u"\"timeZone\": \"UTC\"}}"_s
                                   << warsaw << u"- [x] Zapłacić czynsz ✅ 2026-10-21"_s;
        // To Do keeps the day of the completion as midnight UTC: that day, also west of Greenwich
        // (and never before the due day).
        QTest::newRow("completed midnight UTC in New York")
                << u"{\"title\": \"Bilety\", \"status\": \"completed\", "
                   u"\"dueDateTime\": {\"dateTime\": \"2026-10-27T00:00:00.0000000\", \"timeZone\": \"UTC\"}, "
                   u"\"completedDateTime\": {\"dateTime\": \"2026-10-27T00:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << newYork << u"- [x] Bilety 📅 2026-10-27 ✅ 2026-10-27"_s;
        QTest::newRow("completed midnight in Warsaw sent as UTC")
                << u"{\"title\": \"Żurek\", \"status\": \"completed\", "
                   u"\"completedDateTime\": {\"dateTime\": \"2026-10-26T23:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [x] Żurek ✅ 2026-10-27"_s;
        QTest::newRow("completed in the evening in New York")
                << u"{\"title\": \"Raport\", \"status\": \"completed\", "
                   u"\"completedDateTime\": {\"dateTime\": \"2026-10-27T01:30:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << newYork << u"- [x] Raport ✅ 2026-10-26"_s;
        QTest::newRow("high") << u"{\"title\": \"Pilne\", \"status\": \"notStarted\", \"importance\": \"high\"}"_s << warsaw
                              << u"- [ ] Pilne ⏫"_s;
        QTest::newRow("low") << u"{\"title\": \"Kiedyś\", \"status\": \"notStarted\", \"importance\": \"low\"}"_s << warsaw
                             << u"- [ ] Kiedyś 🔽"_s;
        // A due day set in Warsaw comes back as midnight there, in UTC (the day before).
        QTest::newRow("due 22:00 UTC in Warsaw")
                << u"{\"title\": \"Oddać książkę\", \"status\": \"notStarted\", "
                   u"\"dueDateTime\": {\"dateTime\": \"2026-10-19T22:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [ ] Oddać książkę 📅 2026-10-20"_s;
        QTest::newRow("due 23:00 UTC in Warsaw after DST")
                << u"{\"title\": \"Ząb\", \"dueDateTime\": {\"dateTime\": \"2026-10-26T23:00:00.0000000\", "
                   u"\"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [ ] Ząb 📅 2026-10-27"_s;
        QTest::newRow("due midnight in New York")
                << u"{\"title\": \"Oddać książkę\", \"dueDateTime\": {\"dateTime\": \"2026-10-27T04:00:00.0000000\", "
                   u"\"timeZone\": \"UTC\"}}"_s
                << newYork << u"- [ ] Oddać książkę 📅 2026-10-27"_s;
        // A day saved as midnight UTC stays that day, also west of Greenwich.
        QTest::newRow("due midnight UTC in New York")
                << u"{\"title\": \"Oddać książkę\", \"dueDateTime\": {\"dateTime\": \"2026-10-27T00:00:00.0000000\", "
                   u"\"timeZone\": \"UTC\"}}"_s
                << newYork << u"- [ ] Oddać książkę 📅 2026-10-27"_s;
        QTest::newRow("due in a Windows zone")
                << u"{\"title\": \"Spotkanie\", \"dueDateTime\": {\"dateTime\": \"2026-10-27T00:00:00.0000000\", "
                   u"\"timeZone\": \"Central European Standard Time\"}}"_s
                << warsaw << u"- [ ] Spotkanie 📅 2026-10-27"_s;
        QTest::newRow("due in an IANA zone")
                << u"{\"title\": \"Spotkanie\", \"dueDateTime\": {\"dateTime\": \"2026-10-27T00:00:00\", "
                   u"\"timeZone\": \"Europe/Warsaw\"}}"_s
                << warsaw << u"- [ ] Spotkanie 📅 2026-10-27"_s;
        QTest::newRow("start and created")
                << u"{\"title\": \"Przeprowadzka\", \"createdDateTime\": \"2026-10-14T22:15:00.1234567Z\", "
                   u"\"startDateTime\": {\"dateTime\": \"2026-10-24T22:00:00.0000000\", \"timeZone\": \"UTC\"}, "
                   u"\"dueDateTime\": {\"dateTime\": \"2026-10-30T23:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [ ] Przeprowadzka ➕ 2026-10-15 🛫 2026-10-25 📅 2026-10-31"_s;
        QTest::newRow("reminder")
                << u"{\"title\": \"Zadzwonić do mamy\", \"status\": \"notStarted\", \"isReminderOn\": true, "
                   u"\"reminderDateTime\": {\"dateTime\": \"2026-10-20T07:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [ ] Zadzwonić do mamy ⏰ 2026-10-20 09:00"_s;
        QTest::newRow("reminder in New York")
                << u"{\"title\": \"Lot\", \"isReminderOn\": true, "
                   u"\"reminderDateTime\": {\"dateTime\": \"2026-10-27T00:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << newYork << u"- [ ] Lot ⏰ 2026-10-26 20:00"_s;
        QTest::newRow("reminder off")
                << u"{\"title\": \"Bez przypomnienia\", \"isReminderOn\": false, "
                   u"\"reminderDateTime\": {\"dateTime\": \"2026-10-20T07:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [ ] Bez przypomnienia"_s;
        QTest::newRow("reminder of a completed task")
                << u"{\"title\": \"Już po\", \"status\": \"completed\", \"isReminderOn\": true, "
                   u"\"reminderDateTime\": {\"dateTime\": \"2026-10-20T07:00:00.0000000\", \"timeZone\": \"UTC\"}, "
                   u"\"completedDateTime\": {\"dateTime\": \"2026-10-19T10:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [x] Już po ✅ 2026-10-19"_s;
        QTest::newRow("categories")
                << u"{\"title\": \"Pranie\", \"categories\": [\"Praca domowa\", \"Zakupy/Spożywcze\", \"#źródła\", "
                   u"\"2026\", \"\"]}"_s
                << warsaw << u"- [ ] Pranie #Praca-domowa #Zakupy/Spożywcze #źródła"_s;
        QTest::newRow("empty title") << u"{\"title\": \"  \", \"status\": \"notStarted\"}"_s << warsaw
                                     << u"- [ ] (no title)"_s;
        QTest::newRow("title not a string") << u"{\"title\": 42, \"status\": \"notStarted\"}"_s << warsaw
                                            << u"- [ ] (no title)"_s;
        QTest::newRow("multi-line title") << u"{\"title\": \"Pierwsza\\nDruga\", \"status\": \"notStarted\"}"_s << warsaw
                                          << u"- [ ] Pierwsza Druga"_s;
        QTest::newRow("everything")
                << u"{\"title\": \"Wizyta u lekarza\", \"status\": \"notStarted\", \"importance\": \"high\", "
                   u"\"categories\": [\"Zdrowie\"], \"createdDateTime\": \"2026-10-14T08:00:00Z\", "
                   u"\"isReminderOn\": true, "
                   u"\"reminderDateTime\": {\"dateTime\": \"2026-10-21T06:30:00.0000000\", \"timeZone\": \"UTC\"}, "
                   u"\"dueDateTime\": {\"dateTime\": \"2026-10-20T22:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s
                << warsaw << u"- [ ] Wizyta u lekarza #Zdrowie ⏫ ➕ 2026-10-14 📅 2026-10-21 ⏰ 2026-10-21 08:30"_s;
    }

    void taskLine()
    {
        QFETCH(QString, task);
        QFETCH(QString, zone);
        QFETCH(QString, expected);
        const ImportResult r = importTasks({parse(task)}, zone);
        QCOMPARE(body(r), expected);
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(u" | "_s)));
    }

    void titleEscaping_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("shown");
        QTest::newRow("dollars") << u"Bilet $10 i parking 5$"_s << u"Bilet $10 i parking 5$"_s;
        QTest::newRow("tag sign") << u"Zadanie #1 na liście"_s << u"Zadanie #1 na liście"_s;
        QTest::newRow("wiki link") << u"Przeczytać [[nie link]]"_s << u"Przeczytać [[nie link]]"_s;
        QTest::newRow("list marker") << u"- nie podpunkt"_s << u"- nie podpunkt"_s;
        QTest::newRow("emphasis") << u"*ważne* i **bardzo**"_s << u"*ważne* i **bardzo**"_s;
    }

    void titleEscaping()
    {
        QFETCH(QString, title);
        QFETCH(QString, shown);
        const ImportResult r = importTasks({QJsonObject{{u"title"_s, title}, {u"status"_s, u"notStarted"_s}}});
        const QString page = iotest::textAt(r, u"Zadania.md"_s);
        // The editor shows the title as typed: nothing became a link, a tag, a formula or a list.
        QVERIFY2(!page.contains(u"[[nie link]]"_s), qPrintable(page));
        QCOMPARE(iotest::shownText(page), u"☐ "_s + shown); // the checkbox, then the title
        const QStringList lines = body(r).split(u'\n');
        QCOMPARE(lines.size(), 1);
        QVERIFY(lines.first().startsWith(u"- [ ] "_s));
    }

    void recurrence_data()
    {
        QTest::addColumn<QString>("recurrence");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("warning");
        const QString range = u"\"range\": {\"type\": \"noEnd\", \"startDate\": \"2026-10-20\"}"_s;
        QTest::newRow("daily 2") << u"{\"pattern\": {\"type\": \"daily\", \"interval\": 2}, "_s + range + u"}"_s
                                 << u" 🔁 every 2 days"_s << QString();
        QTest::newRow("weekly mon fri")
                << u"{\"pattern\": {\"type\": \"weekly\", \"interval\": 1, \"daysOfWeek\": [\"friday\", \"monday\"], "
                   u"\"firstDayOfWeek\": \"sunday\"}, "_s
                           + range + u"}"_s
                << u" 🔁 every week on Monday, Friday"_s << QString();
        QTest::newRow("weekdays")
                << u"{\"pattern\": {\"type\": \"weekly\", \"interval\": 1, \"daysOfWeek\": [\"monday\", \"tuesday\", "
                   u"\"wednesday\", \"thursday\", \"friday\"]}, "_s
                           + range + u"}"_s
                << u" 🔁 every weekday"_s << QString();
        QTest::newRow("every 2 weeks on weekdays")
                << u"{\"pattern\": {\"type\": \"weekly\", \"interval\": 2, \"daysOfWeek\": [\"monday\", \"tuesday\", "
                   u"\"wednesday\", \"thursday\", \"friday\"]}, "_s
                           + range + u"}"_s
                << u" 🔁 every 2 weeks on Monday, Tuesday, Wednesday, Thursday, Friday"_s << QString();
        QTest::newRow("monthly 15")
                << u"{\"pattern\": {\"type\": \"absoluteMonthly\", \"interval\": 1, \"dayOfMonth\": 15}, "_s + range
                           + u"}"_s
                << u" 🔁 every month on the 15th"_s << QString();
        QTest::newRow("yearly")
                << u"{\"pattern\": {\"type\": \"absoluteYearly\", \"interval\": 1, \"dayOfMonth\": 20, \"month\": 10}, "_s
                           + range + u"}"_s
                << u" 🔁 every year"_s << QString();
        QTest::newRow("relative monthly")
                << u"{\"pattern\": {\"type\": \"relativeMonthly\", \"interval\": 1, \"daysOfWeek\": [\"tuesday\"], "
                   u"\"index\": \"second\"}, "_s
                           + range + u"}"_s
                << QString() << u"only the first occurrence"_s;
        QTest::newRow("relative yearly")
                << u"{\"pattern\": {\"type\": \"relativeYearly\", \"interval\": 1, \"daysOfWeek\": [\"monday\"], "
                   u"\"index\": \"last\", \"month\": 5}, "_s
                           + range + u"}"_s
                << QString() << u"only the first occurrence"_s;
        QTest::newRow("numbered")
                << u"{\"pattern\": {\"type\": \"daily\", \"interval\": 1}, \"range\": {\"type\": \"numbered\", "
                   u"\"startDate\": \"2026-10-20\", \"numberOfOccurrences\": 5}}"_s
                << u" 🔁 every day"_s << u"simplified"_s;
        QTest::newRow("end date")
                << u"{\"pattern\": {\"type\": \"weekly\", \"interval\": 3, \"daysOfWeek\": [\"sunday\"]}, "
                   u"\"range\": {\"type\": \"endDate\", \"startDate\": \"2026-10-20\", \"endDate\": \"2026-12-31\"}}"_s
                << u" 🔁 every 3 weeks on Sunday"_s << u"simplified"_s;
        QTest::newRow("no type") << u"{\"pattern\": {}}"_s << QString() << QString();
    }

    void recurrence()
    {
        QFETCH(QString, recurrence);
        QFETCH(QString, expected);
        QFETCH(QString, warning);
        QJsonObject task{{u"title"_s, u"Podlać kwiaty"_s},
                         {u"status"_s, u"notStarted"_s},
                         {u"dueDateTime"_s, QJsonObject{{u"dateTime"_s, u"2026-10-19T22:00:00.0000000"_s},
                                                        {u"timeZone"_s, u"UTC"_s}}},
                         {u"recurrence"_s, parse(recurrence)}};
        const ImportResult r = importTasks({task});
        QCOMPARE(body(r), u"- [ ] Podlać kwiaty 📅 2026-10-20"_s + expected);
        if (warning.isEmpty())
            QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(u" | "_s)));
        else
            QVERIFY2(hasWarning(r, warning), qPrintable(r.warnings.join(u" | "_s)));
    }

    void recurrenceWithoutDueDate()
    {
        // A series needs a date to repeat from: the range's start day.
        const ImportResult r = importTasks({parse(
                u"{\"title\": \"Śmieci\", \"recurrence\": {\"pattern\": {\"type\": \"weekly\", \"interval\": 1, "
                u"\"daysOfWeek\": [\"wednesday\"]}, \"range\": {\"type\": \"noEnd\", \"startDate\": \"2026-10-21\"}}}"_s)});
        QCOMPARE(body(r), u"- [ ] Śmieci 📅 2026-10-21 🔁 every week on Wednesday"_s);
    }

    void notes()
    {
        const ImportResult r = importTasks(
                {parse(u"{\"title\": \"Telefon\", \"body\": {\"contentType\": \"text\", \"content\": "
                       u"\"Zadzwonić do pani Józefy — źródło: ąćęłńóśźż\\r\\n\\r\\n# nie nagłówek\\r\\n- nie punkt\\r\\n"
                       u"Bilet $10\\r\\nparking 5$\\r\\n\"}}"_s),
                 parse(u"{\"title\": \"Pusta notatka\", \"body\": {\"contentType\": \"text\", \"content\": \"\\r\\n\"}}"_s)});
        const QString text = body(r);
        QVERIFY2(text.startsWith(u"- [ ] Telefon\n  Zadzwonić do pani Józefy — źródło: ąćęłńóśźż\n\n"
                                 u"  \\# nie nagłówek\n  \\- nie punkt\n"_s),
                 qPrintable(text));
        QVERIFY2(text.endsWith(u"\n- [ ] Pusta notatka"_s), qPrintable(text));
        // The dollars of two lines do not pair into a formula.
        QVERIFY2(text.contains(u"\\$10"_s), qPrintable(text));
        QVERIFY(iotest::shownText(iotest::textAt(r, u"Zadania.md"_s)).contains(u"Bilet $10"_s));
    }

    void htmlNotes()
    {
        const ImportResult r = importTasks({parse(
                u"{\"title\": \"Z Outlooka\", \"body\": {\"contentType\": \"html\", \"content\": "
                u"\"<html><body><p>Proszę <b>pilnie</b> przejrzeć</p><ul><li>umowę</li><li>fakturę</li></ul>"
                u"<script>alert(1)</script></body></html>\"}}"_s)});
        const QString text = body(r);
        QVERIFY2(text.startsWith(u"- [ ] Z Outlooka\n  Proszę **pilnie** przejrzeć\n"_s), qPrintable(text));
        QVERIFY2(text.contains(u"  - umowę\n  - fakturę"_s), qPrintable(text));
        QVERIFY(!text.contains(u"alert"_s));
        QVERIFY(!text.contains(u"<"_s));
    }

    void steps()
    {
        const ImportResult r = importTasks({parse(
                u"{\"title\": \"Wyjazd\", \"checklistItems\": ["
                u"{\"displayName\": \"Spakować śpiwór\", \"isChecked\": true, \"checkedDateTime\": \"2026-10-18T10:00:00Z\"},"
                u"{\"displayName\": \"Kupić bilety [[PKP]]\", \"isChecked\": false},"
                u"{\"displayName\": \"\", \"isChecked\": false}, 5]}"_s)});
        QCOMPARE(body(r), u"- [ ] Wyjazd\n  - [x] Spakować śpiwór\n  - [ ] Kupić bilety \\[\\[PKP\\]\\]"_s);
    }

    void stepsOfACompletedTask()
    {
        // To Do completes a task with steps still unchecked: they are closed (cancelled), not
        // left as open tasks under a done one.
        const ImportResult r = importTasks({parse(
                u"{\"title\": \"Wyjazd\", \"status\": \"completed\", \"checklistItems\": ["
                u"{\"displayName\": \"Spakować śpiwór\", \"isChecked\": true},"
                u"{\"displayName\": \"Kupić bilety\", \"isChecked\": false}]}"_s)});
        QCOMPARE(body(r), u"- [x] Wyjazd\n  - [x] Spakować śpiwór\n  - [-] Kupić bilety"_s);
    }

    void links()
    {
        const ImportResult r = importTasks({parse(
                u"{\"title\": \"Przeczytać\", \"linkedResources\": ["
                u"{\"webUrl\": \"https://example.com/artykuł o pogodzie\", \"displayName\": \"Artykuł [pogoda]\", "
                u"\"applicationName\": \"Edge\"},"
                u"{\"webUrl\": \"https://outlook.live.com/mail/0/inbox/id/AAQk%3D\", \"applicationName\": \"Outlook\"},"
                u"{\"webUrl\": \"javascript:alert(1)\", \"displayName\": \"Zły link\"},"
                u"{\"displayName\": \"Bez adresu\"}, {}]}"_s)});
        QCOMPARE(body(r), u"- [ ] Przeczytać\n"
                          u"  - [Artykuł \\[pogoda\\]](https://example.com/artykuł%20o%20pogodzie)\n"
                          u"  - <https://outlook.live.com/mail/0/inbox/id/AAQk%3D>\n"
                          u"  - Zły link\n"
                          u"  - Bez adresu"_s);
    }

    void attachments()
    {
        const QByteArray pdf("%PDF-1.4 źródło");
        const QByteArray png("\x89PNG\r\n\x1a\n", 8);
        const ImportResult r = importTasks({parse(
                u"{\"title\": \"Faktura\", \"hasAttachments\": true, \"attachments\": ["
                u"{\"@odata.type\": \"#microsoft.graph.taskFileAttachment\", \"name\": \"Faktura październik.pdf\", "
                u"\"contentType\": \"application/pdf\", \"size\": 20, \"contentBytes\": \""_s
                + QString::fromLatin1(pdf.toBase64())
                + u"\"},"
                  u"{\"name\": \"\", \"contentType\": \"image/png\", \"contentBytes\": \""_s
                + QString::fromLatin1(png.toBase64())
                + u"\"},"
                  u"{\"name\": \"zepsuty.txt\", \"contentBytes\": \"to nie jest base64!\"}]}"_s)});
        QCOMPARE(body(r), u"- [ ] Faktura\n  - ![[assets/Microsoft To Do/Faktura październik.pdf]]\n"
                          u"  - ![[assets/Microsoft To Do/attachment.png]]"_s);
        QCOMPARE(iotest::fileAt(r, u"assets/Microsoft To Do/Faktura październik.pdf"_s)->data, pdf);
        QCOMPARE(iotest::fileAt(r, u"assets/Microsoft To Do/attachment.png"_s)->data, png);
        QVERIFY2(hasWarning(r, u"could not be read"_s), qPrintable(r.warnings.join(u" | "_s)));
    }

    void attachmentTooLarge()
    {
        const ImportResult r = importTasks({parse(
                u"{\"title\": \"Film\", \"hasAttachments\": true, \"attachments\": [{\"name\": \"film.mp4\", "
                u"\"size\": 40000000, \"contentBytes\": \"AAAA\"}]}"_s)});
        QCOMPARE(body(r), u"- [ ] Film"_s);
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Zadania.md"_s}));
        QVERIFY2(hasWarning(r, u"25 MB"_s), qPrintable(r.warnings.join(u" | "_s)));
    }

    void attachmentsNotInFile()
    {
        // Graph Explorer's $expand=tasks has no attachments, only hasAttachments.
        const QJsonArray tasks{parse(u"{\"title\": \"Skan\", \"hasAttachments\": true}"_s),
                               parse(u"{\"title\": \"Drugi skan\", \"hasAttachments\": true}"_s)};
        const ImportResult r = runImport(*importerById(u"mstodo"),
                                         {iotest::bytes(u"response.json"_s, explorer({list(u"Zadania"_s, tasks)}))},
                                         iotest::options());
        QCOMPARE(body(r), u"- [ ] Skan\n- [ ] Drugi skan"_s);
        QCOMPARE(r.warnings.size(), 1);
        QVERIFY(r.warnings.first().contains(u"attachments that are not in this file"_s));

        // In the download's bundle the download itself said what it could not get: no advice to
        // sign in, which the user just did.
        const ImportResult own = importTasks(tasks);
        QCOMPARE(body(own), u"- [ ] Skan\n- [ ] Drugi skan"_s);
        QVERIFY2(own.warnings.isEmpty(), qPrintable(own.warnings.join(u" | "_s)));
    }

    void shapes_data()
    {
        QTest::addColumn<QList<InputFile>>("files");
        QTest::addColumn<QStringList>("pages");
        const QJsonArray zakupy{parse(u"{\"title\": \"Chleb\", \"status\": \"notStarted\", \"importance\": \"normal\"}"_s)};
        const QJsonArray praca{parse(u"{\"title\": \"Raport\", \"status\": \"inProgress\", \"importance\": \"high\"}"_s)};
        const QJsonArray lists{list(u"Zakupy"_s, zakupy), list(u"Praca"_s, praca)};
        QTest::newRow("bundle") << QList<InputFile>{iotest::bytes(u"Microsoft To Do.json"_s, bundle(lists))}
                                << QStringList({u"Praca.md"_s, u"Zakupy.md"_s});
        QTest::newRow("graph explorer") << QList<InputFile>{iotest::bytes(u"response.json"_s, explorer(lists))}
                                        << QStringList({u"Praca.md"_s, u"Zakupy.md"_s});
        const QByteArray oneList = QJsonDocument(QJsonObject{{u"@odata.context"_s,
                                                              u"https://graph.microsoft.com/v1.0/$metadata#users('x')/"
                                                              u"todo/lists('AAMk')/tasks"_s},
                                                             {u"value"_s, zakupy}})
                                           .toJson();
        QTest::newRow("one list's tasks") << QList<InputFile>{iotest::bytes(u"Domowe sprawy.json"_s, oneList)}
                                          << QStringList({u"Domowe sprawy.md"_s});
        const QByteArray bare = QJsonDocument(QJsonObject{{u"value"_s, praca}}).toJson();
        QTest::newRow("tasks without context") << QList<InputFile>{iotest::bytes(u"Praca.json"_s, bare)}
                                               << QStringList({u"Praca.md"_s});
        QTest::newRow("several files") << QList<InputFile>{iotest::bytes(u"Zakupy.json"_s, oneList),
                                                           iotest::bytes(u"inne.json"_s, explorer({list(u"Praca"_s, praca)}))}
                                       << QStringList({u"Praca.md"_s, u"Zakupy.md"_s});
        QTest::newRow("zip") << QList<InputFile>{iotest::zip(u"To Do.zip"_s, {{u"eksport/Zakupy.json"_s, oneList},
                                                                             {u"eksport/listy.json"_s, explorer({list(u"Praca"_s, praca)})},
                                                                             {u"eksport/logo.png"_s, "\x89PNG"}})}
                             << QStringList({u"Praca.md"_s, u"Zakupy.md"_s});
    }

    void shapes()
    {
        QFETCH(QList<InputFile>, files);
        QFETCH(QStringList, pages);
        QCOMPARE(detectImporter(files) ? detectImporter(files)->id() : QString(), u"mstodo"_s);
        const ImportResult r = runImport(*importerById(u"mstodo"), files, iotest::options());
        QCOMPARE(iotest::pathsOf(r), pages);
        QVERIFY2(r.warnings.isEmpty(), qPrintable(r.warnings.join(u" | "_s)));
        for (const QString &page : pages)
            QVERIFY2(iotest::textAt(r, page).startsWith(Frontmatter + u"- ["_s), qPrintable(iotest::textAt(r, page)));
    }

    void nextLink()
    {
        const QJsonArray tasks{parse(u"{\"title\": \"Pierwsze\", \"status\": \"notStarted\"}"_s)};
        QJsonObject paged = list(u"Duża lista"_s, tasks);
        paged.insert(u"tasks@odata.nextLink"_s, u"https://graph.microsoft.com/v1.0/me/todo/lists/x/tasks?$skip=10"_s);
        for (const QByteArray &data : {explorer({paged}),
                                       explorer({list(u"Duża lista"_s, tasks)},
                                                u"https://graph.microsoft.com/v1.0/me/todo/lists?$skiptoken=x"_s)}) {
            const ImportResult r = runImport(*importerById(u"mstodo"), {iotest::bytes(u"lists.json"_s, data)},
                                             iotest::options());
            QCOMPARE(iotest::pathsOf(r), QStringList({u"Duża lista.md"_s}));
            QCOMPARE(r.warnings.size(), 1);
            QVERIFY(r.warnings.first().contains(u"export is incomplete"_s));
            QVERIFY(r.warnings.first().contains(u"Sign in to Microsoft"_s));
        }
    }

    void nextLinkByShape_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("warning");
        QTest::addColumn<QString>("notInWarning");
        const QJsonArray tasks{parse(u"{\"title\": \"Pierwsze\", \"status\": \"notStarted\"}"_s)};
        QJsonObject cut = list(u"Duża lista"_s, tasks);
        cut.insert(u"tasks@odata.nextLink"_s, u"(not downloaded)"_s);
        // The download stopped at its task limit: the lists after it have no task at all.
        QJsonObject after = list(u"Następna"_s, {});
        after.insert(u"tasks@odata.nextLink"_s, u"(not downloaded)"_s);
        QTest::newRow("download") << bundle({cut, after}) << u"download is incomplete"_s << u"Sign in"_s;
        const QByteArray oneList =
                QJsonDocument(QJsonObject{{u"@odata.context"_s, u"https://graph.microsoft.com/v1.0/$metadata#users('x')/"
                                                                u"todo/lists('AAMk')/tasks"_s},
                                          {u"@odata.nextLink"_s,
                                           u"https://graph.microsoft.com/v1.0/me/todo/lists/AAMk/tasks?$skip=10"_s},
                                          {u"value"_s, tasks}})
                        .toJson();
        QTest::newRow("one list's tasks") << oneList << u"only the first tasks of the list"_s
                                          << u"export each list separately"_s;
    }

    void nextLinkByShape()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, warning);
        QFETCH(QString, notInWarning);
        const ImportResult r = runImport(*importerById(u"mstodo"), {iotest::bytes(u"Duża lista.json"_s, data)},
                                         iotest::options());
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Duża lista.md"_s}));
        QCOMPARE(r.warnings.size(), 1); // no "empty list" for a list that was cut off
        QVERIFY2(r.warnings.first().contains(warning), qPrintable(r.warnings.first()));
        QVERIFY2(!r.warnings.first().contains(notInWarning), qPrintable(r.warnings.first()));
    }

    void lists()
    {
        const QJsonArray one{parse(u"{\"title\": \"A\"}"_s)};
        const QJsonArray order{
                parse(u"{\"title\": \"Zrobione dawno\", \"status\": \"completed\", \"completedDateTime\": "
                      u"{\"dateTime\": \"2026-10-01T10:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s),
                parse(u"{\"title\": \"Pierwsze otwarte\", \"status\": \"notStarted\"}"_s),
                parse(u"{\"title\": \"Zrobione wczoraj\", \"status\": \"completed\", \"completedDateTime\": "
                      u"{\"dateTime\": \"2026-10-26T10:00:00.0000000\", \"timeZone\": \"UTC\"}}"_s),
                parse(u"{\"title\": \"Zrobione bez daty\", \"status\": \"completed\"}"_s),
                parse(u"{\"title\": \"Drugie otwarte\", \"status\": \"inProgress\"}"_s)};
        ImportOptions o = iotest::options(u"Import/Microsoft To Do"_s);
        o.exists = [](const VaultPath &p) { return p == u"Import/Microsoft To Do/Zakupy.md"; };
        const ImportResult r = runImport(
                *importerById(u"mstodo"),
                {iotest::bytes(u"Microsoft To Do.json"_s,
                               bundle({list(u"Zadania"_s, order, u"defaultList"_s), list(u"Zadania"_s, one),
                                       list(u"Dom/Ogród: plan"_s, one), list(u"Pusta"_s, {}), list(u"Druga pusta"_s, {}),
                                       list(u"Zakupy"_s, one), list(u"Oflagowana poczta e-mail"_s, one, u"flaggedEmails"_s),
                                       list(u""_s, one)}))},
                o);
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Import/Microsoft To Do/Dom Ogród plan.md"_s, u"Import/Microsoft To Do/Microsoft To Do.md"_s,
                              u"Import/Microsoft To Do/Oflagowana poczta e-mail.md"_s,
                              u"Import/Microsoft To Do/Zadania 2.md"_s, u"Import/Microsoft To Do/Zadania.md"_s,
                              u"Import/Microsoft To Do/Zakupy 2.md"_s}));
        QCOMPARE(body(r, u"Import/Microsoft To Do/Zadania.md"_s),
                 u"- [ ] Pierwsze otwarte\n- [/] Drugie otwarte\n- [x] Zrobione wczoraj ✅ 2026-10-26\n"
                 u"- [x] Zrobione dawno ✅ 2026-10-01\n- [x] Zrobione bez daty"_s);
        // A name a file cannot hold keeps its title as an alias.
        QVERIFY2(iotest::textAt(r, u"Import/Microsoft To Do/Dom Ogród plan.md"_s).contains(u"Dom/Ogród: plan"_s),
                 qPrintable(iotest::textAt(r, u"Import/Microsoft To Do/Dom Ogród plan.md"_s)));
        QVERIFY2(hasWarning(r, u"2 empty list"_s), qPrintable(r.warnings.join(u" | "_s)));
    }

    void malformed_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QStringList>("pages");
        QTest::addColumn<QString>("warning");
        QTest::newRow("not json") << QByteArray("{\"value\": [") << QStringList() << u"not valid JSON"_s;
        QTest::newRow("json array") << QByteArray("[{\"title\": \"x\", \"status\": \"notStarted\"}]") << QStringList()
                                    << u"not a Microsoft To Do export"_s;
        QTest::newRow("value not an array") << QByteArray("{\"@odata.context\": \"…/todo/lists\", \"value\": 5}")
                                            << QStringList() << QString();
        QTest::newRow("tasks of wrong types")
                << QByteArray("{\"brulion\": \"mstodo-export\", \"lists\": [{\"displayName\": \"Lista\", \"tasks\": "
                              "[5, \"x\", null, {\"title\": [1], \"status\": 7, \"importance\": {}, \"categories\": \"a\", "
                              "\"checklistItems\": {}, \"linkedResources\": 3, \"body\": \"tekst\", \"recurrence\": 1, "
                              "\"dueDateTime\": 5}]}, 7, \"x\"]}")
                << QStringList({u"Lista.md"_s}) << u"could not be read"_s;
        QTest::newRow("garbage date")
                << bundle({list(u"Daty"_s, {parse(u"{\"title\": \"A\", \"dueDateTime\": {\"dateTime\": \"jutro\", "
                                                  u"\"timeZone\": \"UTC\"}}"_s)})})
                << QStringList({u"Daty.md"_s}) << u"could not be read"_s;
        QTest::newRow("year one is no date")
                << bundle({list(u"Daty"_s, {parse(u"{\"title\": \"A\", \"dueDateTime\": {\"dateTime\": "
                                                  u"\"0001-01-01T00:00:00\", \"timeZone\": \"UTC\"}}"_s)})})
                << QStringList({u"Daty.md"_s}) << QString();
        QTest::newRow("unknown zone")
                << bundle({list(u"Daty"_s, {parse(u"{\"title\": \"A\", \"dueDateTime\": {\"dateTime\": "
                                                  u"\"2026-10-27T00:00:00\", \"timeZone\": \"Mars Standard Time\"}}"_s)})})
                << QStringList({u"Daty.md"_s}) << u"time zones"_s;
    }

    void malformed()
    {
        QFETCH(QByteArray, data);
        QFETCH(QStringList, pages);
        QFETCH(QString, warning);
        const ImportResult r =
                runImport(*importerById(u"mstodo"), {iotest::bytes(u"dane.json"_s, data)}, iotest::options());
        QCOMPARE(iotest::pathsOf(r), pages);
        if (!warning.isEmpty())
            QVERIFY2(hasWarning(r, warning), qPrintable(r.warnings.join(u" | "_s)));
    }

    void unknownZoneKeepsTheDate()
    {
        const ImportResult r = importTasks({parse(u"{\"title\": \"Marsjańskie\", \"dueDateTime\": {\"dateTime\": "
                                                  u"\"2026-10-27T00:00:00\", \"timeZone\": \"Mars Standard Time\"}}"_s)});
        QCOMPARE(body(r), u"- [ ] Marsjańskie 📅 2026-10-27"_s);
    }
};

QTEST_MAIN(TestIoImportMsTodo)
#include "tst_io_import_mstodo.moc"
