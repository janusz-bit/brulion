// End-to-end scenarios across the core modules on a realistic Polish vault (integrationtest.h):
// Vault -> VaultIndex -> queries, calculations, calendar and notifications -> BlockDocument
// edits of a journal written back through the vault -> Logseq and Markdown export and re-import
// into a second vault -> two devices syncing through the in-memory WebDAV server (merge of
// concurrent journal appends, the mass-deletion guard).
#include "integrationtest.h"

#include "calc/calc.h"
#include "calc/table.h"
#include "fakewebdavserver.h"
#include "io/io.h"
#include "reminders/reminders.h"
#include "synctest.h"

#include <QRegularExpression>
#include <QSet>
#include <QTest>
#include <QTimeZone>

#include <algorithm>

using namespace integration;
using synctest::Counts;
using synctest::Device;
using synctest::FakeWebDavServer;

namespace {

QStringList notificationLines(const QList<ScheduledNotification> &list)
{
    QStringList out;
    for (const ScheduledNotification &n : list) {
        out.append(n.at.toString(u"yyyy-MM-dd HH:mm"_s) + (n.alarm ? u" ⏰! "_s : u" ⏰ "_s) + n.title + u" @ "_s
                   + n.path);
    }
    return out;
}

QStringList notificationIds(const QList<ScheduledNotification> &list)
{
    QStringList out;
    for (const ScheduledNotification &n : list)
        out.append(n.id);
    return out;
}

QString kindName(CalendarItem::Kind kind)
{
    switch (kind) {
    case CalendarItem::Journal: return u"journal"_s;
    case CalendarItem::Due: return u"due"_s;
    case CalendarItem::Scheduled: return u"scheduled"_s;
    case CalendarItem::Start: return u"start"_s;
    case CalendarItem::Reminder: return u"reminder"_s;
    case CalendarItem::Done: return u"done"_s;
    }
    return {};
}

QStringList calendarLines(const QList<CalendarItem> &items)
{
    QStringList out;
    for (const CalendarItem &item : items) {
        QString line = item.date.toString(u"MM-dd"_s);
        if (item.time)
            line += u' ' + item.time->toString(u"HH:mm"_s);
        line += u' ' + kindName(item.kind) + u' ' + item.title;
        if (item.recurring)
            line += u" 🔁"_s;
        if (item.alarm)
            line += u" !"_s;
        out.append(line);
    }
    return out;
}

QStringList queryTexts(const VaultIndex &index, const QString &source)
{
    const auto parsed = parseQuery(source);
    if (const QString *error = std::get_if<QString>(&parsed)) {
        qWarning().noquote() << "query error:" << *error;
        return {u"ERROR"_s};
    }
    const QueryResult result = runQuery(std::get<Query>(parsed), index, today());
    QStringList out;
    for (const QueryItem &item : result.items) {
        if (item.task)
            out.append(item.task->text);
        else
            out.append(index.note(item.path)->blocks.value(item.blockIndex).text);
    }
    return out;
}

QList<ScheduledNotification> upcoming(const VaultIndex &index)
{
    return upcomingNotifications(index, now(), 14);
}

/// Every file of a vault as export input (the vault config included).
io::ExportInput exportInput(Vault &vault)
{
    io::ExportInput input;
    for (const FileStat &file : vault.files())
        input.files.append(io::FileEntry{file.path, vault.readBinary(file.path)});
    input.files.append(io::FileEntry{kConfig, vault.readBinary(kConfig)});
    input.config = vault.config();
    input.vaultName = u"Mój brulion"_s;
    input.now = now();
    return input;
}

io::ExportedFile exportWith(Vault &vault, const QString &id)
{
    const io::Exporter *exporter = io::exporterById(id);
    if (!exporter) {
        qWarning().noquote() << "no exporter" << id;
        return {};
    }
    const io::ExportResult result = io::runExport(*exporter, exportInput(vault));
    if (!result.warnings.isEmpty())
        qDebug().noquote() << id << "warnings:" << result.warnings;
    return result.files.value(0);
}

/// Import `file` with `importerId` into `vault` like the app does (daily notes appended).
io::ImportResult importInto(Vault &vault, const QString &importerId, const io::ExportedFile &file)
{
    io::ImportOptions options;
    options.config = vault.config();
    options.timeZone = QTimeZone("Europe/Warsaw");
    options.exists = [&vault](const VaultPath &path) { return vault.exists(path); };
    const io::ImportResult result =
            io::runImport(*io::importerById(importerId), {io::InputFile{file.name, file.data}}, options);
    for (const io::ImportedFile &imported : result.files) {
        QByteArray data = imported.data;
        if (imported.append && vault.exists(imported.path))
            data = io::appendImported(vault.readBinary(imported.path), imported.data);
        if (!vault.writeBinary(imported.path, data, ChangeSource::Import))
            qWarning().noquote() << "cannot write" << imported.path << vault.errorString();
    }
    return result;
}

/// Tasks of a note as comparable strings (status, text, dates, recurrence, priority).
QStringList taskSignatures(const VaultIndex &index, const VaultPath &path)
{
    QStringList out;
    for (const TaskRef &ref : index.tasks()) {
        if (ref.path != path)
            continue;
        const Task &t = ref.task;
        out.append(u"%1|%2|%3|%4|%5|%6|%7"_s.arg(int(t.status))
                           .arg(t.text, t.due ? t.due->toString() : QString(),
                                t.scheduled ? t.scheduled->toString() : QString(), t.recurrence)
                           .arg(int(t.priority))
                           .arg(t.doneDate ? t.doneDate->toString(Qt::ISODate) : QString()));
    }
    return out;
}

QStringList reminderSignatures(const VaultIndex &index)
{
    QStringList out;
    for (const ReminderRef &ref : index.reminders()) {
        out.append(ref.path + u'|' + ref.reminder.at.toString() + (ref.reminder.alarm ? u"|!"_s : u"|"_s)
                   + ref.reminder.recurrence);
    }
    return out;
}

/// Index of the BlockDocument block whose body contains `text`, -1 if none.
int blockWith(const markdown::BlockDocument &doc, const QString &text)
{
    for (int i = 0; i < doc.count(); ++i) {
        if (doc.at(i).body.contains(text))
            return i;
    }
    return -1;
}

std::optional<VaultPath> blockRefTarget(const VaultIndex &index, const VaultPath &from)
{
    for (const ResolvedLink &link : index.outgoingLinks(from)) {
        if (link.link.kind == LinkKind::BlockRef)
            return link.target;
    }
    return std::nullopt;
}

std::optional<VaultPath> embedTarget(const VaultIndex &index, const VaultPath &from)
{
    for (const ResolvedLink &link : index.outgoingLinks(from)) {
        if (link.link.kind == LinkKind::Embed
            || (link.link.kind == LinkKind::Markdown && !paths::isMarkdown(link.target)))
            return link.target;
    }
    return std::nullopt;
}

QStringList backlinkPaths(const VaultIndex &index, const VaultPath &path)
{
    QStringList out;
    for (const Backlink &b : index.backlinks(path)) {
        if (!out.contains(b.path))
            out.append(b.path);
    }
    return out;
}

/// In-app notification sink (like the WebAssembly / desktop-without-daemon notifier).
class InAppSink : public NotificationSink {
public:
    bool firesWhenClosed() const override { return false; }
    void schedule(const QList<ScheduledNotification> &notifications) override { scheduled.append(notifications); }
    void showNow(const ScheduledNotification &notification) override { shown.append(notification.title); }

