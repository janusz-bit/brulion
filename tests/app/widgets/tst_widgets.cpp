// The C++ side of the Android widgets (src/app/widgets): the snapshot they show — TasksView's
// sections and order, plain text only, the calendar's agenda, the limits, the capture days of quick
// add — against the golden file the Kotlin tests read too (tests/data/widgets/snapshot-v1.json;
// BRULION_UPDATE_GOLDEN=1 rewrites it), the sections of a later day (refile-cases.json, mirrored
// by WidgetSnapshot.kt) and what quick add writes (journal-cases.json, ported by
// JournalAppend.kt), plus the entries queued for the app.
#include "widgetactions.h"
#include "widgetsnapshot.h"

#include "tasklistmodel.h"

#include "index/querycache.h"
#include "index/vaultindex.h"
#include "notebook/journals.h"
#include "reminders/calendar.h"
#include "planningutil.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::widgets;

namespace {

const QString kData = QStringLiteral(BRULION_TEST_DATA "/widgets/");

QJsonObject readJson(const QString &name)
{
    QFile f(kData + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

std::unique_ptr<VaultIndex> makeIndex(const QList<std::pair<QString, QString>> &files)
{
    auto index = std::make_unique<VaultIndex>(VaultConfig());
    {
        VaultIndex::Batch batch(*index);
        for (const auto &[path, content] : files)
            index->update(path, content);
    }
    return index;
}

/// The fixture notebook: tasks of every section, a journal per day, reminders and an alarm.
const QList<std::pair<QString, QString>> kFixture = {
        {u"journals/2026-10-26.md"_s,
         u"- [ ] Zadzwonić do hydraulika 📅 2026-10-26 #dom\n"
         "- [x] Zrobione wczoraj ✅ 2026-10-26\n"_s},
        {u"journals/2026-10-27.md"_s,
         u"# wtorek, 27 października\n"
         "\n"
         "- [ ] [[Spotkanie z Łucją]] **pilne** #praca 📅 2026-10-27\n"
         "- [ ] Kupić żółte jabłka ⏰ 2026-10-27 09:00\n"
         "- [ ] Odebrać paczkę ⏳ 2026-10-20\n"
         "- [ ] <script>alert(1)</script> & <b>x</b> 📅 2026-10-27\n"
         "- przypomnienie bez zadania ⏰ 2026-10-27 18:00\n"_s},
        {u"Projekty/Dom.md"_s,
         u"# Dom\n"
         "\n"
         "- [ ] Zapłacić rachunki 📅 2026-10-28 ⏫\n"
         "- [ ] Pobudka na pociąg ⏰! 2026-10-28 06:30\n"
         "- [ ] Wymienić filtr 📅 2026-11-05\n"
         "- [ ] Malowanie 📅 2026-12-15\n"
         "- [ ] Bez terminu\n"
         "- [ ] Podlać kwiaty 📅 2026-10-27 🔁 every day\n"
         "- [/] W trakcie 📅 2026-10-29 18:30\n"
         "- [-] Anulowane 📅 2026-10-27\n"
         "- [ ] Zły termin 📅 2026-13-45\n"_s},
};

const QDateTime kNow(QDate(2026, 10, 27), QTime(14, 5));

SnapshotOptions fixtureOptions()
{
    SnapshotOptions o;
    o.now = kNow;
    o.weekStart = Qt::Monday;
    o.locale = u"pl"_s;
    o.vaultId = u"brulion-test"_s;
    o.vaultName = u"Brulion"_s;
    o.vaultRoot = u"/storage/emulated/0/Brulion"_s;
    o.favorites = {{u"Projekty/Dom.md"_s, u"Dom"_s}, {u""_s, u"pusta"_s}, {u"Zakupy ąę.md"_s, u"Zakupy ąę"_s}};
    o.recent = {{u"journals/2026-10-27.md"_s, u"2026-10-27"_s}};
    o.captureDays = {{QDate(2026, 10, 27), u"journals/2026-10-27.md"_s, true, u"# wtorek, 27 października\n\n- "_s},
                     {QDate(2026, 10, 28), u"journals/2026-10-28.md"_s, false, u"# środa, 28 października\n\n- "_s}};
    return o;
}

QJsonObject taskByText(const QJsonObject &snapshot, const QString &text)
{
    for (const QJsonValue &v : snapshot.value(u"tasks"_s).toArray()) {
        if (v.toObject().value(u"text"_s).toString() == text)
            return v.toObject();
    }
    return {};
}

std::optional<DateSpec> spec(const QString &text)
{
    if (text.isEmpty())
        return std::nullopt;
    const QDate date = planning::parseIsoDate(text.left(10));
    if (!date.isValid())
        return std::nullopt;
    DateSpec s{date, std::nullopt};
    if (text.size() > 10)
        s.time = QTime::fromString(text.mid(11), u"HH:mm"_s);
    return s;
}

/// The reminders of a refile case or a snapshot task ("reminders": [spec | {at, alarm}]).
QList<Reminder> remindersOf(const QJsonArray &array)
{
    QList<Reminder> out;
    for (const QJsonValue &v : array) {
        const QString at = v.isObject() ? v.toObject().value(u"at"_s).toString() : v.toString();
        if (const auto s = spec(at)) {
            Reminder r;
            r.at = *s;
            r.alarm = v.toObject().value(u"alarm"_s).toBool();
            out.append(r);
        }
    }
    return out;
}

/// The date the task's next reminder on `today` files it by: of "reminders" when given, else "reminder".
std::optional<QDate> reminderDate(const QJsonObject &c, QDate today, QString *next = nullptr)
{
    if (c.contains(u"reminders"_s)) {
        const auto r = planning::TaskListModel::nextReminder(remindersOf(c.value(u"reminders"_s).toArray()), today);
        if (next)
            *next = r ? r->at.toString() : QString();
        return r ? std::optional<QDate>(r->at.date) : std::nullopt;
    }
    if (const auto r = spec(c.value(u"reminder"_s).toString()))
        return r->date;
    return std::nullopt;
}

void writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    QVERIFY(f.commit());
}

} // namespace

class TestWidgets : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // The fixture's snapshot equals the golden file the Kotlin tests parse.
    void snapshotGolden()
    {
        const auto index = makeIndex(kFixture);
        const QJsonObject snapshot = buildSnapshot(*index, fixtureOptions());
        if (qEnvironmentVariableIsSet("BRULION_UPDATE_GOLDEN")) {
            QSaveFile out(kData + u"snapshot-v1.json"_s);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(QJsonDocument(snapshot).toJson(QJsonDocument::Indented));
            QVERIFY(out.commit());
            QSKIP("snapshot-v1.json rewritten");
        }
        const QJsonObject golden = readJson(u"snapshot-v1.json"_s);
        QVERIFY2(!golden.isEmpty(), "tests/data/widgets/snapshot-v1.json is missing");
        if (snapshot != golden)
            qWarning().noquote() << QJsonDocument(snapshot).toJson(QJsonDocument::Indented);
        QCOMPARE(snapshot, golden);
    }

