// Cross-module consistency of the syntax rules (docs/SYNTAX.md §2-§6). Several modules recognise
// tags, links, tasks and reminders on their own paths: the parser (markdown::parseNote), the
// rich-text renderer (markdown::InlineRenderer), the block editor model (BlockDocument), the
// index (VaultIndex), the exporters (HTML site, tasks CSV, Logseq round trip) and the reminders
// module (notifications, calendar, snooze). For every row the expected result is written down
// from SYNTAX.md and every module must agree with it.
#include "index/index.h"
#include "io/io.h"
#include "markdown/markdown.h"
#include "reminders/reminders.h"
#include "sync/syncengine.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "centraleuropeantime.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

const QString kPath = u"Test.md"_s;

QStringList sortedUnique(QStringList list)
{
    list.sort();
    list.removeDuplicates();
    return list;
}

QStringList lowered(const QStringList &list)
{
    QStringList out;
    for (const QString &s : list)
        out.append(s.toLower());
    return out;
}

QString unescapeHtml(QString s)
{
    s.replace(u"&quot;"_s, u"\""_s);
    s.replace(u"&lt;"_s, u"<"_s);
    s.replace(u"&gt;"_s, u">"_s);
    s.replace(u"&#39;"_s, u"'"_s);
    s.replace(u"&amp;"_s, u"&"_s);
    return s;
}

/// Rich text of every block of the note, as the block editor shows it.
QStringList renderedBlocks(const QString &content)
{
    const markdown::BlockDocument doc(content);
    const markdown::InlineRenderer renderer;
    QStringList out;
    for (int i = 0; i < doc.count(); ++i)
        out.append(renderer.renderBlock(doc.at(i).body, doc.at(i).kind));
    return out;
}

/// Decoded targets of the renderer's links with `scheme` ("tag", "page", "block", "ext", …).
QStringList renderedHrefs(const QString &content, const QString &scheme)
{
    static const QRegularExpression href(u"href=\"([^\"]*)\""_s);
    QStringList out;
    for (const QString &html : renderedBlocks(content)) {
        for (auto it = href.globalMatch(html); it.hasNext();) {
            const QString value = unescapeHtml(it.next().captured(1));
            if (value.startsWith(scheme + u':'))
                out.append(QUrl::fromPercentEncoding(value.sliced(scheme.size() + 1).toUtf8()));
        }
    }
    return out;
}

/// The page of `content` in an html-site export of a one-note vault.
QString exportedHtml(const QString &content)
{
    io::ExportInput input;
    input.files = {io::FileEntry{kPath, content.toUtf8()}};
    input.vaultName = u"Test"_s;
    input.now = QDateTime(QDate(2026, 10, 14), QTime(8, 0));
    const io::ExportResult result = io::runExport(*io::exporterById(u"html-site"), input);
    for (const io::FileEntry &entry : io::readZip(result.files.value(0).data)) {
        if (entry.path == u"Test/Test.html"_s)
            return QString::fromUtf8(entry.data);
    }
    return {};
}

/// The body of an exported page (navigation and backlinks left out).
QString exportedBody(const QString &content)
{
    const QString html = exportedHtml(content);
    const qsizetype from = html.indexOf(u"<article"_s);
    const qsizetype to = html.indexOf(u"</article>"_s);
    return from >= 0 && to > from ? html.mid(from, to - from) : html;
}

/// `content` exported with logseq-zip and imported back with the Logseq importer.
QString logseqRoundTrip(const QString &content)
{
    io::ExportInput input;
    input.files = {io::FileEntry{kPath, content.toUtf8()}};
    input.vaultName = u"Test"_s;
    input.now = QDateTime(QDate(2026, 10, 14), QTime(8, 0));
    const io::ExportResult exported = io::runExport(*io::exporterById(u"logseq-zip"), input);
    io::ImportOptions options;
    options.timeZone = QTimeZone("Europe/Warsaw");
    const io::ImportResult imported = io::runImport(
            *io::importerById(u"logseq"), {io::InputFile{u"test.zip"_s, exported.files.value(0).data}}, options);
    for (const io::ImportedFile &file : imported.files) {
        if (file.path == kPath)
            return QString::fromUtf8(file.data);
    }
    return u"<missing after the round trip>"_s;
}

QString statusName(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

QStringList statuses(const ParsedNote &note)
{
    QStringList out;
    for (const Task &task : note.tasks)
        out.append(statusName(task.status));
    return out;
}

QString reminderString(const Reminder &r)
{
    return r.at.toString() + (r.alarm ? u"!"_s : QString());
}

QStringList reminders(const ParsedNote &note)
{
    QStringList out;
    for (const Reminder &r : note.reminders)
        out.append(reminderString(r));
    return out;
}

QString linkString(const Link &link)
{
    QString kind;
    switch (link.kind) {
    case LinkKind::Wiki: kind = u"wiki"_s; break;
    case LinkKind::Embed: kind = u"embed"_s; break;
    case LinkKind::BlockRef: kind = u"blockref"_s; break;
    case LinkKind::BlockEmbed: kind = u"blockembed"_s; break;
    case LinkKind::Markdown: kind = u"markdown"_s; break;
    case LinkKind::Url: kind = u"url"_s; break;
    }
    QString out = kind + u':' + link.target;
    if (!link.heading.isEmpty())
        out += u'#' + link.heading;
    if (!link.blockId.isEmpty() && link.kind != LinkKind::BlockRef && link.kind != LinkKind::BlockEmbed)
        out += u"#^"_s + link.blockId;
    return out;
}

VaultIndex &indexOf(const QString &content)
{
    static VaultIndex index;
    index.clear();
    index.update(kPath, content);
    return index;
}

} // namespace