    QList<QList<ScheduledNotification>> scheduled;
    QStringList shown;
};

bool hasTitle(const QList<ScheduledNotification> &list, const QString &title)
{
    return std::any_of(list.cbegin(), list.cend(), [&](const ScheduledNotification &n) { return n.title == title; });
}

} // namespace

class TestIntegration : public QObject {
    Q_OBJECT

public:
    static void initMain() { qputenv("TZ", "Europe/Warsaw"); }

private Q_SLOTS:
    void openIndexAndQuery()
    {
        FakeWebDavServer server;
        Device device(server, u"laptop"_s, sampleVault());
        QVERIFY(device.vault->scanStatus().isOk());
        QCOMPARE(device.vault->config().journalsFolder, u"journals"_s);
        Indexer indexer(*device.vault);
        const VaultIndex &index = indexer.index;

        QCOMPARE(index.paths().size(), 7 + kArchiveNotes);
        QCOMPARE(index.attachments(), QList<VaultPath>{kPhoto});
        QCOMPARE(index.journals(),
                 (QList<JournalRef>{{QDate(2026, 10, 14), kJournal14}, {QDate(2026, 10, 13), kJournal13}}));

        // Links (SYNTAX §2): aliases from frontmatter and Logseq properties, case, attachments.
        QCOMPARE(index.resolveLink(u"Kuchnia"_s), std::optional<VaultPath>(kKitchen));
        QCOMPARE(index.resolveLink(u"Janek"_s), std::optional<VaultPath>(kJan));
        QCOMPARE(index.resolveLink(u"jan kowalski"_s), std::optional<VaultPath>(kJan));
        QCOMPARE(index.resolveLink(u"kuchnia.png"_s), std::optional<VaultPath>(kPhoto));
        QVERIFY(index.missingPages().isEmpty());
        QCOMPARE(backlinkPaths(index, kKitchen), (QStringList{kJournal14, kJan, kShopping}));
        QCOMPARE(backlinkPaths(index, kJan), (QStringList{kJournal14, kJournal13}));
        QCOMPARE(backlinkPaths(index, kJournal14), QStringList{kKitchen}); // ((ustalenia))
        QCOMPARE(index.blockById(u"ustalenia"_s)->path, kJournal14);
        QCOMPARE(blockRefTarget(index, kKitchen), std::optional<VaultPath>(kJournal14));
        QCOMPARE(embedTarget(index, kKitchen), std::optional<VaultPath>(kPhoto));

        // Tags (SYNTAX §3): block tags, frontmatter tags, Logseq `tags::`.
        QHash<QString, int> tagCounts;
        for (const TagCount &tag : index.tags())
            tagCounts.insert(tag.tag, tag.count);
        QCOMPARE(tagCounts.value(u"archiwum"_s), kArchiveNotes);
        QCOMPARE(tagCounts.value(u"zakupy"_s), 4);
        QCOMPARE(tagCounts.value(u"praca"_s), 2);
        for (const QString &tag : {u"dom"_s, u"remont"_s, u"znajomi"_s, u"dziennik"_s, u"rodzina"_s})
            QVERIFY2(tagCounts.contains(tag), qPrintable(tag));

        // Tasks (SYNTAX §5): checkboxes, Logseq markers, planning lines, Polish recurrence.
        const std::optional<TaskRef> flowers = findTask(index, u"Podlać kwiaty"_s);
        QVERIFY(flowers);
        QCOMPARE(flowers->task.recurrence, u"every week on Wednesday"_s);
        QCOMPARE(flowers->task.due->toString(), u"2026-10-14"_s);
        const std::optional<TaskRef> gift = findTask(index, u"Kupić prezent"_s);
        QVERIFY(gift);
        QCOMPARE(gift->task.status, TaskStatus::Todo);
        QCOMPARE(gift->task.scheduled->toString(), u"2026-10-20"_s);
        QCOMPARE(gift->task.recurrence, u"every year"_s);
        QCOMPARE(findTask(index, u"Oddać wiertarkę"_s)->task.status, TaskStatus::Done);
        QCOMPARE(findTask(index, u"Zamówić fachowca"_s)->task.status, TaskStatus::Doing);
        QCOMPARE(findTask(index, u"Kupić płytki"_s)->task.priority, Priority::High);
        QCOMPARE(findTask(index, u"Zapłacić czynsz"_s)->task.priority, Priority::Medium);
        QCOMPARE(index.tasks().size(), 13);
        QCOMPARE(index.reminders().size(), 6);

        // Queries (SYNTAX §10).
        QCOMPARE(queryTexts(index, u"type:tasks status:open due:<=+7d sort:due"_s),
                 (QStringList{u"Podlać kwiaty"_s, u"Wynieść śmieci"_s, u"Kupić płytki #zakupy"_s}));
        QCOMPARE(queryTexts(index, u"tag:zakupy status:todo"_s).size(), 3);
        QCOMPARE(queryTexts(index, u"type:tasks status:done"_s).size(), 3);
        QCOMPARE(queryTexts(index, u"type:reminders remind:today"_s).size(), 3);
        QCOMPARE(queryTexts(index, u"type:pages tag:remont"_s).size(), 1);
        QCOMPARE(queryTexts(index, u"type:tasks status:doing,todo page:\"Remont kuchni\""_s).size(), 3);

        // Full-text search: diacritic-insensitive, title first.
        const QList<SearchHit> hits = index.search(u"plytki"_s);
        QVERIFY(!hits.isEmpty());
        QStringList hitPaths;
        for (const SearchHit &hit : hits)
            hitPaths.append(hit.path);
        QVERIFY(hitPaths.contains(kKitchen));
        QVERIFY(hitPaths.contains(kJournal14));
        QCOMPARE(index.search(u"kowalski"_s).value(0).path, kJan);
    }