    // A publisher keeps its caches across snapshots: after an edit the next snapshot computes the
    // changed note only and equals one built afresh.
    void snapshotsKeepCaches()
    {
        auto index = makeIndex(kFixture);
        SnapshotOptions options = fixtureOptions();
        options.caches = SnapshotCaches::create();
        QCOMPARE(buildSnapshot(*index, options), buildSnapshot(*index, fixtureOptions()));
        QVERIFY(options.caches->tasks->lastSync().full);
        QVERIFY(options.caches->calendar->lastSync().full);
        index->update(u"journals/2026-10-27.md"_s,
                      index->content(u"journals/2026-10-27.md"_s) + u"\n- [ ] Nowe z widżetu 📅 2026-10-28 ⏰ 2026-10-28 08:00\n"_s);
        const QJsonObject cached = buildSnapshot(*index, options);
        QCOMPARE(cached, buildSnapshot(*index, fixtureOptions()));
        QCOMPARE(taskByText(cached, u"Nowe z widżetu"_s).value(u"section"_s).toString(), u"tomorrow"_s);
        QCOMPARE(options.caches->tasks->lastSync().changed, QList<QString>{u"journals/2026-10-27.md"_s});
        QCOMPARE(options.caches->calendar->lastSync().changed, QList<QString>{u"journals/2026-10-27.md"_s});
        QVERIFY(!options.caches->overdue->lastSync().full);
    }

