// JSON of the model (notebook/json.h): the names of statuses, priorities and kinds (also from
// Polish words), and the shapes brulion-cli prints — the keys of the app's maps (the app's own
// parity check: tests/app/shell tst_shell_workspace queryJsonMatchesTheApp).
#include "notebook/json.h"

#include "index/vaultindex.h"
#include "markdown/parse.h"

#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

namespace {

QStringList keys(const QJsonObject &o)
{
    QStringList out = o.keys();
    out.sort();
    return out;
}

} // namespace

class TestNotebookJson : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void statusWords_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<int>("status"); // -1 = unknown
        QTest::newRow("todo") << u"todo"_s << int(TaskStatus::Todo);
        QTest::newRow("open") << u"open"_s << int(TaskStatus::Todo);
        QTest::newRow("do zrobienia") << u"Do zrobienia"_s << int(TaskStatus::Todo);
        QTest::newRow("doing") << u"DOING"_s << int(TaskStatus::Doing);
        QTest::newRow("w toku") << u"w toku"_s << int(TaskStatus::Doing);
        QTest::newRow("in-progress") << u"in_progress"_s << int(TaskStatus::Doing);
        QTest::newRow("done") << u"done"_s << int(TaskStatus::Done);
        QTest::newRow("zrobione") << u"zrobione"_s << int(TaskStatus::Done);
        QTest::newRow("wykonane") << u"Wykonane"_s << int(TaskStatus::Done);
        QTest::newRow("cancel") << u"cancel"_s << int(TaskStatus::Cancelled);
        QTest::newRow("canceled") << u"canceled"_s << int(TaskStatus::Cancelled);
        QTest::newRow("anulowane") << u"anulowane"_s << int(TaskStatus::Cancelled);
        QTest::newRow("defer") << u"defer"_s << int(TaskStatus::Deferred);
        QTest::newRow("odłożone") << u"odłożone"_s << int(TaskStatus::Deferred);
        QTest::newRow("odlozone") << u"odlozone"_s << int(TaskStatus::Deferred);
        QTest::newRow("unknown") << u"gotowe?"_s << -1;
        QTest::newRow("empty") << QString() << -1;
    }
    void statusWords()
    {
        QFETCH(QString, word);
        QFETCH(int, status);
        const auto s = taskStatusFromName(word);
        QCOMPARE(s ? int(*s) : -1, status);
        if (s)
            QCOMPARE(taskStatusFromName(taskStatusName(*s)), s);
    }

    void priorityWords()
    {
        QCOMPARE(priorityFromName(u"najwyższy"), std::optional<Priority>(Priority::Highest));
        QCOMPARE(priorityFromName(u"WYSOKI"), std::optional<Priority>(Priority::High));
        QCOMPARE(priorityFromName(u"średni"), std::optional<Priority>(Priority::Medium));
        QCOMPARE(priorityFromName(u"low"), std::optional<Priority>(Priority::Low));
        QCOMPARE(priorityFromName(u"brak"), std::optional<Priority>(Priority::None));
        QVERIFY(!priorityFromName(u"pilne"));
        QCOMPARE(priorityName(Priority::None), QString());
        QCOMPARE(priorityName(Priority::Lowest), u"lowest"_s);
    }

    void kindNames()
    {
        // The names of the JSON export "brulion-export v1" (io uses these functions).
        QCOMPARE(blockKindName(BlockKind::ListItem), u"listItem"_s);
        QCOMPARE(blockKindName(BlockKind::Frontmatter), u"frontmatter"_s);
        QCOMPARE(linkKindName(LinkKind::BlockRef), u"blockRef"_s);
        QCOMPARE(linkKindName(LinkKind::Url), u"url"_s);
        QCOMPARE(queryTypeName(QueryType::Reminders), u"reminders"_s);
        QCOMPARE(calendarKindName(CalendarItem::Scheduled), u"scheduled"_s);
        QCOMPARE(taskStatusName(TaskStatus::Cancelled), u"cancelled"_s);
    }

    void taskAndBlockShapes()
    {
        const ParsedNote note = markdown::parseNote(
                u"Dom.md"_s,
                u"- [ ] Zapłacić rachunek ⏫ 📅 2026-10-20 ⏰ 2026-10-19 18:00 🔁 co miesiąc #dom ^r1\n"_s);
        QCOMPARE(note.blocks.size(), 1);
        const QJsonObject task = taskJson(*note.blocks.first().task);
        QCOMPARE(keys(task),
                 (QStringList{u"due"_s, u"marker"_s, u"priority"_s, u"recurrence"_s, u"status"_s, u"text"_s}));
        QCOMPARE(task.value(u"due"_s).toString(), u"2026-10-20"_s);
        QCOMPARE(task.value(u"priority"_s).toString(), u"high"_s);
        QCOMPARE(task.value(u"recurrence"_s).toString(), u"every month"_s);
        const QJsonObject block = blockJson(note.blocks.first());
        QCOMPARE(block.value(u"kind"_s).toString(), u"listItem"_s);
        QCOMPARE(block.value(u"blockId"_s).toString(), u"r1"_s);
        QCOMPARE(block.value(u"reminders"_s).toArray().first().toObject().value(u"at"_s).toString(),
                 u"2026-10-19 18:00"_s);
        QCOMPARE(block.value(u"tags"_s).toArray().first().toString(), u"dom"_s);
    }

    void queryShapes()
    {
        VaultIndex index;
        index.update(u"Dom.md"_s, u"- [ ] Pomalować płot 📅 2026-10-20\n- notatka ⏰ 2026-10-15 09:00\n"_s);
        index.update(u"journals/2026-10-14.md"_s, u"- żółw w ogrodzie\n"_s);
        const auto parsed = parseQuery(u"type:tasks"_s);
        QVERIFY(std::holds_alternative<Query>(parsed));
        const QJsonObject result =
                queryResultJson(index, runQuery(std::get<Query>(parsed), index, QDate(2026, 10, 14)));
        QCOMPARE(keys(result), (QStringList{u"groups"_s, u"items"_s, u"total"_s, u"type"_s}));
        QCOMPARE(result.value(u"total"_s).toInt(), 1);
        const QJsonObject item = result.value(u"items"_s).toArray().first().toObject();
        QCOMPARE(keys(item), (QStringList{u"depth"_s, u"folder"_s, u"line"_s, u"path"_s, u"raw"_s, u"task"_s, u"text"_s,
                                          u"title"_s}));
        QCOMPARE(item.value(u"text"_s).toString(), u"Pomalować płot"_s);
        // Reminders carry {at, alarm}; search hits name the journal day.
        const auto reminders = parseQuery(u"type:reminders"_s);
        const QJsonObject r = queryResultJson(index, runQuery(std::get<Query>(reminders), index, QDate(2026, 10, 14)));
        QCOMPARE(r.value(u"items"_s)
                         .toArray()
                         .first()
                         .toObject()
                         .value(u"reminder"_s)
                         .toObject()
                         .value(u"at"_s)
                         .toString(),
                 u"2026-10-15 09:00"_s);
        const QList<SearchHit> hits = index.search(u"zolw"_s);
        QCOMPARE(hits.size(), 1);
        const QJsonObject hit = searchHitJson(hits.first(), VaultConfig());
        QCOMPARE(hit.value(u"journal"_s).toString(), u"2026-10-14"_s);
        QCOMPARE(keys(hit),
                 (QStringList{u"excerpt"_s, u"folder"_s, u"journal"_s, u"line"_s, u"path"_s, u"score"_s, u"title"_s}));
    }

    void configAndNotifications()
    {
        const QJsonObject config = vaultConfigJson(VaultConfig());
        QCOMPARE(keys(config),
                 (QStringList{u"attachmentsFolder"_s, u"defaultReminderTime"_s, u"journalFileFormat"_s,
                              u"journalMode"_s, u"journalTemplate"_s, u"journalsFolder"_s, u"newPagesFolder"_s,
                              u"pageMode"_s, u"templatesFolder"_s, u"weekStartsOn"_s}));
        QCOMPARE(config.value(u"defaultReminderTime"_s).toString(), u"09:00"_s);
        ScheduledNotification n;
        n.id = u"x"_s;
        n.at = QDateTime(QDate(2026, 10, 15), QTime(9, 0));
        n.title = u"Zadzwonić do Łukasza"_s;
        n.path = u"Dom.md"_s;
        n.line = 3;
        const QJsonObject o = notificationJson(n);
        QCOMPARE(o.value(u"at"_s).toString(), u"2026-10-15 09:00"_s); // local wall clock, never UTC
        QCOMPARE(o.value(u"line"_s).toInt(), 3);
        CalendarItem item;
        item.date = QDate(2026, 10, 15);
        item.kind = CalendarItem::Due;
        QCOMPARE(calendarItemJson(item).value(u"time"_s).toString(), QString());
        QCOMPARE(calendarItemJson(item).value(u"kind"_s).toString(), u"due"_s);
    }
};

QTEST_GUILESS_MAIN(TestNotebookJson)
#include "tst_notebook_json.moc"