    void calculations()
    {
        // Calc block with Polish decimal commas, Unicode names and percent sugar.
        const QList<calc::CalcBlock> blocks = calc::findCalcBlocks(kKitchenText);
        QCOMPARE(blocks.size(), 1);
        calc::Scope scope;
        const QList<calc::CalcLineResult> lines = calc::evaluateCalc(blocks.first().source, scope);
        QCOMPARE(lines.size(), 6);
        for (const calc::CalcLineResult &line : lines)
            QVERIFY2(!line.hasError(), qPrintable(line.input + u": "_s + line.error));
        QCOMPARE(lines.at(3).numeric, std::optional<double>(1842));
        QCOMPARE(lines.at(5).numeric, std::optional<double>(1657.8));
        QCOMPARE(lines.at(5).value, u"1657,8"_s);

        // Inline calculation sees the calc block's variables (page scope).
        const int inlineLine = int(kKitchenText.left(kKitchenText.indexOf(u"Do zapłaty"_s)).count(u'\n'));
        const calc::Scope pageScope = calc::pageScope(kKitchenText, inlineLine);
        const calc::CalcLineResult inlineResult = calc::evaluateInline(u"łącznie - rabat"_s, pageScope);
        QCOMPARE(inlineResult.numeric, std::optional<double>(1657.8));

        // Table formulas: [Kolumna] in the current row and sum(above).
        const QList<calc::TableSpan> tables = calc::findTables(kKitchenText);
        QCOMPARE(tables.size(), 1);
        const QString tableText = kKitchenText.mid(tables.first().from, tables.first().to - tables.first().from);
        const std::optional<calc::TableModel> model = calc::parseTable(tableText);
        QVERIFY(model);
        const calc::TableEval eval = calc::evaluateTable(*model);
        QCOMPARE(eval.values.size(), 3);
        QCOMPARE(eval.values.at(0).at(3), u"546"_s);
        QCOMPARE(eval.values.at(1).at(3), u"96"_s);
        QCOMPARE(eval.values.at(2).at(3), u"642"_s);
    }