    void snapshotTasks_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("section"); // "" = left out
        QTest::newRow("yesterday's due") << u"Zadzwonić do hydraulika #dom"_s << u"overdue"_s;
        QTest::newRow("due today, plain text") << u"Spotkanie z Łucją pilne #praca"_s << u"today"_s;
        QTest::newRow("reminder today, no date") << u"Kupić żółte jabłka"_s << u"today"_s;
        QTest::newRow("scheduled in the past rolls over") << u"Odebrać paczkę"_s << u"today"_s;
        QTest::newRow("tags as text, HTML literal") << u"<script>alert(1)</script> & <b>x</b>"_s << u"today"_s;
        QTest::newRow("tomorrow") << u"Zapłacić rachunki"_s << u"tomorrow"_s;
        QTest::newRow("alarm tomorrow") << u"Pobudka na pociąg"_s << u"tomorrow"_s;
        QTest::newRow("this week, in progress") << u"W trakcie"_s << u"thisWeek"_s;
        QTest::newRow("within 14 days") << u"Wymienić filtr"_s << u"later"_s;
        QTest::newRow("recurring") << u"Podlać kwiaty"_s << u"today"_s;
        QTest::newRow("beyond 14 days") << u"Malowanie"_s << QString();
        QTest::newRow("no date") << u"Bez terminu"_s << QString();
        QTest::newRow("malformed date: no date") << u"Zły termin 📅 2026-13-45"_s << QString();
        QTest::newRow("done") << u"Zrobione wczoraj"_s << QString();
        QTest::newRow("cancelled") << u"Anulowane"_s << QString();
        QTest::newRow("a reminder outside tasks") << u"przypomnienie bez zadania"_s << QString();
    }
    void snapshotTasks()
    {
        QFETCH(QString, text);
        QFETCH(QString, section);
        const auto index = makeIndex(kFixture);
        const QJsonObject snapshot = buildSnapshot(*index, fixtureOptions());
        const QJsonObject task = taskByText(snapshot, text);
        QCOMPARE(task.value(u"section"_s).toString(), section);
    }

    void snapshotFields()
    {
        const auto index = makeIndex(kFixture);
        const QJsonObject snapshot = buildSnapshot(*index, fixtureOptions());
        QCOMPARE(snapshot.value(u"version"_s).toInt(), 1);
        QCOMPARE(snapshot.value(u"built"_s).toString(), u"2026-10-27T14:05"_s);
        QCOMPARE(snapshot.value(u"today"_s).toString(), u"2026-10-27"_s);
        QCOMPARE(snapshot.value(u"vault"_s).toObject().value(u"root"_s).toString(), u"/storage/emulated/0/Brulion"_s);

        const QJsonObject apples = taskByText(snapshot, u"Kupić żółte jabłka"_s);
        QCOMPARE(apples.value(u"reminder"_s).toString(), u"2026-10-27 09:00"_s);
        QCOMPARE(apples.value(u"path"_s).toString(), u"journals/2026-10-27.md"_s);
        QCOMPARE(apples.value(u"line"_s).toInt(), 3);
        QCOMPARE(apples.value(u"key"_s).toString(), u"journals/2026-10-27.md:3"_s);
        QCOMPARE(apples.value(u"page"_s).toString(), u"2026-10-27"_s);
        QVERIFY(!apples.value(u"alarm"_s).toBool());
        QVERIFY(taskByText(snapshot, u"Pobudka na pociąg"_s).value(u"alarm"_s).toBool());
        QCOMPARE(taskByText(snapshot, u"W trakcie"_s).value(u"due"_s).toString(), u"2026-10-29 18:30"_s);
        QCOMPARE(taskByText(snapshot, u"W trakcie"_s).value(u"status"_s).toString(), u"doing"_s);
        QCOMPARE(taskByText(snapshot, u"Zapłacić rachunki"_s).value(u"priority"_s).toInt(), 4);
        QVERIFY(taskByText(snapshot, u"Podlać kwiaty"_s).value(u"recurring"_s).toBool());
        QCOMPARE(taskByText(snapshot, u"Odebrać paczkę"_s).value(u"scheduled"_s).toString(), u"2026-10-20"_s);

        // TasksView's order: sections, then by date.
        QStringList order;
        for (const QJsonValue &v : snapshot.value(u"tasks"_s).toArray())
            order.append(v.toObject().value(u"section"_s).toString());
        QStringList sorted = order;
        const QStringList keys = planning::TaskListModel::sectionKeys();
        std::stable_sort(sorted.begin(), sorted.end(),
                         [&](const QString &a, const QString &b) { return keys.indexOf(a) < keys.indexOf(b); });
        QCOMPARE(order, sorted);

        const QJsonObject counts = snapshot.value(u"taskCounts"_s).toObject();
        QCOMPARE(counts.value(u"overdue"_s).toInt(), 1);
        QCOMPARE(counts.value(u"today"_s).toInt(), 5);
        QCOMPARE(counts.value(u"tomorrow"_s).toInt(), 2);

        // Never HTML of our own: the note's text as written, nothing rendered.
        const QByteArray json = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
        QVERIFY(!json.contains("<a "));
        QVERIFY(!json.contains("<span"));
        QVERIFY(!json.contains("&amp;"));

        const QJsonObject notes = snapshot.value(u"notes"_s).toObject();
        QCOMPARE(notes.value(u"favorites"_s).toArray().size(), 2); // the empty path left out
        QCOMPARE(notes.value(u"recent"_s).toArray().size(), 1);

        const QJsonArray capture = snapshot.value(u"capture"_s).toObject().value(u"days"_s).toArray();
        QCOMPARE(capture.size(), 2);
        // Also for a note that exists: it may be deleted before quick add writes.
        QCOMPARE(capture.at(0).toObject().value(u"newText"_s).toString(), u"# wtorek, 27 października\n\n- "_s);
        QCOMPARE(snapshot.value(u"reminders"_s), QJsonValue(true));
        SnapshotOptions off = fixtureOptions();
        off.reminders = false;
        QCOMPARE(buildSnapshot(*index, off).value(u"reminders"_s), QJsonValue(false));
        QCOMPARE(capture.at(1).toObject().value(u"newText"_s).toString(), u"# środa, 28 października\n\n- "_s);
    }

    void snapshotCalendar()
    {
        const auto index = makeIndex(kFixture);
        const QJsonObject calendar = buildSnapshot(*index, fixtureOptions()).value(u"calendar"_s).toObject();
        // A week before today (from the start of that week) to eight weeks ahead.
        QCOMPARE(calendar.value(u"from"_s).toString(), u"2026-10-19"_s);
        QCOMPARE(calendar.value(u"to"_s).toString(), u"2026-12-22"_s);
        const QJsonObject days = calendar.value(u"days"_s).toObject();
        QVERIFY(!days.contains(u"2026-10-21"_s)); // nothing on that day
        const QJsonObject today = days.value(u"2026-10-27"_s).toObject();
        QCOMPARE(today.value(u"journal"_s).toString(), u"journals/2026-10-27.md"_s);
        const QStringList kinds = today.value(u"kinds"_s).toVariant().toStringList();
        QVERIFY(kinds.contains(u"journal"_s));
        QVERIFY(kinds.contains(u"due"_s));
        QVERIFY(kinds.contains(u"reminder"_s));
        // Timed entries first, by time; then the all-day ones.
        const QJsonArray items = today.value(u"items"_s).toArray();
        QVERIFY(items.size() >= 4);
        QCOMPARE(items.at(0).toObject().value(u"time"_s).toString(), u"09:00"_s);
        QCOMPARE(items.at(0).toObject().value(u"title"_s).toString(), u"Kupić żółte jabłka"_s);
        QVERIFY(items.at(0).toObject().value(u"isTask"_s).toBool());
        QCOMPARE(items.at(1).toObject().value(u"time"_s).toString(), u"18:00"_s);
        QVERIFY(!items.at(1).toObject().value(u"isTask"_s).toBool());
        QCOMPARE(items.at(2).toObject().value(u"minutes"_s).toInt(), -1);
        // The alarm, the cancelled task (closed), the recurring task expanded on later days.
        const QJsonObject alarm = days.value(u"2026-10-28"_s).toObject().value(u"items"_s).toArray().at(0).toObject();
        QVERIFY(alarm.value(u"alarm"_s).toBool());
        QCOMPARE(alarm.value(u"variant"_s).toString(), u"alarm"_s);
        bool closedSeen = false;
        for (const QJsonValue &v : items)
            closedSeen = closedSeen || (v.toObject().value(u"title"_s).toString() == u"Anulowane"_s
                                        && v.toObject().value(u"closed"_s).toBool());
        QVERIFY(closedSeen);
        const QJsonArray later = days.value(u"2026-11-03"_s).toObject().value(u"items"_s).toArray();
        QVERIFY(!later.isEmpty());
        QVERIFY(later.at(0).toObject().value(u"recurring"_s).toBool());
        // Yesterday's overdue task on its day.
        const QJsonArray yesterday = days.value(u"2026-10-26"_s).toObject().value(u"items"_s).toArray();
        QCOMPARE(yesterday.at(0).toObject().value(u"variant"_s).toString(), u"overdue"_s);
    }

    void snapshotLimits()
    {
        const auto index = makeIndex(kFixture);
        SnapshotOptions options = fixtureOptions();
        options.maxTasks = 2;
        options.maxPages = 1;
        options.maxDayItems = 1;
        const QJsonObject snapshot = buildSnapshot(*index, options);
        QCOMPARE(snapshot.value(u"tasks"_s).toArray().size(), 2);
        QCOMPARE(snapshot.value(u"taskCounts"_s).toObject().value(u"today"_s).toInt(), 5); // counts stay full
        QCOMPARE(snapshot.value(u"notes"_s).toObject().value(u"favorites"_s).toArray().size(), 1);
        const QJsonObject days = snapshot.value(u"calendar"_s).toObject().value(u"days"_s).toObject();
        QCOMPARE(days.value(u"2026-10-27"_s).toObject().value(u"items"_s).toArray().size(), 1);

        options = fixtureOptions();
        options.taskDays = 0;
        QVERIFY(taskByText(buildSnapshot(*index, options), u"Zapłacić rachunki"_s).isEmpty());
        QVERIFY(!taskByText(buildSnapshot(*index, options), u"Zadzwonić do hydraulika #dom"_s).isEmpty());
    }

    void snapshotEmpty()
    {
        const auto index = makeIndex({});
        const QJsonObject snapshot = buildSnapshot(*index, fixtureOptions());
        QVERIFY(snapshot.value(u"tasks"_s).toArray().isEmpty());
        QVERIFY(snapshot.value(u"calendar"_s).toObject().value(u"days"_s).toObject().isEmpty());
        QVERIFY(snapshot.value(u"vault"_s).isObject());

        const QJsonObject none = emptySnapshot(kNow, u"en"_s);
        QVERIFY(none.value(u"vault"_s).isNull());
        QCOMPARE(none.value(u"version"_s).toInt(), SnapshotVersion);
        QCOMPARE(none.value(u"locale"_s).toString(), u"en"_s);
        QCOMPARE(none.value(u"today"_s).toString(), u"2026-10-27"_s);
        QCOMPARE(emptySnapshot(kNow, u"de"_s).value(u"locale"_s).toString(), u"pl"_s);
    }

    // Where quick add writes: today's note under another accepted name, a missing one with the
    // template filled for its day.
    void captureDaysOfVault()
    {
        QTemporaryDir tmp;
        const QString root = tmp.filePath(u"v"_s);
        writeFile(root + u"/journals/2026_10_27.md"_s, "- Logseq\n");
        writeFile(root + u"/templates/journal.md"_s, "# {{weekday}}, {{date}}\n\n- ");
        Vault vault(root, tmp.filePath(u"state"_s));
        QVERIFY(vault.open());
        const QList<CaptureDay> days = captureDays(vault, QDate(2026, 10, 27), 3, kNow, dates::UiLocale::Polish);
        QCOMPARE(days.size(), 3);
        QCOMPARE(days.at(0).path, u"journals/2026_10_27.md"_s);
        QVERIFY(days.at(0).exists);
        QCOMPARE(days.at(0).newText, u"# wtorek, 2026-10-27\n\n- "_s); // in case it is deleted
        QCOMPARE(days.at(1).path, u"journals/2026-10-28.md"_s);
        QVERIFY(!days.at(1).exists);
        QCOMPARE(days.at(1).newText, u"# środa, 2026-10-28\n\n- "_s);
        QCOMPARE(days.at(2).date, QDate(2026, 10, 29));
        QCOMPARE(captureDays(vault, QDate(2026, 10, 28), 1, kNow, dates::UiLocale::English).at(0).newText,
                 u"# Wednesday, 2026-10-28\n\n- "_s);
        QVERIFY(!QFileInfo::exists(root + u"/journals/2026-10-28.md"_s)); // never writes
    }

    // The sections of a later day: the rule WidgetSnapshot.kt mirrors.
    void refile_data()
    {
        QTest::addColumn<QJsonObject>("c");
        const QJsonArray cases = readJson(u"refile-cases.json"_s).value(u"cases"_s).toArray();
        QVERIFY(cases.size() >= 20);
        for (const QJsonValue &v : cases)
            QTest::newRow(qPrintable(v.toObject().value(u"name"_s).toString())) << v.toObject();
    }
    void refile()
    {
        QFETCH(QJsonObject, c);
        Task task;
        task.due = spec(c.value(u"due"_s).toString());
        task.scheduled = spec(c.value(u"scheduled"_s).toString());
        task.start = spec(c.value(u"start"_s).toString());
        const QDate today = planning::parseIsoDate(c.value(u"today"_s).toString());
        QString next;
        const std::optional<QDate> reminder = reminderDate(c, today, &next);
        if (c.contains(u"next"_s))
            QCOMPARE(next, c.value(u"next"_s).toString());
        QCOMPARE(planning::TaskListModel::sectionOf(task, today, Qt::DayOfWeek(c.value(u"weekStart"_s).toInt()), reminder),
                 c.value(u"section"_s).toString());
    }

    // A block with several reminders: all of them published, the one that counts today chosen
    // as TasksView does; a later day files it by its next one (the widget, WidgetSnapshot.kt).
    void snapshotSeveralReminders()
    {
        const auto index = makeIndex({{u"Ogród.md"_s,
                                       u"- [ ] Podlać storczyki ⏰ 2026-10-27 09:00 ⏰! 2026-10-30 07:15\n"
                                       "- [ ] Przyciąć róże ⏰ 2026-11-25 10:00 ⏰ 2026-10-29 08:00\n"_s}});
        const QJsonObject snapshot = buildSnapshot(*index, fixtureOptions());
        const QJsonObject orchids = taskByText(snapshot, u"Podlać storczyki"_s);
        QCOMPARE(orchids.value(u"reminder"_s).toString(), u"2026-10-27 09:00"_s);
        QCOMPARE(orchids.value(u"section"_s).toString(), u"today"_s);
        const QJsonArray reminders = orchids.value(u"reminders"_s).toArray();
        QCOMPARE(reminders.size(), 2);
        QCOMPARE(reminders.at(1).toObject().value(u"at"_s).toString(), u"2026-10-30 07:15"_s);
        QVERIFY(reminders.at(1).toObject().value(u"alarm"_s).toBool());
        QCOMPARE(reminderDate(orchids, QDate(2026, 10, 28)), std::optional<QDate>(QDate(2026, 10, 30)));
        Task task;
        QCOMPARE(planning::TaskListModel::sectionOf(task, QDate(2026, 10, 28), Qt::Monday,
                                                    reminderDate(orchids, QDate(2026, 10, 28))),
                 u"thisWeek"_s);
        // Filed by its next reminder (the 29th, within 14 days), not its first one (beyond).
        const QJsonObject roses = taskByText(snapshot, u"Przyciąć róże"_s);
        QCOMPARE(roses.value(u"section"_s).toString(), u"thisWeek"_s);
        QCOMPARE(roses.value(u"reminder"_s).toString(), u"2026-10-29 08:00"_s);
    }

    // The golden snapshot's sections are what sectionOf() gives for its tasks (the Kotlin test
    // re-files the same tasks).
    void goldenSectionsAreSectionOf()
    {
        const QJsonObject golden = readJson(u"snapshot-v1.json"_s);
        const QDate today = planning::parseIsoDate(golden.value(u"today"_s).toString());
        const QJsonArray tasks = golden.value(u"tasks"_s).toArray();
        QVERIFY(!tasks.isEmpty());
        for (const QJsonValue &v : tasks) {
            const QJsonObject t = v.toObject();
            Task task;
            task.due = spec(t.value(u"due"_s).toString());
            task.scheduled = spec(t.value(u"scheduled"_s).toString());
            task.start = spec(t.value(u"start"_s).toString());
            QCOMPARE(planning::TaskListModel::sectionOf(task, today, Qt::Monday, reminderDate(t, today)),
                     t.value(u"section"_s).toString());
        }
    }

    void lines_data()
    {
        QTest::addColumn<QJsonObject>("action");
        QTest::addColumn<QString>("line");
        const QJsonArray cases = readJson(u"journal-cases.json"_s).value(u"lines"_s).toArray();
        QVERIFY(cases.size() >= 20);
        for (const QJsonValue &v : cases) {
            QJsonObject action = v.toObject().value(u"action"_s).toObject();
            action.insert(u"id"_s, u"case"_s);
            QTest::newRow(qPrintable(v.toObject().value(u"name"_s).toString()))
                    << action << v.toObject().value(u"line"_s).toString();
        }
    }
    void lines()
    {
        QFETCH(QJsonObject, action);
        QFETCH(QString, line);
        const auto parsed = widgetActionFromJson(action);
        QVERIFY(parsed);
        QCOMPARE(widgetActionBlock(*parsed), line);
    }

    void append_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("expected");
        const QJsonArray cases = readJson(u"journal-cases.json"_s).value(u"append"_s).toArray();
        QVERIFY(cases.size() >= 15);
        for (const QJsonValue &v : cases) {
            const QJsonObject c = v.toObject();
            QTest::newRow(qPrintable(c.value(u"name"_s).toString()))
                    << c.value(u"content"_s).toString() << c.value(u"block"_s).toString()
                    << c.value(u"expected"_s).toString();
        }
    }
    void append()
    {
        QFETCH(QString, content);
        QFETCH(QString, block);
        QFETCH(QString, expected);
        QCOMPARE(notebook::appendBlock(content, block), expected);
    }

    void queuedActions()
    {
        const QJsonArray array{
                QJsonObject{{u"id"_s, u"a"_s}, {u"kind"_s, u"task"_s}, {u"text"_s, u"Kupić ser"_s},
                            {u"date"_s, u"2026-10-28"_s}, {u"time"_s, u"07:15"_s}, {u"alarm"_s, true},
                            {u"vault"_s, u"v1"_s}},
                QJsonObject{{u"id"_s, u"a"_s}, {u"kind"_s, u"task"_s}, {u"text"_s, u"duplikat"_s}},
                QJsonObject{{u"kind"_s, u"task"_s}, {u"text"_s, u"bez id"_s}},
                QJsonObject{{u"id"_s, u"c"_s}, {u"kind"_s, u"zadanie"_s}, {u"text"_s, u"zły rodzaj"_s}},
                QJsonValue(u"nie obiekt"_s),
                QJsonObject{{u"id"_s, u"d"_s}, {u"kind"_s, u"note"_s}, {u"text"_s, u"Myśl"_s}},
                QJsonObject{{u"id"_s, u"e"_s}, {u"text"_s, u"domyślnie zadanie"_s}},
        };
        const QList<WidgetAction> actions = widgetActions(array);
        QCOMPARE(actions.size(), 3);
        QCOMPARE(actions.at(0).id, u"a"_s);
        QCOMPARE(actions.at(0).vault, u"v1"_s);
        QCOMPARE(widgetActionBlock(actions.at(0)), u"- [ ] Kupić ser ⏰! 2026-10-28 07:15"_s);
        QCOMPARE(widgetActionBlock(actions.at(1)), u"- Myśl"_s);
        QCOMPARE(actions.at(2).kind, WidgetAction::Kind::Task);
        // An entry the app could not write goes back as WidgetInbox.kt keeps it, unchanged.
        const QJsonObject note{{u"id"_s, u"n-ż"_s}, {u"kind"_s, u"note"_s}, {u"text"_s, u"Myśl\ndruga linia"_s},
                               {u"date"_s, u""_s}, {u"time"_s, u""_s}, {u"alarm"_s, false},
                               {u"vault"_s, u"v1"_s}, {u"added"_s, u"2026-10-27T23:58"_s}};
        QCOMPARE(widgetActionToJson(*widgetActionFromJson(note)), note);
        const QJsonObject requeued = widgetActionToJson(actions.at(0));
        QCOMPARE(requeued.value(u"date"_s).toString(), u"2026-10-28"_s);
        QCOMPARE(requeued.value(u"time"_s).toString(), u"07:15"_s);
        QVERIFY(requeued.value(u"alarm"_s).toBool());
        QCOMPARE(widgetActionBlock(*widgetActionFromJson(requeued)), widgetActionBlock(actions.at(0)));
        QVERIFY(widgetActionsAddedMessage(3).contains(u'3'));
        QVERIFY(widgetActionFailedMessage(u"Kupić\nser"_s).contains(u"Kupić ser"_s));
    }
};

QTEST_GUILESS_MAIN(TestWidgets)
#include "tst_widgets.moc"