class TestIntegrationConsistency : public QObject {
    Q_OBJECT

public:
    static void initMain() { brulion::test::useCentralEuropeanTime(); }

private Q_SLOTS:
    // ---- tags (SYNTAX §3) ----------------------------------------------------------------------

    void tags_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected"); // lower-case, without '#'
        const auto row = [](const char *name, const QString &content, const QStringList &tags) {
            QTest::newRow(name) << content << tags;
        };
        row("forms", u"- #tag i #nested/tag oraz #[[tag ze spacją]]\n"_s,
            {u"tag"_s, u"nested/tag"_s, u"tag ze spacją"_s});
        row("digits", u"- #1 nie, #2026 nie, ale #1a i #a1 tak\n"_s, {u"1a"_s, u"a1"_s});
        row("preceding characters", u"- a#nie (#tak) x,#nie [#nie] „#nie\n"_s, {u"tak"_s});
        row("no-break space", u"- zdanie\u00A0#tag\n"_s, {u"tag"_s});
        row("code span", u"- `#kod` i #po_kodzie\n"_s, {u"po_kodzie"_s});
        row("urls and link targets", u"- https://example.com/#kotwica i [[Strona#Nagłówek]] #po\n"_s, {u"po"_s});
        row("markdown link", u"- [tekst](https://x.pl/#frag) #po-linku\n"_s, {u"po-linku"_s});
        row("case", u"- #Zażółć i #zażółć oraz #ZAŻÓŁĆ\n"_s, {u"zażółć"_s});
        row("trailing slash", u"- #koniec/ i #a/b/\n"_s, {u"koniec"_s, u"a/b"_s});
        row("trailing punctuation", u"- #tag. #drugi, #trzeci!\n"_s, {u"tag"_s, u"drugi"_s, u"trzeci"_s});
        row("comment", u"- %%#ukryty%% #widoczny\n"_s, {u"widoczny"_s});
        row("heading", u"# Nagłówek #w-nagłówku\n"_s, {u"w-nagłówku"_s});
        row("emphasis is no preceder", u"- **#pogrubiony** i *#kursywa*\n"_s, {});
        row("no letter or digit", u"- #- #_ #/ #-- #_/\n"_s, {});
        row("only non-ASCII digits", u"- #٣ i #１２\n"_s, {});
        row("unclosed multi-word", u"- #[[bez zamknięcia\n"_s, {});
        row("multi-word with a pipe", u"- #[[a|b]]\n"_s, {});
        row("inside a wiki link", u"- [[Strona #nie]]\n"_s, {});
        row("inside link labels", u"- [tekst #nie](https://x.pl) i [[Strona|alias #nie]] ![obraz #nie](a.png)\n"_s, {});
        row("continuation line", u"- pierwsza linia\n  #w-kontynuacji\n"_s, {u"w-kontynuacji"_s});
        row("task with tokens", u"- [ ] zadanie #praca 📅 2026-10-15\n"_s, {u"praca"_s});
        row("url fragment", u"- https://x.pl/?a=1#b <https://x.pl/#c> #d\n"_s, {u"d"_s});
        row("emoji", u"- #🙂 #tag🙂\n"_s, {u"tag"_s});
        row("paragraph and quote", u"Akapit #akapit\n\n> cytat #cytat\n"_s, {u"akapit"_s, u"cytat"_s});
    }

    void tags()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        const QStringList want = sortedUnique(expected);
        const ParsedNote note = markdown::parseNote(kPath, content);
        QCOMPARE(sortedUnique(note.tags), want);

        // Rendered rich text (block editor).
        QCOMPARE(sortedUnique(lowered(renderedHrefs(content, u"tag"_s))), want);

        // Index.
        QStringList indexed;
        for (const TagCount &tag : indexOf(content).tags())
            indexed.append(tag.tag);
        QCOMPARE(sortedUnique(indexed), want);

        // HTML export.
        static const QRegularExpression tagSpan(u"class=\"tag\"[^>]*>#([^<]*)<"_s);
        QStringList exported;
        for (auto it = tagSpan.globalMatch(exportedBody(content)); it.hasNext();)
            exported.append(unescapeHtml(it.next().captured(1)).toLower());
        QCOMPARE(sortedUnique(exported), want);

        // Logseq export + import.
        QCOMPARE(sortedUnique(markdown::parseNote(kPath, logseqRoundTrip(content)).tags), want);
    }

    // ---- links (SYNTAX §2) ---------------------------------------------------------------------

    void links_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected");
        const auto row = [](const char *name, const QString &content, const QStringList &links) {
            QTest::newRow(name) << content << links;
        };
        row("wiki", u"- [[Strona]] i [[Strona|Alias]]\n"_s, {u"wiki:Strona"_s, u"wiki:Strona"_s});
        row("fragments", u"- [[Strona#Nagłówek]] [[Strona#^blok1]] [[#^blok2]]\n"_s,
            {u"wiki:Strona#Nagłówek"_s, u"wiki:Strona#^blok1"_s, u"wiki:#^blok2"_s});
        row("embeds", u"- ![[obraz.png]] ![[Strona#Sekcja]]\n"_s, {u"embed:obraz.png"_s, u"embed:Strona#Sekcja"_s});
        row("block references", u"- ((abc123)) i !((def456))\n"_s, {u"blockref:abc123"_s, u"blockembed:def456"_s});
        row("markdown", u"- [tekst](Folder/Strona.md) i [www](https://example.com)\n"_s,
            {u"markdown:Folder/Strona.md"_s, u"url:https://example.com"_s});
        row("autolinks", u"- <https://a.pl> i https://b.pl/x?y=1.\n"_s,
            {u"url:https://a.pl"_s, u"url:https://b.pl/x?y=1"_s});
        row("code span", u"- `[[nie link]]` i [[tak]]\n"_s, {u"wiki:tak"_s});
        row("comment", u"- %%[[ukryty]]%% [[widoczny]]\n"_s, {u"wiki:widoczny"_s});
        row("polish and folders", u"- [[Zażółć gęślą]] i [[Projekty/Dom]]\n"_s,
            {u"wiki:Zażółć gęślą"_s, u"wiki:Projekty/Dom"_s});
        row("empty", u"- [[a]] [[]] [[ ]]\n"_s, {u"wiki:a"_s});
        row("tag form is no link", u"- #[[tag]] i [[link]]\n"_s, {u"wiki:link"_s});
    }

    void links()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        const ParsedNote note = markdown::parseNote(kPath, content);
        QStringList parsed;
        for (const Link &link : note.links)
            parsed.append(linkString(link));
        // Markdown links to URLs are reported as Markdown links with the URL as target.
        QStringList want = expected;
        for (QString &w : want) {
            if (w.startsWith(u"url:"_s) && !content.contains(u'<' + w.sliced(4))
                && content.contains(u"](" + w.sliced(4)))
                w = u"markdown:"_s + w.sliced(4);
        }
        QCOMPARE(parsed, want);

        // The renderer links exactly those: page: for wiki links, embeds and Markdown links to
        // files, block: for block references, ext: for URLs.
        QStringList rendered;
        for (const QString &target : renderedHrefs(content, u"page"_s))
            rendered.append(u"page:"_s + target);
        for (const QString &target : renderedHrefs(content, u"block"_s))
            rendered.append(u"block:"_s + target);
        for (const QString &target : renderedHrefs(content, u"ext"_s))
            rendered.append(u"ext:"_s + target);
        QStringList fromParser;
        for (const Link &link : note.links) {
            QString fragment = link.heading.isEmpty() ? QString() : u'#' + link.heading;
            if (!link.blockId.isEmpty())
                fragment = u"#^"_s + link.blockId;
            switch (link.kind) {
            case LinkKind::Wiki:
            case LinkKind::Embed: fromParser.append(u"page:"_s + link.target + fragment); break;
            case LinkKind::BlockRef:
            case LinkKind::BlockEmbed: fromParser.append(u"block:"_s + link.target); break;
            case LinkKind::Markdown:
            case LinkKind::Url:
                fromParser.append(link.target.contains(u"://"_s) ? QString(u"ext:"_s + link.target)
                                                                 : QString(u"page:"_s + link.target + fragment));
                break;
            }
        }
        QCOMPARE(sortedUnique(rendered), sortedUnique(fromParser));

        // The index resolves the same path-based links and block references (URLs excluded).
        const qsizetype urls = std::count_if(note.links.cbegin(), note.links.cend(), [](const Link &l) {
            return l.kind == LinkKind::Url || l.target.contains(u"://"_s);
        });
        QCOMPARE(indexOf(content).outgoingLinks(kPath).size(), note.links.size() - urls);
    }

    // ---- tasks (SYNTAX §5) ---------------------------------------------------------------------

    void tasks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected");
        const auto row = [](const char *name, const QString &content, const QStringList &statuses) {
            QTest::newRow(name) << content << statuses;
        };
        row("checkboxes", u"- [ ] a\n- [x] b\n- [X] c\n- [/] d\n- [-] e\n- [>] f\n"_s,
            {u"todo"_s, u"done"_s, u"done"_s, u"doing"_s, u"cancelled"_s, u"deferred"_s});
        row("logseq markers",
            u"- TODO g\n- LATER h\n- NOW i\n- DOING j\n- DONE k\n- CANCELED l\n- CANCELLED m\n- WAITING n\n"_s,
            {u"todo"_s, u"todo"_s, u"doing"_s, u"doing"_s, u"done"_s, u"cancelled"_s, u"cancelled"_s, u"deferred"_s});
        row("list markers", u"* [ ] gwiazdka\n+ [ ] plus\n1. [ ] numer\n2) [x] nawias\n"_s,
            {u"todo"_s, u"todo"_s, u"todo"_s, u"done"_s});
        row("nested", u"- [ ] zadanie\n\t- [x] podzadanie\n\t\t- DOING głębiej\n"_s,
            {u"todo"_s, u"done"_s, u"doing"_s});
        row("custom status counts as todo", u"- [?] pytanie\n- [!] ważne\n"_s, {u"todo"_s, u"todo"_s});
        row("not tasks", u"- todo małymi\n- [ ]bez spacji\n- TODOS nie\n- [] puste\n"_s, {});
        row("paragraph, heading, quote", u"[ ] poza listą\n\nTODO też nie\n\n# TODO nagłówek\n\n> [x] cytat\n"_s, {});
        row("tokens and priority",
            u"- [ ] zadanie 📅 2026-10-15 ⏫ #praca\n- TODO [#A] pilne\n- [ ] Zażółć 🔁 co tydzień 📅 2026-10-14\n"_s,
            {u"todo"_s, u"todo"_s, u"todo"_s});
        row("planning lines", u"- TODO spotkanie\n  SCHEDULED: <2026-10-15 Thu 10:00>\n"_s, {u"todo"_s});
    }

    void tasks()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        const ParsedNote note = markdown::parseNote(kPath, content);
        QCOMPARE(statuses(note), expected);

        // Block editor model.
        const markdown::BlockDocument doc(content);
        QStringList fromDoc;
        for (int i = 0; i < doc.count(); ++i) {
            if (doc.at(i).taskStatus)
                fromDoc.append(statusName(*doc.at(i).taskStatus));
        }
        QCOMPARE(fromDoc, expected);

        // Rendered checkboxes.
        QCOMPARE(renderedHrefs(content, u"task"_s).size(), expected.size());

        // Index and query.
        QStringList indexed;
        for (const TaskRef &ref : indexOf(content).tasks())
            indexed.append(statusName(ref.task.status));
        QCOMPARE(indexed, expected);
        const auto query = parseQuery(u"type:tasks"_s);
        QCOMPARE(runQuery(std::get<Query>(query), indexOf(content), QDate(2026, 10, 14)).total, int(expected.size()));

        // HTML export: one task item per task, with the status as class.
        static const QRegularExpression taskItem(u"class=\"task (\\w+)\""_s);
        QStringList exported;
        for (auto it = taskItem.globalMatch(exportedBody(content)); it.hasNext();)
            exported.append(it.next().captured(1));
        QCOMPARE(exported, expected);

        // Tasks CSV: one row per task (plus the header), status in the 4th column.
        io::ExportInput input;
        input.files = {io::FileEntry{kPath, content.toUtf8()}};
        input.now = QDateTime(QDate(2026, 10, 14), QTime(8, 0));
        const QString csv =
                QString::fromUtf8(io::runExport(*io::exporterById(u"tasks-csv"), input).files.value(0).data);
        const QList<QStringList> rows = io::parseCsv(csv);
        QStringList csvStatuses;
        for (qsizetype i = 1; i < rows.size(); ++i) {
            if (rows[i].size() > 3)
                csvStatuses.append(rows[i][3].toLower());
        }
        QCOMPARE(csvStatuses, expected);

        // Logseq export + import keeps every status.
        QCOMPARE(statuses(markdown::parseNote(kPath, logseqRoundTrip(content))), expected);

        // Toggling through the pure edit and through the block editor gives the same text.
        for (const Task &task : note.tasks) {
            const QString viaEdit = markdown::toggleTask(content, task.line, QDate(2026, 10, 14));
            markdown::BlockDocument editor(content);
            const int block = editor.indexAtLine(task.line);
            editor.toggleTask(block, QDate(2026, 10, 14), task.line - editor.at(block).line);
            QCOMPARE(editor.toString(), viaEdit);
        }
    }

    // ---- reminders (SYNTAX §6) -----------------------------------------------------------------

    void reminders_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected"); // "yyyy-MM-dd HH:mm" + "!" for alarms
        QTest::addColumn<int>("firing");           // of those: notifications on 14-16 October
        const auto row = [](const char *name, const QString &content, const QStringList &at, int firing) {
            QTest::newRow(name) << content << at << firing;
        };
        row("time", u"- ⏰ 2026-10-15 09:30\n"_s, {u"2026-10-15 09:30"_s}, 1);
        row("default time", u"- ⏰ 2026-10-15\n"_s, {u"2026-10-15 09:00"_s}, 1);
        row("alarm", u"- ⏰! 2026-10-15 06:30\n"_s, {u"2026-10-15 06:30!"_s}, 1);
        row("obsidian-reminder", u"- Spotkanie (@2026-10-15 10:00)\n"_s, {u"2026-10-15 10:00"_s}, 1);
        row("variation selector", u"- ⏰️ 2026-10-15 09:00\n"_s, {u"2026-10-15 09:00"_s}, 1);
        row("code span", u"- `⏰ 2026-10-15 09:00`\n"_s, {}, 0);
        row("comment", u"- %%⏰ 2026-10-15 09:00%%\n"_s, {}, 0);
        row("invalid date", u"- ⏰ 2026-13-45 09:00\n"_s, {}, 0);
        row("two in a block", u"- dwa ⏰ 2026-10-15 08:00 i ⏰! 2026-10-16 07:00\n"_s,
            {u"2026-10-15 08:00"_s, u"2026-10-16 07:00!"_s}, 2);
        row("continuation line", u"- linia\n  druga ⏰ 2026-10-15 11:00\n"_s, {u"2026-10-15 11:00"_s}, 1);
        row("paragraph", u"Akapit ⏰ 2026-10-15 12:00\n"_s, {u"2026-10-15 12:00"_s}, 1);
        row("done task", u"- [x] zrobione ⏰ 2026-10-15 09:00 ✅ 2026-10-14\n"_s, {u"2026-10-15 09:00"_s}, 0);
        row("cancelled task", u"- [-] odwołane ⏰ 2026-10-15 09:00\n"_s, {u"2026-10-15 09:00"_s}, 0);
        row("recurring", u"- [ ] leki ⏰ 2026-10-14 08:30 🔁 every day\n"_s, {u"2026-10-14 08:30"_s}, 3);
        row("polish text around", u"- Zażółć gęślą jaźń ⏰ 2026-10-16 18:15 #dom\n"_s, {u"2026-10-16 18:15"_s}, 1);
    }

    void reminders()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        QFETCH(int, firing);
        const ParsedNote note = markdown::parseNote(kPath, content);
        QCOMPARE(::reminders(note), expected);

        // Rendered chips: one per reminder, each covering its token.
        static const QRegularExpression range(u"^(\\d+)-(\\d+)$"_s);
        const markdown::BlockDocument doc(content);
        const markdown::InlineRenderer renderer;
        QStringList chips;
        for (int i = 0; i < doc.count(); ++i) {
            const QString body = doc.at(i).body;
            static const QRegularExpression href(u"href=\"reminder:([^\"]*)\""_s);
            for (auto it = href.globalMatch(renderer.renderBlock(body, doc.at(i).kind)); it.hasNext();) {
                const QRegularExpressionMatch m = range.match(it.next().captured(1));
                QVERIFY(m.hasMatch());
                const qsizetype from = m.captured(1).toLongLong();
                const qsizetype to = m.captured(2).toLongLong();
                QVERIFY(from >= 0 && to <= body.size() && from < to);
                chips.append(body.mid(from, to - from));
            }
        }
        QCOMPARE(chips.size(), expected.size());
        for (qsizetype i = 0; i < chips.size(); ++i)
            QVERIFY2(chips[i].contains(expected[i].left(10)), qPrintable(chips[i]));

        // Index, notifications and calendar.
        const VaultIndex &index = indexOf(content);
        QCOMPARE(index.reminders().size(), expected.size());
        const QList<ScheduledNotification> fired = notificationsBetween(
                index, QDateTime(QDate(2026, 10, 14), QTime(0, 0)), QDateTime(QDate(2026, 10, 16), QTime(23, 59)));
        QCOMPARE(fired.size(), firing);
        const QList<CalendarItem> items = calendarItems(index, QDate(2026, 10, 14), QDate(2026, 10, 16));
        const qsizetype calendarReminders = std::count_if(items.cbegin(), items.cend(), [](const CalendarItem &item) {
            return item.kind == CalendarItem::Reminder;
        });
        QCOMPARE(calendarReminders, qMax<qsizetype>(expected.size(), firing));

        // HTML export: a chip per reminder.
        static const QRegularExpression chip(u"class=\"chip (reminder|alarm)\""_s);
        int exportedChips = 0;
        for (auto it = chip.globalMatch(exportedBody(content)); it.hasNext(); it.next())
            ++exportedChips;
        QCOMPARE(exportedChips, int(expected.size()));

        // Logseq export + import.
        QCOMPARE(::reminders(markdown::parseNote(kPath, logseqRoundTrip(content))), expected);

        // Snoozing moves exactly the first reminder of its line.
        if (!note.reminders.isEmpty()) {
            const Reminder &first = note.reminders.first();
            const QDateTime later(QDate(2026, 10, 20), QTime(21, 45));
            const ParsedNote snoozed = markdown::parseNote(kPath, snoozeReminder(content, first.line, later));
            QStringList want = expected;
            if (first.recurrence.isEmpty())
                want[0] = u"2026-10-20 21:45"_s + (first.alarm ? u"!"_s : QString());
            else
                want.append(u"2026-10-20 21:45"_s + (first.alarm ? u"!"_s : QString()));
            QCOMPARE(sortedUnique(::reminders(snoozed)), sortedUnique(want));
        }
    }

    // ---- block ids (SYNTAX §4) -----------------------------------------------------------------

    void blockIds_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected");
        const auto row = [](const char *name, const QString &content, const QStringList &ids) {
            QTest::newRow(name) << content << ids;
        };
        row("list item", u"- tekst ^abc-123\n"_s, {u"abc-123"_s});
        row("paragraph last line", u"Akapit\ndruga linia ^id2\n"_s, {u"id2"_s});
        row("logseq property", u"- zadanie\n  id:: 6512a2b4-1c3d-4e5f-8a9b-0c1d2e3f4a5b\n"_s,
            {u"6512a2b4-1c3d-4e5f-8a9b-0c1d2e3f4a5b"_s});
        row("heading", u"# Nagłówek ^h1\n"_s, {u"h1"_s});
        row("nested", u"- pierwszy ^a1\n\t- drugi ^b2\n"_s, {u"a1"_s, u"b2"_s});
        row("quote", u"> cytat ^q1\n"_s, {u"q1"_s});
        row("after a code block", u"```\nkod\n```\n^c1\n"_s, {u"c1"_s});
        row("after a table", u"| a | b |\n| --- | --- |\n| 1 | 2 |\n^t1\n"_s, {u"t1"_s});
        row("multi-line quote", u"> pierwsza\n> druga ^q2\n"_s, {u"q2"_s});
        row("in code", u"- `kod ^nie`\n"_s, {});
        row("invalid characters", u"- zły ^ab_c\n- też ^żółw\n"_s, {});
    }

    void blockIds()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        const ParsedNote note = markdown::parseNote(kPath, content);
        QStringList parsed;
        for (const Block &b : note.blocks) {
            if (!b.id.isEmpty())
                parsed.append(b.id);
        }
        QCOMPARE(parsed, expected);

        const markdown::BlockDocument doc(content);
        QStringList fromDoc;
        for (int i = 0; i < doc.count(); ++i) {
            if (!doc.at(i).blockId.isEmpty())
                fromDoc.append(doc.at(i).blockId);
        }
        QCOMPARE(fromDoc, expected);

        const VaultIndex &index = indexOf(content);
        const QString html = exportedBody(content);
        for (const QString &id : expected) {
            QCOMPARE(index.blockById(id).value_or(BlockRef{}).path, kPath);
            QVERIFY2(html.contains(u"id=\"block-"_s + id + u'"'), qPrintable(id));
            // An existing id is returned unchanged, nothing is written.
            const int line = note.blocks[note.blockIds.value(id)].line;
            const markdown::EnsuredBlockId ensured = markdown::ensureBlockId(content, line);
            QCOMPARE(ensured.id, id);
            QCOMPARE(ensured.content, content);
        }
        // Logseq keeps one id per block (UUIDs for ours, so references survive).
        const ParsedNote back = markdown::parseNote(kPath, logseqRoundTrip(content));

        QCOMPARE(back.blockIds.size(), note.blockIds.size());

        // Ids added by the block editor are found by the parser and the index at that block.
        markdown::BlockDocument editor(content);
        for (int i = 0; i < editor.count(); ++i) {
            const markdown::BlockIdResult added = editor.ensureBlockId(i);
            if (added.id.isEmpty())
                continue;
            const QString text = editor.toString();
            const ParsedNote reparsed = markdown::parseNote(kPath, text);
            QVERIFY2(reparsed.blockIds.contains(added.id), qPrintable(text));
            QCOMPARE(reparsed.blocks[reparsed.blockIds.value(added.id)].line, editor.at(i).line);
            QCOMPARE(indexOf(text).blockById(added.id).value_or(BlockRef{}).path, kPath);
        }
    }

    // ---- task metadata (SYNTAX §5, §7) --------------------------------------------------------

    void taskMetadata_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("expected"); // due|scheduled|start|created|done|priority|recurrence
        QTest::newRow("all emoji tokens")
                << u"- [ ] a 📅 2026-10-15 ⏳ 2026-10-14 🛫 2026-10-13 ➕ 2026-10-01 ⏫ 🔁 every week\n"_s
                << u"2026-10-15|2026-10-14|2026-10-13|2026-10-01||high|every week"_s;
        QTest::newRow("done with time") << u"- [x] b ✅ 2026-10-14 📅 2026-10-15 18:00\n"_s
                                        << u"2026-10-15 18:00||||2026-10-14|none|"_s;
        QTest::newRow("logseq planning")
                << u"- TODO c\n  SCHEDULED: <2026-10-15 Thu 10:00 .+1w>\n  DEADLINE: <2026-10-20 Tue>\n"_s
                << u"2026-10-20|2026-10-15 10:00||||none|every week when done"_s;
        QTest::newRow("polish rule") << u"- [ ] d 🔁 co 2 tygodnie 📅 2026-10-15 🔽\n"_s
                                     << u"2026-10-15|||||low|every 2 weeks"_s;
        QTest::newRow("logseq priority") << u"- TODO [#A] e 📅 2026-10-15\n"_s << u"2026-10-15|||||high|"_s;
        QTest::newRow("highest and lowest") << u"- [ ] f 🔺\n- [ ] g ⏬\n"_s << u"|||||highest|"_s;
        QTest::newRow("tokens in code") << u"- [ ] h `📅 2026-10-15` ⏳ 2026-10-16\n"_s << u"|2026-10-16||||none|"_s;
        QTest::newRow("monthly on a day") << u"- [ ] czynsz 📅 2026-10-10 🔁 co miesiąc 10.\n"_s
                                          << u"2026-10-10|||||none|every month on the 10th"_s;
    }

    void taskMetadata()
    {
        QFETCH(QString, content);
        QFETCH(QString, expected);
        const auto priorityName = [](Priority p) {
            switch (p) {
            case Priority::None: return u"none"_s;
            case Priority::Lowest: return u"lowest"_s;
            case Priority::Low: return u"low"_s;
            case Priority::Medium: return u"medium"_s;
            case Priority::High: return u"high"_s;
            case Priority::Highest: return u"highest"_s;
            }
            return QString();
        };
        const auto signature = [&](const Task &t) {
            const auto spec = [](const std::optional<DateSpec> &d) { return d ? d->toString() : QString(); };
            const auto date = [](const std::optional<QDate> &d) { return d ? d->toString(Qt::ISODate) : QString(); };
            return QStringList{spec(t.due),      spec(t.scheduled),        spec(t.start), date(t.created),
                               date(t.doneDate), priorityName(t.priority), t.recurrence}
                    .join(u'|');
        };
        const ParsedNote note = markdown::parseNote(kPath, content);
        QVERIFY(!note.tasks.isEmpty());
        const Task &task = note.tasks.first();
        QCOMPARE(signature(task), expected);

        // Tasks CSV columns: Due, Scheduled, Start, Created, Done, ..., Priority, Recurrence.
        io::ExportInput input;
        input.files = {io::FileEntry{kPath, content.toUtf8()}};
        input.now = QDateTime(QDate(2026, 10, 14), QTime(8, 0));
        const QList<QStringList> rows = io::parseCsv(
                QString::fromUtf8(io::runExport(*io::exporterById(u"tasks-csv"), input).files.value(0).data));
        QVERIFY(rows.size() >= 2);
        const QStringList &csv = rows[1];
        QCOMPARE(QStringList({csv.value(5), csv.value(6), csv.value(7), csv.value(8), csv.value(9),
                              csv.value(11).isEmpty() ? u"none"_s : csv.value(11).toLower(), csv.value(12)})
                         .join(u'|'),
                 expected);

        // HTML export chips.
        const QString html = exportedBody(content);
        if (task.due)
            QVERIFY2(html.contains(u"📅 "_s + task.due->toString()), qPrintable(html));
        if (task.scheduled)
            QVERIFY(html.contains(u"⏳ "_s + task.scheduled->toString()));
        if (!task.recurrence.isEmpty())
            QVERIFY(html.contains(u"🔁 "_s + task.recurrence));
        if (task.priority != Priority::None)
            QVERIFY(html.contains(u"priority-"_s + priorityName(task.priority)));

        // Logseq export + import.
        const ParsedNote back = markdown::parseNote(kPath, logseqRoundTrip(content));
        QVERIFY(!back.tasks.isEmpty());
        QCOMPARE(signature(back.tasks.first()), expected);

        // Calendar: the written dates of the task.
        const QList<CalendarItem> items = calendarItems(indexOf(content), QDate(2026, 9, 1), QDate(2026, 10, 31));
        const auto has = [&](CalendarItem::Kind kind, const std::optional<DateSpec> &spec) {
            return !spec || std::any_of(items.cbegin(), items.cend(), [&](const CalendarItem &item) {
                return item.kind == kind && item.date == spec->date && item.time == spec->time;
            });
        };
        QVERIFY(has(CalendarItem::Due, task.due));
        QVERIFY(has(CalendarItem::Scheduled, task.scheduled));
        QVERIFY(has(CalendarItem::Start, task.start));

        // Completing a recurring task writes the next occurrence where the calendar expects it.
        if (!task.recurrence.isEmpty() && task.status != TaskStatus::Done && task.due) {
            const QDate today(2026, 10, 14);
            const QString done = markdown::setTaskStatus(content, task.line, TaskStatus::Done, today);
            const ParsedNote after = markdown::parseNote(kPath, done);
            QCOMPARE(after.tasks.size(), 2);
            const Task &next = after.tasks.first();
            QCOMPARE(next.status, TaskStatus::Todo);
            if (!task.recurrence.endsWith(u"when done"_s)) {
                QDate expectedNext;
                for (const CalendarItem &item :
                     calendarItems(indexOf(content), task.due->date.addDays(1), task.due->date.addYears(1))) {
                    if (item.kind == CalendarItem::Due) {
                        expectedNext = item.date;
                        break;
                    }
                }
                QCOMPARE(next.due->date, expectedNext);
            }
        }
    }

    // ---- renaming pages (SYNTAX §2) ------------------------------------------------------------

    void renames_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("oldPath");
        QTest::addColumn<QString>("newPath");
        QTest::addColumn<int>("rewritten");
        QTest::addColumn<QStringList>("others"); // other pages of the vault
        QTest::newRow("every link form")
                << u"- [[Stara]] [[Stara|alias]] [[Stara#Nagłówek]] ![[Stara#^id]] [x](Projekty/Stara.md) [[Projekty/Stara]]\n"_s
                << u"Projekty/Stara.md"_s << u"Archiwum/Nowa nazwa.md"_s << 6 << QStringList{u"Starania.md"_s};
        QTest::newRow("polish and case") << u"- [[żółć]] i [[Żółć|kolor]]\n"_s << u"Żółć.md"_s << u"Zielona żółć.md"_s
                                         << 2 << QStringList{u"Żółw.md"_s};
        QTest::newRow("code and comments stay")
                << u"- `[[Stara]]` %%[[Stara]]%% [[Stara]]\n"_s << u"Stara.md"_s << u"Nowa.md"_s << 1 << QStringList{};
        QTest::newRow("other pages stay") << u"- [[Starania]] [[Stara]] [[Inna/Stara]]\n"_s << u"Stara.md"_s
                                          << u"Nowa.md"_s << 1 << QStringList{u"Starania.md"_s, u"Inna/Stara.md"_s};
    }

    void renames()
    {
        QFETCH(QString, content);
        QFETCH(QString, oldPath);
        QFETCH(QString, newPath);
        QFETCH(int, rewritten);
        QFETCH(QStringList, others);
        VaultIndex index;
        index.update(kPath, content);
        index.update(oldPath, u"# stara strona\n"_s);
        for (const QString &other : std::as_const(others))
            index.update(other, u"- inna strona\n"_s);
        const auto countTo = [&index](const VaultPath &target) {
            int n = 0;
            for (const ResolvedLink &link : index.outgoingLinks(kPath))
                n += link.target == target ? 1 : 0;
            return n;
        };
        QCOMPARE(countTo(oldPath), rewritten);
        QCOMPARE(index.linksTo(oldPath), QList<VaultPath>{kPath});
        const int unrelated = int(index.outgoingLinks(kPath).size()) - rewritten;

        const QString renamed =
                markdown::renameLinks(content, paths::pageTitle(oldPath), paths::pageTitle(newPath), oldPath, newPath);
        index.rename(oldPath, newPath, u"# stara strona\n"_s);
        index.update(kPath, renamed);
        QCOMPARE(countTo(newPath), rewritten);
        QCOMPARE(countTo(oldPath), 0);
        QCOMPARE(int(index.outgoingLinks(kPath).size()) - rewritten, unrelated);
        QVERIFY(index.linksTo(oldPath).isEmpty());
        // Code and comments keep their bytes.
        if (content.contains(u'`'))
            QVERIFY(renamed.contains(u"`[[Stara]]`"_s));
        if (content.contains(u"%%"_s))
            QVERIFY(renamed.contains(u"%%[[Stara]]%%"_s));
    }

    // ---- journals and the vault config (SYNTAX §1) ---------------------------------------------

    void journals_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("path");
        QTest::newRow("default") << u"journals"_s << u"yyyy-MM-dd"_s << u"journals/2026-10-14.md"_s;
        QTest::newRow("polish folder and format") << u"Dziennik"_s << u"dd.MM.yyyy"_s << u"Dziennik/14.10.2026.md"_s;
        QTest::newRow("vault root") << QString() << u"yyyy_MM_dd"_s << u"2026_10_14.md"_s;
        QTest::newRow("nested folders") << u"journals"_s << u"yyyy/MM/dd"_s << u"journals/2026/10/14.md"_s;
    }

    void journals()
    {
        QFETCH(QString, folder);
        QFETCH(QString, format);
        QFETCH(QString, path);
        VaultConfig config;
        config.journalsFolder = folder;
        config.journalFileFormat = format;
        config.defaultReminderTime = QTime(7, 15);
        const QDate date(2026, 10, 14);
        QCOMPARE(markdown::journalPathForDate(date, config), path);
        QCOMPARE(markdown::journalDateFromPath(path, config), std::optional<QDate>(date));
        const QString content = u"- wpis ⏰ 2026-10-15\n"_s;
        QCOMPARE(markdown::parseNote(path, content, config).journalDate, std::optional<QDate>(date));

        VaultIndex index(config);
        index.update(path, content);
        QCOMPARE(index.journals(), (QList<JournalRef>{{date, path}}));
        // The vault's default reminder time reaches the notifications.
        const QList<ScheduledNotification> fired =
                notificationsBetween(index, QDateTime(date, QTime(0, 0)), QDateTime(date.addDays(2), QTime(0, 0)));
        QCOMPARE(fired.size(), 1);
        QCOMPARE(fired.first().at, QDateTime(QDate(2026, 10, 15), QTime(7, 15)));
        const QList<CalendarItem> items = calendarItems(index, date, date);
        QVERIFY(std::any_of(items.cbegin(), items.cend(), [&](const CalendarItem &item) {
            return item.kind == CalendarItem::Journal && item.path == path;
        }));

        // Logseq export + import with this config puts the journal back at the same path.
        io::ExportInput input;
        input.files = {io::FileEntry{path, content.toUtf8()}};
        input.config = config;
        input.vaultName = u"Test"_s;
        input.now = QDateTime(date, QTime(8, 0));
        const io::ExportResult exported = io::runExport(*io::exporterById(u"logseq-zip"), input);
        io::ImportOptions options;
        options.config = config;
        options.timeZone = QTimeZone("Europe/Warsaw");
        const io::ImportResult imported = io::runImport(
                *io::importerById(u"logseq"), {io::InputFile{u"test.zip"_s, exported.files.value(0).data}}, options);
        QStringList importedPaths;
        for (const io::ImportedFile &file : imported.files)
            importedPaths.append(file.path);
        QVERIFY2(importedPaths.contains(path), qPrintable(importedPaths.join(u", "_s)));
    }

    // ---- ignored paths (SYNTAX §1): vault, index, sync and import agree --------------------------

    void pathRules_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("tracked");  // the vault lists it (files())
        QTest::addColumn<bool>("indexed");  // the index knows it (page or attachment)
        QTest::addColumn<bool>("synced");   // sync::isSyncablePath
        QTest::addColumn<bool>("imported"); // kept by runImport
        const auto row = [](const char *name, const QString &path, bool tracked, bool indexed, bool synced,
                            bool imported) { QTest::newRow(name) << path << tracked << indexed << synced << imported; };
        row("note", u"Notatki/Zażółć.md"_s, true, true, true, true);
        row("attachment", u"assets/zdjęcie.png"_s, true, true, true, true);
        row("user folder named Logseq", u"Logseq/notatka.md"_s, true, true, true, true);
        row("syncthing conflict copy", u"Notatka.sync-conflict-20261014-101010-ABCDEFG.md"_s, true, false, true, true);
        row("brulion conflict copy", u"Notatka.conflict-telefon-20261014-101010.md"_s, true, false, true, true);
        row("other brulion settings", u".brulion/workspace.json"_s, false, false, false, false);
        for (const char *ignored :
             {".trash/a.md", ".git/HEAD", ".obsidian/app.json", "logseq/config.edn", ".stversions/a.md",
              "node_modules/pakiet/README.md", ".github/workflows/ci.yml", "Projekty/.git/config",
              "~syncthing~a.md.tmp", "Folder/.syncthing.a.md.tmp", ".~lock.raport.odt#", "film.mp4.part", ".DS_Store",
              "Zdjęcia/Thumbs.db", ".stignore"}) {
            QTest::newRow(ignored) << QString::fromUtf8(ignored) << false << false << false << false;
        }
    }

    void pathRules()
    {
        QFETCH(QString, path);
        QFETCH(bool, tracked);
        QFETCH(bool, indexed);
        QFETCH(bool, synced);
        QFETCH(bool, imported);
        const QByteArray bytes = paths::isMarkdown(path) ? QByteArray("- treść #tag\n") : QByteArray("\x01\x02", 2);

        QTemporaryDir dir;
        const QString abs = dir.path() + u"/vault/"_s + path;
        QVERIFY(QDir().mkpath(QFileInfo(abs).absolutePath()));
        QFile file(abs);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();
        Vault vault(dir.path() + u"/vault"_s);
        QVERIFY(vault.open());
        bool listed = false;
        for (const FileStat &st : vault.files())
            listed = listed || st.path == path;
        QCOMPARE(listed, tracked);

        VaultIndex index;
        index.update(path, paths::isMarkdown(path) ? QString::fromUtf8(bytes) : QString());
        QCOMPARE(index.contains(path), indexed);

        QCOMPARE(sync::isSyncablePath(path), synced);

        io::ImportOptions options;
        options.timeZone = QTimeZone("Europe/Warsaw");
        // A second note at the top keeps the importer from stripping a common root folder.
        const io::ImportResult result = io::runImport(
                *io::importerById(u"markdown"),
                {io::InputFile{path, bytes}, io::InputFile{u"Strona główna.md"_s, QByteArray("- start\n")}}, options);
        QStringList kept;
        for (const io::ImportedFile &out : result.files) {
            if (out.path != u"Strona główna.md"_s)
                kept.append(out.path);
        }
        QVERIFY2(kept.isEmpty() != imported, qPrintable(kept.join(u", "_s)));
        for (const QString &out : std::as_const(kept))
            QVERIFY2(!paths::isIgnored(out) && sync::isSyncablePath(out), qPrintable(out));
    }
};

QTEST_MAIN(TestIntegrationConsistency)
#include "tst_integration_consistency.moc"