    void calendarAndNotifications()
    {
        FakeWebDavServer server;
        Device device(server, u"laptop"_s, sampleVault());
        Indexer indexer(*device.vault);

        QCOMPARE(calendarLines(calendarItems(indexer.index, QDate(2026, 10, 12), QDate(2026, 10, 16))),
                 (QStringList{
                         u"10-12 done Kawa #zakupy"_s,
                         u"10-13 journal 2026-10-13"_s,
                         u"10-13 done Wysłać raport kwartalny #praca"_s,
                         u"10-14 12:30 reminder Zadzwonić do mamy #rodzina"_s,
                         u"10-14 18:00 reminder Podlać kwiaty 🔁"_s,
                         u"10-14 20:00 reminder Wieczorny przegląd"_s,
                         u"10-14 journal 2026-10-14"_s,
                         u"10-14 due Podlać kwiaty 🔁"_s,
                         u"10-15 07:00 reminder Wynieść śmieci 🔁"_s,
                         u"10-15 07:30 reminder Odebrać zamówienie !"_s,
                         u"10-15 scheduled Zamówić fachowca"_s,
                         u"10-15 due Wynieść śmieci 🔁"_s,
                         u"10-16 10:00 reminder Wizyta u dentysty"_s,
                         u"10-16 due Kupić płytki #zakupy"_s,
                 }));
        // A recurring task keeps its series in the calendar.
        const QStringList later = calendarLines(calendarItems(indexer.index, QDate(2026, 10, 19), QDate(2026, 10, 22)));
        QVERIFY2(later.contains(u"10-19 due Wynieść śmieci 🔁"_s), qPrintable(later.join(u'\n')));
        QVERIFY(later.contains(u"10-21 18:00 reminder Podlać kwiaty 🔁"_s));
        QVERIFY(later.contains(u"10-20 scheduled Kupić prezent na urodziny 🔁"_s));

        int dropped = -1;
        const QList<ScheduledNotification> list = upcomingNotifications(indexer.index, now(), 14, 64, &dropped);
        QCOMPARE(dropped, 0);
        QCOMPARE(notificationLines(list), (QStringList{
                                                  u"2026-10-14 12:30 ⏰ Zadzwonić do mamy #rodzina @ "_s + kJournal14,
                                                  u"2026-10-14 18:00 ⏰ Podlać kwiaty @ "_s + kJournal14,
                                                  u"2026-10-14 20:00 ⏰ Wieczorny przegląd @ "_s + kChores,
                                                  u"2026-10-15 07:00 ⏰ Wynieść śmieci @ "_s + kChores,
                                                  u"2026-10-15 07:30 ⏰! Odebrać zamówienie @ "_s + kKitchen,
                                                  u"2026-10-16 10:00 ⏰ Wizyta u dentysty @ "_s + kChores,
                                                  u"2026-10-19 07:00 ⏰ Wynieść śmieci @ "_s + kChores,
                                                  u"2026-10-21 18:00 ⏰ Podlać kwiaty @ "_s + kJournal14,
                                                  u"2026-10-22 07:00 ⏰ Wynieść śmieci @ "_s + kChores,
                                                  u"2026-10-26 07:00 ⏰ Wynieść śmieci @ "_s + kChores,
                                          }));
        // Lines point at the reminder token; bodies name the page (and the due date of tasks).
        QCOMPARE(list.at(0).line, 2);
        QCOMPARE(list.at(4).body, u"Remont kuchni"_s);
        QVERIFY2(list.at(1).body.startsWith(u"2026-10-14 · termin"_s), qPrintable(list.at(1).body));
        QSet<int> nativeIds;
        for (const ScheduledNotification &n : list)
            nativeIds.insert(n.nativeId);
        QCOMPARE(nativeIds.size(), list.size());
    }

    void editingAJournalWithTheBlockEditor()
    {
        FakeWebDavServer server;
        Device device(server, u"laptop"_s, sampleVault());
        Indexer indexer(*device.vault);
        Vault &vault = *device.vault;

        // Lossless round trip of every note (CRLF, frontmatter, tables, code, Logseq lines).
        for (const FileStat &file : vault.markdownFiles()) {
            const QString content = vault.readText(file.path);
            QCOMPARE(markdown::BlockDocument(content).toString(), content);
        }

        const QList<ScheduledNotification> before = upcoming(indexer.index);
        markdown::BlockDocument doc(vault.readText(kJournal14));
        QCOMPARE(doc.count(), 6);

        // Complete the recurring task: the next occurrence goes above it (Obsidian Tasks).
        const int flowers = blockWith(doc, u"Podlać kwiaty"_s);
        QVERIFY(doc.toggleTask(flowers, today()).changed());
        QCOMPARE(doc.count(), 7);
        QCOMPARE(doc.at(flowers).taskStatus, std::optional<TaskStatus>(TaskStatus::Todo));
        QCOMPARE(doc.at(flowers + 1).taskStatus, std::optional<TaskStatus>(TaskStatus::Done));
        QCOMPARE(doc.at(flowers).depth, 1);

        // Enter at the end of "Zadzwonić do mamy", type a new task, Tab it under its sibling.
        const int call = blockWith(doc, u"Zadzwonić do mamy"_s);
        const markdown::EditResult split = doc.split(call, int(doc.at(call).body.size()));
        QVERIFY(split.changed());
        const int added = split.focus;
        QCOMPARE(added, call + 1);
        QCOMPARE(doc.at(added).taskStatus, std::optional<TaskStatus>(TaskStatus::Todo));
        QVERIFY(doc.setBody(added, u"[ ] Kupić bilety do kina ⏰ 2026-10-14 17:00"_s).changed());
        QVERIFY(doc.indent(added).changed());
        QCOMPARE(doc.at(added).depth, 2);
        QCOMPARE(doc.parent(added), call);

        const QString edited = doc.toString();
        const QStringList lines = edited.split(u'\n');
        QCOMPARE(lines.at(0), u"- Środa — plan dnia #dziennik"_s);
        QCOMPARE(lines.at(1), u"\t- [ ] Podlać kwiaty 📅 2026-10-21 🔁 every week on Wednesday ⏰ 2026-10-21 18:00"_s);
        // SYNTAX §7: the Polish rule is written in canonical form when Brulion rewrites the line.
        QCOMPARE(lines.at(2),
                 u"\t- [x] Podlać kwiaty 📅 2026-10-14 🔁 every week on Wednesday ⏰ 2026-10-14 18:00 ✅ 2026-10-14"_s);
        QCOMPARE(lines.at(3), u"\t- [ ] Zadzwonić do mamy ⏰ 2026-10-14 12:30 #rodzina"_s);
        QCOMPARE(lines.at(4), u"\t\t- [ ] Kupić bilety do kina ⏰ 2026-10-14 17:00"_s);
        // The untouched second half keeps its exact bytes.
        QVERIFY(edited.endsWith(kJournal14Text.mid(kJournal14Text.indexOf(u"- Notatki ze spotkania"_s))));

        QVERIFY(vault.writeText(kJournal14, edited));
        QCOMPARE(taskTexts(indexer.index, kJournal14),
                 (QStringList{u"Podlać kwiaty"_s, u"Podlać kwiaty"_s, u"Zadzwonić do mamy #rodzina"_s,
                              u"Kupić bilety do kina"_s}));
        // The completed occurrence no longer reminds; the next one keeps its identity.
        const QList<ScheduledNotification> after = upcoming(indexer.index);
        QStringList expectedIds = notificationIds(before);
        QStringList afterIds = notificationIds(after);
        const ScheduledNotification todayFlowers = before.at(1);
        QCOMPARE(todayFlowers.title, u"Podlać kwiaty"_s);
        expectedIds.removeAll(todayFlowers.id);
        const auto cinema = std::find_if(after.cbegin(), after.cend(), [](const ScheduledNotification &n) {
            return n.title == u"Kupić bilety do kina"_s;
        });
        QVERIFY(cinema != after.cend());
        QCOMPARE(cinema->at, QDateTime(QDate(2026, 10, 14), QTime(17, 0)));
        afterIds.removeAll(cinema->id);
        QCOMPARE(afterIds, expectedIds);

        // Snooze the call by an hour through the reminders module; the index follows.
        const ScheduledNotification callReminder = after.at(0);
        QCOMPARE(callReminder.title, u"Zadzwonić do mamy #rodzina"_s);
        const QString snoozed = snoozeReminder(vault.readText(kJournal14), callReminder.line,
                                               QDateTime(QDate(2026, 10, 14), QTime(13, 30)));
        QVERIFY(vault.writeText(kJournal14, snoozed));
        QVERIFY(snoozed.contains(u"\t- [ ] Zadzwonić do mamy ⏰ 2026-10-14 13:30 #rodzina\n"_s));
        QCOMPARE(upcoming(indexer.index).at(0).at, QDateTime(QDate(2026, 10, 14), QTime(13, 30)));
        QCOMPARE(markdown::BlockDocument(snoozed).toString(), snoozed);
    }

    void logseqExportAndReimport()
    {
        FakeWebDavServer server;
        Device source(server, u"laptop"_s, sampleVault());
        Indexer original(*source.vault);
        const io::ExportedFile zip = exportWith(*source.vault, u"logseq-zip"_s);
        QVERIFY(!zip.data.isEmpty());

        Device target(server, u"drugi"_s);
        Indexer copy(*target.vault);
        const io::ImportResult imported = importInto(*target.vault, u"logseq"_s, zip);
        if (!imported.warnings.isEmpty())
            qDebug().noquote() << "import warnings:" << imported.warnings;
        const VaultIndex &back = copy.index;

        // Pages and journals keep their paths; tasks keep status, dates, recurrence, priority.
        for (const QString &path : {kJournal13, kJournal14, kKitchen, kJan, kChores, kShopping}) {
            QVERIFY2(back.note(path), qPrintable(path + u" missing; have: "_s + back.paths().join(u", "_s)));
            QCOMPARE(taskSignatures(back, path), taskSignatures(original.index, path));
        }
        QCOMPARE(back.tasks().size(), original.index.tasks().size());
        // Reminders and alarms survive (Logseq shows the tokens as text).
        QCOMPARE(reminderSignatures(back), reminderSignatures(original.index));
        QCOMPARE(notificationLines(upcoming(back)), notificationLines(upcoming(original.index)));
        // Links, aliases, the block reference and the attachment still resolve.
        QCOMPARE(back.resolveLink(u"Kuchnia"_s), std::optional<VaultPath>(kKitchen));
        QCOMPARE(back.resolveLink(u"Janek"_s), std::optional<VaultPath>(kJan));
        QVERIFY(back.missingPages().isEmpty());
        QCOMPARE(backlinkPaths(back, kJan), (QStringList{kJournal14, kJournal13}));
        QCOMPARE(blockRefTarget(back, kKitchen), std::optional<VaultPath>(kJournal14));
        const std::optional<VaultPath> photo = embedTarget(back, kKitchen);
        QVERIFY(photo);
        QCOMPARE(target.vault->readBinary(*photo), photoBytes());
        // Tags, calculations and table formulas are still there.
        QStringList tags;
        for (const TagCount &tag : back.tags())
            tags.append(tag.tag);
        for (const QString &tag : {u"zakupy"_s, u"praca"_s, u"dom"_s, u"remont"_s, u"archiwum"_s})
            QVERIFY2(tags.contains(tag), qPrintable(tag));
        const QString kitchen = target.vault->readText(kKitchen);
        QCOMPARE(calc::findCalcBlocks(kitchen).value(0).source, calc::findCalcBlocks(kKitchenText).value(0).source);
        QVERIFY(kitchen.contains(u"=[Cena]*[Ilość]"_s));
    }

    void markdownExportAndReimport()
    {
        FakeWebDavServer server;
        Device source(server, u"laptop"_s, sampleVault());
        Indexer original(*source.vault);
        const io::ExportedFile zip = exportWith(*source.vault, u"markdown-zip"_s);
        QVERIFY(!zip.data.isEmpty());

        Device target(server, u"drugi"_s);
        Indexer copy(*target.vault);
        importInto(*target.vault, u"markdown"_s, zip);
        // Every note arrives byte for byte at its path (CRLF included).
        for (const FileStat &file : source.vault->markdownFiles()) {
            QVERIFY2(target.vault->exists(file.path), qPrintable(file.path));
            QCOMPARE(target.vault->readBinary(file.path), source.vault->readBinary(file.path));
        }
        QCOMPARE(copy.index.paths(), original.index.paths());
        QCOMPARE(reminderSignatures(copy.index), reminderSignatures(original.index));
        const std::optional<VaultPath> photo = embedTarget(copy.index, kKitchen);
        QVERIFY(photo);
        QCOMPARE(target.vault->readBinary(*photo), photoBytes());
    }

    void twoDevicesSyncAndConverge()
    {
        synctest::TestClock clock;
        FakeWebDavServer server(synctest::serverOptions(clock));
        synctest::DeviceOptions options;
        options.clock = &clock;
        Device laptop(server, u"laptop"_s, sampleVault(), options);
        Device phone(server, u"telefon"_s, {}, options);
        Indexer onLaptop(*laptop.vault);
        Indexer onPhone(*phone.vault);
        const int files = int(sampleVault().size());

        QCOMPARE(synctest::counts(laptop.sync()), (Counts{.up = files}));
        QCOMPARE(synctest::counts(phone.sync()), (Counts{.down = files}));
        QCOMPARE(phone.files(), laptop.files());
        QCOMPARE(phone.vault->readBinary(kPhoto), photoBytes());
        QCOMPARE(phone.vault->config().journalsFolder, u"journals"_s);
        // Same index, same notifications with the same ids on both devices.
        QCOMPARE(onPhone.index.paths(), onLaptop.index.paths());
        QCOMPARE(notificationIds(upcoming(onPhone.index)), notificationIds(upcoming(onLaptop.index)));

        // Both devices append to today's journal (and edit other notes) while offline.
        clock.advance(60'000);
        QVERIFY(laptop.write(kJournal14, laptop.read(kJournal14) + u"- Wieczorem kino z [[Jan Kowalski]]\n"_s));
        QVERIFY(phone.write(kJournal14, phone.read(kJournal14) + u"- [ ] Kupić mleko ⏰ 2026-10-14 16:00 #zakupy\n"_s));
        QVERIFY(phone.write(kShopping, phone.read(kShopping).replace(u"- [ ] Chleb"_s, u"- [x] Chleb"_s)));
        QVERIFY(laptop.write(kChores, laptop.read(kChores) + u"- Odkurzyć\r\n"_s));
        QCOMPARE(synctest::counts(laptop.sync()), (Counts{.up = 2}));
        QCOMPARE(synctest::counts(phone.sync()), (Counts{.up = 1, .down = 1, .merged = 1}));
        QCOMPARE(synctest::counts(laptop.sync()), (Counts{.down = 2}));
        QCOMPARE(laptop.files(), phone.files());
        const QString journal = laptop.read(kJournal14);
        QVERIFY(journal.startsWith(kJournal14Text));
        QVERIFY(journal.contains(u"- Wieczorem kino z [[Jan Kowalski]]\n"_s));
        QVERIFY(journal.contains(u"- [ ] Kupić mleko ⏰ 2026-10-14 16:00 #zakupy\n"_s));
        QVERIFY(laptop.read(kChores).endsWith(u"- Wizyta u dentysty (@2026-10-16 10:00)\r\n- Odkurzyć\r\n"_s));
        // The indexes followed the synced files on both sides.
        QCOMPARE(findTask(onLaptop.index, u"Chleb"_s)->task.status, TaskStatus::Done);
        QCOMPARE(notificationLines(upcoming(onLaptop.index)), notificationLines(upcoming(onPhone.index)));
        QVERIFY(notificationLines(upcoming(onLaptop.index))
                        .contains(u"2026-10-14 16:00 ⏰ Kupić mleko #zakupy @ "_s + kJournal14));
        QCOMPARE(backlinkPaths(onPhone.index, kJan).size(), 2);

        // The phone clears the archive folder: 12 of 20 notes — the guard asks first.
        QVERIFY(phone.vault->remove(u"Archiwum"_s));
        const synctest::SyncReport held = phone.sync();
        QVERIFY(held.needsConfirmation);
        QCOMPARE(held.pendingDeletions.size(), kArchiveNotes);
        QCOMPARE(held.pendingDeletions.first(),
                 (synctest::SyncChange{archiveNote(0), synctest::SyncAction::DeleteRemote}));
        QCOMPARE(server.files(synctest::kFolder).size(), files);
        synctest::SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        QCOMPARE(synctest::counts(phone.sync(confirm)), (Counts{.delR = kArchiveNotes}));
        // The laptop sees the same mass deletion coming from the server and asks as well.
        const synctest::SyncReport onLaptopHeld = laptop.sync();
        QVERIFY(onLaptopHeld.needsConfirmation);
        QCOMPARE(onLaptopHeld.pendingDeletions.size(), kArchiveNotes);
        QVERIFY(laptop.vault->exists(archiveNote(0)));
        QCOMPARE(synctest::counts(laptop.sync(confirm)), (Counts{.delL = kArchiveNotes}));
        QCOMPARE(laptop.files(), phone.files());
        QCOMPARE(laptop.trash().size(), kArchiveNotes);
        QVERIFY(!onLaptop.index.note(archiveNote(0)));
        for (const TagCount &tag : onLaptop.index.tags())
            QVERIFY(tag.tag != u"archiwum"_s);
        QCOMPARE(synctest::counts(laptop.sync()), Counts{});
        QCOMPARE(synctest::counts(phone.sync()), Counts{});
    }

    void remindersFollowSyncedNotes()
    {
        synctest::TestClock clock;
        FakeWebDavServer server(synctest::serverOptions(clock));
        synctest::DeviceOptions options;
        options.clock = &clock;
        Device laptop(server, u"laptop"_s, sampleVault(), options);
        Device phone(server, u"telefon"_s, {}, options);
        laptop.sync();
        phone.sync();
        Indexer onPhone(*phone.vault);

        // The phone shows reminders in the app (no background alarms).
        QDateTime wall = now();
        InAppSink sink;
        ReminderScheduler scheduler(&onPhone.index, &sink, phone.vault->store(), [&wall] { return wall; });
        scheduler.setDebounceInterval(0);
        scheduler.setHeartbeatInterval(3'600'000);
        scheduler.start();
        QVERIFY(!sink.scheduled.isEmpty());
        QCOMPARE(sink.scheduled.last().size(), 10);

        // A reminder written on the laptop arrives through sync and rings on the phone.
        clock.advance(60'000);
        QVERIFY(laptop.write(kJournal14, laptop.read(kJournal14) + u"- [ ] Odebrać paczkę ⏰ 2026-10-14 09:30\n"_s));
        laptop.sync();
        phone.sync();
        QTRY_VERIFY(hasTitle(scheduler.upcoming(), u"Odebrać paczkę"_s));
        QVERIFY(hasTitle(sink.scheduled.last(), u"Odebrać paczkę"_s));
        wall = QDateTime(QDate(2026, 10, 14), QTime(9, 31));
        scheduler.checkNow();
        QCOMPARE(sink.shown, QStringList{u"Odebrać paczkę"_s});

        // The laptop completes the 12:30 call: it never rings on the phone.
        const QString journal = laptop.read(kJournal14);
        QVERIFY(laptop.write(kJournal14, markdown::toggleTask(journal, 2, today())));
        laptop.sync();
        phone.sync();
        QTRY_VERIFY(!hasTitle(scheduler.upcoming(), u"Zadzwonić do mamy #rodzina"_s));
        wall = QDateTime(QDate(2026, 10, 14), QTime(12, 31));
        scheduler.checkNow();
        QCOMPARE(sink.shown, QStringList{u"Odebrać paczkę"_s});
        wall = QDateTime(QDate(2026, 10, 14), QTime(18, 0, 30));
        scheduler.checkNow();
        QCOMPARE(sink.shown, (QStringList{u"Odebrać paczkę"_s, u"Podlać kwiaty"_s}));
        QVERIFY(scheduler.missed().isEmpty());
        scheduler.stop();
    }

    void anUnmountedVaultNeitherDeletesNorSyncs()
    {
        synctest::TestClock clock;
        FakeWebDavServer server(synctest::serverOptions(clock));
        synctest::DeviceOptions options;
        options.clock = &clock;
        Device phone(server, u"telefon"_s, sampleVault(), options);
        Indexer indexer(*phone.vault);
        phone.sync();
        const qsizetype notes = indexer.index.paths().size();
        // The SD card holding the vault is removed: the folder is gone.
        QVERIFY(QDir().rename(phone.root, phone.root + u"-karta"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"vault folder is missing"_s));
        QVERIFY(phone.vault->rescan().isEmpty());
        QCOMPARE(phone.vault->scanStatus().state, VaultScanStatus::State::RootMissing);
        QCOMPARE(indexer.index.paths().size(), notes);
        const synctest::SyncReport refused = phone.sync();
        QVERIFY(refused.syncError.is(synctest::SyncErrorCode::VaultUnavailable));
        QCOMPARE(server.files(synctest::kFolder).size(), sampleVault().size());
        // Back again: nothing to do.
        QVERIFY(QDir().rename(phone.root + u"-karta"_s, phone.root));
        QVERIFY(phone.vault->rescan().isEmpty());
        QVERIFY(phone.vault->scanStatus().isOk());
        QCOMPARE(synctest::counts(phone.sync()), Counts{});
    }
};

QTEST_MAIN(TestIntegration)
#include "tst_integration.moc"
