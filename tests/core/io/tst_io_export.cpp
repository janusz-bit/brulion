// Exporters: Markdown / Obsidian / Logseq ZIP, HTML site, single HTML, JSON, iCalendar, OPML,
// tasks CSV — and the Logseq export -> import round trip.
#include "iotest.h"

#include "index/vaultindex.h"
#include "io/csv.h"
#include "io/exporter.h"
#include "io/exportutil_p.h"
#include "io/ical.h"
#include "io/importer.h"
#include "io/zip.h"
#include "markdown/math.h"
#include "markdown/parse.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QXmlStreamReader>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

const QString Dom = u"---\n"
                    "tags: [dom, remont]\n"
                    "aliases: [Remont]\n"
                    "---\n"
                    "# Dom\n"
                    "\n"
                    "Plan remontu ^plan\n"
                    "\n"
                    "- [ ] Kup farbę 📅 2026-10-20 ⏳ 2026-10-18 10:00 🔁 every week ⏫ #zakupy ^farba\n"
                    "- [x] Zmierz ściany ✅ 2026-10-10\n"
                    "- [/] Malowanie\n"
                    "\t- [-] Tapeta\n"
                    "- [>] Meble\n"
                    "\n"
                    "1. pierwszy\n"
                    "2. drugi\n"
                    "\n"
                    "```js\n"
                    "let x = 1; // [[nie link]]\n"
                    "```\n"
                    "\n"
                    "| Pokój | Kolor |\n"
                    "| --- | --- |\n"
                    "| Salon | biały |\n"
                    "\n"
                    "> [!note] Uwaga\n"
                    "> Pamiętaj o ((farba))\n"
                    "\n"
                    "Zdjęcie: ![[assets/salon.png]] i [plan](../assets/plan.pdf)\n"
                    "⏰ 2026-10-16 09:00 Zadzwoń do fachowca\n"_s;

const QString Zakupy = u"- Lista na [[Dom]] i [[Projekty/Dom#^farba|farba]]\n"
                       "- ![[Dom#^plan]]\n"
                       "- Odnośnik do ((farba))\n"
                       "- TODO Logseq marker\n"
                       "  SCHEDULED: <2026-10-21 Wed>\n"
                       "- [[2026-10-14]] notatka\n"
                       "- [ ] =SUM(A1) podejrzane\n"_s;

const QString Journal = u"- Rano: zadzwonić ⏰! 2026-10-14 07:30\n"
                        "- [ ] Pranie 📅 2026-10-14\n"_s;

const QString Unsafe =
        u"<script>alert(1)</script>\n\nTekst <b>pogrubiony</b> [x](javascript:alert(1)) & \"cudzysłów\"\n"_s;

ExportInput fixture()
{
    ExportInput in;
    in.files = {
            iotest::text(u"Projekty/Dom.md"_s, Dom),
            iotest::text(u"Zakupy.md"_s, Zakupy),
            iotest::text(u"journals/2026-10-14.md"_s, Journal),
            iotest::text(u"Bezpieczeństwo.md"_s, Unsafe),
            iotest::bytes(u"assets/salon.png"_s, "\x89PNG-salon"),
            iotest::bytes(u"assets/plan.pdf"_s, "%PDF-plan"),
            iotest::text(u".trash/stary.md"_s, u"stary"_s),
            iotest::text(u".brulion/config.json"_s, u"{\"version\": 1}"_s),
            iotest::text(u"Zakupy.sync-conflict-20261014-101010-ABCDEFG.md"_s, u"konflikt"_s),
    };
    in.vaultName = u"Mój skarbiec"_s;
    in.now = QDateTime(QDate(2026, 10, 15), QTime(12, 0), iotest::warsaw());
    return in;
}

ExportedFile exportWith(const QString &id, const ExportInput &input = fixture())
{
    const Exporter *e = exporterById(id);
    Q_ASSERT(e);
    const ExportResult r = runExport(*e, input);
    Q_ASSERT(r.files.size() == 1);
    return r.files.first();
}

QString textOf(const QList<FileEntry> &entries, const QString &path)
{
    return QString::fromUtf8(iotest::entryData(entries, path));
}

QString uuid(const QString &id)
{
    return detail::logseqUuid(id);
}

ImportResult reimportLogseq(const ExportedFile &zip)
{
    return runImport(*importerById(u"logseq"), {iotest::bytes(zip.name, zip.data)}, iotest::options());
}

} // namespace

class TestIoExport : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void registry()
    {
        QStringList ids;
        for (const Exporter *e : exporters()) {
            ids.append(e->id());
            QVERIFY(!e->name().isEmpty());
            QVERIFY(e->fileExtension().startsWith(u'.'));
            // Every exporter copes with an empty vault.
            const ExportResult r = runExport(*e, ExportInput());
            QCOMPARE(r.files.size(), 1);
            QVERIFY(r.files.first().name.endsWith(e->fileExtension()));
        }
        QCOMPARE(ids, QStringList({u"markdown-zip"_s, u"obsidian-zip"_s, u"logseq-zip"_s, u"html-site"_s,
                                   u"html-single"_s, u"json"_s, u"ics"_s, u"opml"_s, u"tasks-csv"_s}));
        QVERIFY(exporterById(u"nope") == nullptr);
    }

    void fileNames()
    {
        QCOMPARE(exportWith(u"markdown-zip"_s).name, u"Mój skarbiec-2026-10-15.zip"_s);
        QCOMPARE(exportWith(u"logseq-zip"_s).name, u"Mój skarbiec-2026-10-15-logseq.zip"_s);
        QCOMPARE(exportWith(u"tasks-csv"_s).name, u"Mój skarbiec-2026-10-15-tasks.csv"_s);
        QCOMPARE(exportWith(u"ics"_s).mime, u"text/calendar"_s);
        ExportInput noName;
        QCOMPARE(runExport(*exporterById(u"json"), noName).files.first().name, u"Brulion.json"_s);
    }

    void markdownZip()
    {
        const ExportedFile f = exportWith(u"markdown-zip"_s);
        QCOMPARE(f.mime, u"application/zip"_s);
        const QList<FileEntry> entries = readZip(f.data);
        QCOMPARE(iotest::entryPaths(entries),
                 QStringList({u"Mój skarbiec/.brulion/config.json"_s, u"Mój skarbiec/Bezpieczeństwo.md"_s,
                              u"Mój skarbiec/Projekty/Dom.md"_s, u"Mój skarbiec/Zakupy.md"_s,
                              u"Mój skarbiec/assets/plan.pdf"_s, u"Mój skarbiec/assets/salon.png"_s,
                              u"Mój skarbiec/journals/2026-10-14.md"_s}));
        QCOMPARE(textOf(entries, u"Mój skarbiec/Projekty/Dom.md"_s), Dom);
        QCOMPARE(iotest::entryData(entries, u"Mój skarbiec/assets/salon.png"_s), QByteArray("\x89PNG-salon"));
        // Deterministic: same input, same bytes.
        QCOMPARE(exportWith(u"markdown-zip"_s).data, f.data);
    }

    void obsidianZip()
    {
        const QList<FileEntry> entries = readZip(exportWith(u"obsidian-zip"_s).data);
        const QString root = u"Mój skarbiec/"_s;
        QCOMPARE(textOf(entries, root + u"Zakupy.md"_s), u"- Lista na [[Dom]] i [[Projekty/Dom#^farba|farba]]\n"
                                                         "- ![[Dom#^plan]]\n"
                                                         "- Odnośnik do [[Projekty/Dom#^farba]]\n"
                                                         "- [ ] Logseq marker ⏳ 2026-10-21\n"
                                                         "- [[2026-10-14]] notatka\n"
                                                         "- [ ] =SUM(A1) podejrzane\n"_s);
        QVERIFY(textOf(entries, root + u"Projekty/Dom.md"_s).contains(u"> Pamiętaj o [[#^farba]]\n"_s));
        const QJsonObject daily =
                QJsonDocument::fromJson(iotest::entryData(entries, root + u".obsidian/daily-notes.json"_s)).object();
        QCOMPARE(daily.value(u"folder"_s).toString(), u"journals"_s);
        QCOMPARE(daily.value(u"format"_s).toString(), u"YYYY-MM-DD"_s);
        QCOMPARE(daily.value(u"template"_s).toString(), u"templates/journal"_s);
        const QJsonObject app =
                QJsonDocument::fromJson(iotest::entryData(entries, root + u".obsidian/app.json"_s)).object();
        QCOMPARE(app.value(u"attachmentFolderPath"_s).toString(), u"assets"_s);
        QCOMPARE(iotest::entryData(entries, root + u"assets/plan.pdf"_s), QByteArray("%PDF-plan"));
        QVERIFY(!iotest::entryPaths(entries).contains(root + u".brulion/config.json"_s));
    }

    void obsidianRelativeLinks()
    {
        ExportInput in;
        in.files = {
                iotest::text(u"Projekty/Plan.md"_s, u"[[Sub/Dom]] [[Inne]] ![[../assets/a.png]] [[Sub/Dom|alias]]"_s),
                iotest::text(u"Projekty/Sub/Dom.md"_s, u"dom"_s), iotest::text(u"Projekty/Inne.md"_s, u"i"_s),
                iotest::bytes(u"assets/a.png"_s, "A")};
        const QList<FileEntry> entries = readZip(exportWith(u"obsidian-zip"_s, in).data);
        QCOMPARE(textOf(entries, u"Brulion/Projekty/Plan.md"_s),
                 u"[[Projekty/Sub/Dom]] [[Inne]] ![[assets/a.png]] [[Projekty/Sub/Dom|alias]]"_s);
    }

    void obsidianParagraphIds()
    {
        // Obsidian knows block ids only as " ^id": an `id::` line of a paragraph becomes one, so
        // references to the paragraph keep working.
        ExportInput in;
        in.files = {iotest::text(u"A.md"_s,
                                 u"Akapit\nz id\nid:: abc\n\nInny\ntyp:: x\nid:: zostaje\n\n- odnośnik ((abc))\n"_s)};
        const QList<FileEntry> entries = readZip(exportWith(u"obsidian-zip"_s, in).data);
        QCOMPARE(textOf(entries, u"Brulion/A.md"_s),
                 u"Akapit\nz id ^abc\n\nInny\ntyp:: x\nid:: zostaje\n\n- odnośnik [[#^abc]]\n"_s);
    }

    void momentFormats_data()
    {
        QTest::addColumn<QString>("qt");
        QTest::addColumn<QString>("moment");
        QTest::newRow("iso") << u"yyyy-MM-dd"_s << u"YYYY-MM-DD"_s;
        QTest::newRow("polish") << u"dd.MM.yyyy"_s << u"DD.MM.YYYY"_s;
        QTest::newRow("short year") << u"d.M.yy"_s << u"D.M.YY"_s;
        QTest::newRow("folders") << u"yyyy/MM/yyyy-MM-dd"_s << u"YYYY/MM/YYYY-MM-DD"_s;
        QTest::newRow("names") << u"dddd, d MMMM yyyy"_s << u"dddd, D MMMM YYYY"_s;
        QTest::newRow("literal") << u"'Dziennik' yyyy"_s << u"[Dziennik] YYYY"_s;
    }

    void momentFormats()
    {
        QFETCH(QString, qt);
        QFETCH(QString, moment);
        QCOMPARE(detail::momentFormat(qt), moment);
    }

    void logseqZip()
    {
        const QList<FileEntry> entries = readZip(exportWith(u"logseq-zip"_s).data);
        const QString root = u"Mój skarbiec/"_s;
        QCOMPARE(iotest::entryPaths(entries),
                 QStringList({root + u"assets/plan.pdf"_s, root + u"assets/salon.png"_s,
                              root + u"journals/2026_10_14.md"_s, root + u"logseq/config.edn"_s,
                              root + u"pages/Bezpieczeństwo.md"_s, root + u"pages/Projekty___Dom.md"_s,
                              root + u"pages/Zakupy.md"_s}));
        const QString dom = textOf(entries, root + u"pages/Projekty___Dom.md"_s);
        // #dom names this page, which Logseq knows as the namespace page "Projekty/Dom"; the
        // alias Remont stays (Logseq resolves alias:: itself).
        const QString expected = u"alias:: Remont\n"
                                 "tags:: [[Projekty/Dom]], remont\n"
                                 "\n"
                                 "- # Dom\n"
                                 "- Plan remontu\n"
                                 "  id:: "_s
                                 + uuid(u"plan"_s)
                                 + u"\n"
                                   "- TODO [#A] Kup farbę #zakupy\n"
                                   "  id:: "_s
                                 + uuid(u"farba"_s)
                                 + u"\n"
                                   "  SCHEDULED: <2026-10-18 Sun 10:00 +1w>\n"
                                   "  DEADLINE: <2026-10-20 Tue>\n"
                                   "- DONE Zmierz ściany ✅ 2026-10-10\n"
                                   "- DOING Malowanie\n"
                                   "\t- CANCELED Tapeta\n"
                                   "- WAITING Meble\n"
                                   "- pierwszy\n"
                                   "  logseq.order-list-type:: number\n"
                                   "- drugi\n"
                                   "  logseq.order-list-type:: number\n"
                                   "- ```js\n"
                                   "  let x = 1; // [[nie link]]\n"
                                   "  ```\n"
                                   "- | Pokój | Kolor |\n"
                                   "  | --- | --- |\n"
                                   "  | Salon | biały |\n"
                                   "- > [!note] Uwaga\n"
                                   "  > Pamiętaj o (("_s
                                 + uuid(u"farba"_s)
                                 + u"))\n"
                                   "- Zdjęcie: ![salon.png](../assets/salon.png) i [plan](../assets/plan.pdf)\n"
                                   "  ⏰ 2026-10-16 09:00 Zadzwoń do fachowca\n"_s;
        QCOMPARE(dom, expected);
        QCOMPARE(textOf(entries, root + u"pages/Zakupy.md"_s),
                 u"- Lista na [[Projekty/Dom]] i (("_s + uuid(u"farba"_s) + u"))\n- {{embed (("_s + uuid(u"plan"_s)
                         + u"))}}\n- Odnośnik do (("_s + uuid(u"farba"_s)
                         + u"))\n- TODO Logseq marker\n  SCHEDULED: <2026-10-21 Wed>\n- [[2026-10-14]] notatka\n"
                           "- TODO =SUM(A1) podejrzane\n"_s);
        QCOMPARE(textOf(entries, root + u"journals/2026_10_14.md"_s),
                 u"- Rano: zadzwonić ⏰! 2026-10-14 07:30\n- TODO Pranie\n  DEADLINE: <2026-10-14 Wed>\n"_s);
        const QString edn = textOf(entries, root + u"logseq/config.edn"_s);
        QVERIFY(edn.contains(u":journal/page-title-format \"yyyy-MM-dd\""_s));
        QVERIFY(edn.contains(u":file/name-format :triple-lowbar"_s));
    }

    // A tag names a page (SYNTAX §3): when Logseq knows that page under another name, the tag
    // says it.
    void logseqTagsNamePages()
    {
        ExportInput in;
        in.files = {iotest::text(u"Projekty/Dom.md"_s, u"- x"_s), iotest::text(u"Zakupy.md"_s, u"- y"_s),
                    iotest::text(u"Ludzie/Jan Kowalski.md"_s, u"---\naliases: [Janek]\n---\n- z"_s),
                    iotest::text(u"Notatki.md"_s, u"- #dom, #Zakupy, #Janek, #praca, #[[Projekty/Dom]], `#dom`, "
                                                  "#dom/kuchnia\n  tags:: dom, praca\n"_s)};
        const QString page = textOf(readZip(exportWith(u"logseq-zip"_s, in).data), u"Brulion/pages/Notatki.md"_s);
        QVERIFY2(page.contains(
                         u"- #[[Projekty/Dom]], #Zakupy, #Janek, #praca, #[[Projekty/Dom]], `#dom`, #dom/kuchnia\n"_s),
                 qPrintable(page));
        QVERIFY2(page.contains(u"tags:: [[Projekty/Dom]], praca\n"_s), qPrintable(page));
    }

    void logseqKeepsWhatLogseqCannotSay()
    {
        ExportInput in;
        in.files = {iotest::text(
                u"Strona.md"_s,
                u"```js\nlet x = 1;\n```\n^kod1\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n^tab1\n\n"
                "TODO to nie zadanie\n\n[ ] ani to\n\n- [ ] pilne 🔺\n- [ ] kiedyś ⏬\n- [ ] ważne ⏫\n"_s)};
        const QString page = textOf(readZip(exportWith(u"logseq-zip"_s, in).data), u"Brulion/pages/Strona.md"_s);
        // The id of a code block or table follows its content (inside, it would be code / break the table).
        QVERIFY2(page.contains(u"- ```js\n  let x = 1;\n  ```\n  id:: "_s + uuid(u"kod1"_s) + u'\n'), qPrintable(page));
        QVERIFY(page.contains(u"- | a | b |\n  | --- | --- |\n  | 1 | 2 |\n  id:: "_s + uuid(u"tab1"_s) + u'\n'));
        // A paragraph that starts like a task stays text: a no-break space after the marker.
        QVERIFY(page.contains(u"- TODO\u00A0to nie zadanie\n"_s));
        QVERIFY(page.contains(u"- [ ]\u00A0ani to\n"_s));
        // Logseq has A/B/C only: highest and lowest keep their emoji, high becomes [#A].
        QVERIFY(page.contains(u"- TODO pilne 🔺\n"_s));
        QVERIFY(page.contains(u"- TODO kiedyś ⏬\n"_s));
        QVERIFY(page.contains(u"- TODO [#A] ważne\n"_s));
    }

    void logseqNamespacesAndAssets()
    {
        ExportInput in;
        in.files = {iotest::text(u"a/b/Strona?.md"_s,
                                 u"![[img/x.png]] [[c/d]] [t](../../c/d.md) ![[c/d]] [[a/b/Strona?|alias]]"_s),
                    iotest::text(u"c/d.md"_s, u"x"_s), iotest::bytes(u"a/b/img/x.png"_s, "X"),
                    iotest::bytes(u"assets/img_x.png"_s, "Y")};
        const QList<FileEntry> entries = readZip(exportWith(u"logseq-zip"_s, in).data);
        QCOMPARE(iotest::entryPaths(entries),
                 QStringList({u"Brulion/assets/a_b_img_x.png"_s, u"Brulion/assets/img_x.png"_s,
                              u"Brulion/logseq/config.edn"_s, u"Brulion/pages/a___b___Strona%3F.md"_s,
                              u"Brulion/pages/c___d.md"_s}));
        QCOMPARE(
                textOf(entries, u"Brulion/pages/a___b___Strona%3F.md"_s),
                u"- ![x.png](../assets/a_b_img_x.png) [[c/d]] [t]([[c/d]]) {{embed [[c/d]]}} [alias]([[a/b/Strona?]])\n"_s);
    }

    void logseqRoundTrip()
    {
        const ExportInput in = fixture();
        const ImportResult imported = reimportLogseq(exportWith(u"logseq-zip"_s, in));
        // Parse both sides.
        const auto parse = [](const QString &path, const QString &content) {
            return markdown::parseNote(path, content);
        };
        const ParsedNote original = parse(u"Projekty/Dom.md"_s, Dom);
        const ParsedNote back = parse(u"Projekty/Dom.md"_s, iotest::textAt(imported, u"Projekty/Dom.md"_s));
        QCOMPARE(back.tasks.size(), original.tasks.size());
        for (qsizetype i = 0; i < original.tasks.size(); ++i) {
            const Task &a = original.tasks[i];
            const Task &b = back.tasks[i];
            QCOMPARE(b.status, a.status);
            QCOMPARE(b.text, a.text);
            QCOMPARE(b.due.has_value(), a.due.has_value());
            if (a.due)
                QCOMPARE(b.due->toString(), a.due->toString());
            QCOMPARE(b.scheduled.has_value(), a.scheduled.has_value());
            if (a.scheduled)
                QCOMPARE(b.scheduled->toString(), a.scheduled->toString());
            QCOMPARE(b.priority, a.priority);
            QCOMPARE(b.recurrence, a.recurrence);
        }
        QCOMPARE(back.aliases, QStringList({u"Remont"_s}));
        QVERIFY(back.tags.contains(u"remont"_s));
        // References still resolve to the same blocks.
        VaultIndex index;
        for (const ImportedFile &f : imported.files)
            index.update(f.path, QString::fromUtf8(f.data));
        const auto farba = index.blockById(uuid(u"farba"_s));
        QVERIFY(farba);
        QCOMPARE(farba->path, u"Projekty/Dom.md"_s);
        QCOMPARE(index.note(farba->path)->blocks[farba->blockIndex].task->text, u"Kup farbę #zakupy"_s);
        const QString zakupy = iotest::textAt(imported, u"Zakupy.md"_s);
        QVERIFY(zakupy.contains(u"(("_s + uuid(u"farba"_s) + u"))"_s));
        QVERIFY(zakupy.contains(u"!(("_s + uuid(u"plan"_s) + u"))"_s));
        const ParsedNote journal =
                parse(u"journals/2026-10-14.md"_s, iotest::textAt(imported, u"journals/2026-10-14.md"_s));
        QCOMPARE(journal.tasks.size(), 1);
        QCOMPARE(journal.tasks.first().due->toString(), u"2026-10-14"_s);
        QCOMPARE(journal.reminders.size(), 1);
        QVERIFY(journal.reminders.first().alarm);
        QVERIFY(iotest::fileAt(imported, u"assets/Logseq/salon.png"_s));
        QVERIFY(iotest::textAt(imported, u"Projekty/Dom.md"_s).contains(u"![salon.png](../assets/Logseq/salon.png)"_s));
    }

    void htmlSite()
    {
        const QList<FileEntry> entries = readZip(exportWith(u"html-site"_s).data);
        const QString root = u"Mój skarbiec/"_s;
        const QStringList paths = iotest::entryPaths(entries);
        for (const QString &p :
             {u"index.html"_s, u"style.css"_s, u"Projekty/Dom.html"_s, u"Zakupy.html"_s, u"journals/2026-10-14.html"_s,
              u"tags/dom.html"_s, u"tags/zakupy.html"_s, u"assets/salon.png"_s, u"Bezpieczeństwo.html"_s})
            QVERIFY2(paths.contains(root + p), qPrintable(p));
        const QString dom = textOf(entries, root + u"Projekty/Dom.html"_s);
        QVERIFY(dom.contains(u"<h1 class=\"title\">Dom</h1>"_s));
        // #zakupy names the page Zakupy (SYNTAX §3): the tag leads there.
        QVERIFY(dom.contains(u"<li class=\"task todo\" id=\"block-farba\"><input type=\"checkbox\" disabled> Kup farbę "
                             "<a class=\"tag\" href=\"../Zakupy.html\">#zakupy</a>"_s));
        QVERIFY(dom.contains(u"<p class=\"tags\"><a class=\"tag\" href=\"Dom.html\">#dom</a>"_s));
        QVERIFY(dom.contains(u"<span class=\"chip due\">📅 2026-10-20</span>"_s));
        QVERIFY(dom.contains(u"<li class=\"task done\"><input type=\"checkbox\" disabled checked> Zmierz ściany"_s));
        QVERIFY(dom.contains(u"<img src=\"../assets/salon.png\" alt=\"salon.png\" loading=\"lazy\">"_s));
        QVERIFY(dom.contains(u"<a class=\"internal\" href=\"../assets/plan.pdf\">plan</a>"_s));
        QVERIFY(dom.contains(u"<ol><li>pierwszy</li><li>drugi</li></ol>"_s));
        QVERIFY(dom.contains(u"<pre><code class=\"language-js\">let x = 1; // [[nie link]]</code></pre>"_s));
        QVERIFY(dom.contains(u"<th>Pokój</th>"_s));
        QVERIFY(dom.contains(u"<div class=\"callout callout-note\"><div class=\"callout-title\">Uwaga</div>"_s));
        QVERIFY(dom.contains(u"<section class=\"backlinks\">"_s));
        QVERIFY(dom.contains(u"<a href=\"../Zakupy.html\">Zakupy</a>"_s));
        QVERIFY(dom.contains(u"<link rel=\"stylesheet\" href=\"../style.css\">"_s));
        const QString zakupy = textOf(entries, root + u"Zakupy.html"_s);
        QVERIFY(zakupy.contains(u"<a class=\"internal\" href=\"Projekty/Dom.html\">Dom</a>"_s));
        QVERIFY(zakupy.contains(u"<a class=\"internal\" href=\"Projekty/Dom.html#block-farba\">farba</a>"_s));
        QVERIFY(zakupy.contains(u"<a class=\"block-ref\" href=\"Projekty/Dom.html#block-farba\">Kup farbę"_s));
        QVERIFY(zakupy.contains(u"<div class=\"embed\""_s) || zakupy.contains(u"Plan remontu"_s));
        QVERIFY(zakupy.contains(u"href=\"journals/2026-10-14.html\""_s));
        // Its references: the block tagged #zakupy.
        const QString zakupyRefs = zakupy.mid(zakupy.indexOf(u"<section class=\"backlinks\">"_s));
        QVERIFY2(zakupyRefs.contains(u"<li><a href=\"Projekty/Dom.html\">Dom</a><span class=\"excerpt\">Kup farbę"_s),
                 qPrintable(zakupyRefs));
        const QString unsafe = textOf(entries, root + u"Bezpieczeństwo.html"_s);
        QVERIFY(!unsafe.contains(u"<script>"_s));
        QVERIFY(!unsafe.contains(u"<b>"_s));
        QVERIFY(!unsafe.contains(u"javascript:"_s) || unsafe.contains(u"[x](javascript:alert(1))"_s));
        QVERIFY(unsafe.contains(u"&lt;script&gt;alert(1)&lt;/script&gt;"_s));
        QVERIFY(unsafe.contains(u"&amp; &quot;cudzysłów&quot;"_s));
        const QString index = textOf(entries, root + u"index.html"_s);
        QVERIFY(index.contains(u"href=\"journals/2026-10-14.html\""_s));
        QVERIFY(index.contains(u"href=\"Projekty/Dom.html\""_s));
        QVERIFY(index.contains(u"href=\"tags/dom.html\""_s));
        const QString tag = textOf(entries, root + u"tags/dom.html"_s);
        QVERIFY(tag.contains(u"<a href=\"../Projekty/Dom.html\">Dom</a>"_s));
    }

    void htmlTagsFollowTheParser()
    {
        ExportInput in;
        in.files = {iotest::text(
                            u"Strona.md"_s,
                            u"- #tag #- #_ #٣ (#ok) **#nie** [link #nie](https://x.pl) [[Inna|alias #nie]] ((b1))\n"_s),
                    iotest::text(u"Inna.md"_s, u"- [[Strona]] i #wewnątrz ^b1\n"_s)};
        const QList<FileEntry> entries = readZip(exportWith(u"html-site"_s, in).data);
        const QString page = textOf(entries, u"Brulion/Strona.html"_s);
        QVERIFY2(page.contains(u">#tag</a>"_s), qPrintable(page));
        QVERIFY(page.contains(u">#ok</a>"_s));
        const qsizetype from = page.indexOf(u"<ul><li>"_s);
        const QString item = page.mid(from, page.indexOf(u"</li></ul>"_s, from) - from);
        QCOMPARE(item.count(u"class=\"tag\""_s), 2);
        QVERIFY(item.contains(u"</a> #- #_ #٣ ("_s));
        QVERIFY(page.contains(u"<p class=\"tags\"><a class=\"tag\" href=\"tags/tag.html\">#tag</a> <a class=\"tag\" "
                              "href=\"tags/ok.html\">#ok</a></p>"_s));
        // Link labels and block references never hold links of their own (no nested anchors).
        QVERIFY(page.contains(u"rel=\"noopener\">link #nie</a>"_s));
        QVERIFY(page.contains(u">alias #nie</a>"_s));
        QVERIFY(page.contains(u"<a class=\"block-ref\" href=\"Inna.html#block-b1\">Strona i #wewnątrz</a>"_s));
    }

    void htmlHeadingLinksMatchLikeTheApp()
    {
        // Heading links and embeds as Brulion copies them (SYNTAX §2): markup and tag hashes left
        // out, an Obsidian heading path for a repeated heading, a heading with a block id. The
        // export finds the heading as the app does (markdown::findHeading).
        ExportInput in;
        in.files = {iotest::text(u"Plan.md"_s, u"# Plan\n\n## Plan: **wrzesień** #praca\n\n- zadanie wrześniowe\n\n"
                                               u"## Budżet\n\n### Zadania\n\n- budżetowe zadanie\n\n## Remont\n\n"
                                               u"### Zadania\n\n- remontowe zadanie\n\n## Z identyfikatorem ^nag1\n\n"
                                               u"- pod nagłówkiem\n"_s),
                    iotest::text(u"Linki.md"_s, u"![[Plan#Plan wrzesień praca]]\n\n![[Plan#Remont#Zadania]]\n\n"
                                                u"- [[Plan#Remont#Zadania]] [[Plan#plan wrzesien praca]] "
                                                u"[[Plan#Z identyfikatorem]]\n"_s)};
        const QList<FileEntry> entries = readZip(exportWith(u"html-site"_s, in).data);
        const QString page = textOf(entries, u"Brulion/Linki.html"_s);
        QVERIFY2(page.contains(u"zadanie wrześniowe"_s), qPrintable(page));
        QVERIFY(page.contains(u"remontowe zadanie"_s));
        QVERIFY(!page.contains(u"budżetowe zadanie"_s));
        QVERIFY(page.contains(u"href=\"Plan.html#zadania\""_s));
        QVERIFY(!page.contains(u"#remont-zadania"_s));
        QVERIFY(page.contains(u"href=\"Plan.html#plan-wrzesień-praca\""_s));
        QVERIFY(page.contains(u"href=\"Plan.html#block-nag1\""_s));
        const QString plan = textOf(entries, u"Brulion/Plan.html"_s);
        QVERIFY(plan.contains(u"id=\"plan-wrzesień-praca\""_s));
        QVERIFY(plan.contains(u"id=\"block-nag1\""_s));
    }

    void htmlMath()
    {
        ExportInput in;
        in.files = {iotest::text(u"Wzory.md"_s, u"Pole koła: $\\pi r^2$, źródło: $\\sqrt{2}$.\n"
                                                "\n"
                                                "$$\\frac{a}{b}$$\n"
                                                "\n"
                                                "```math\n"
                                                "\\sum_{i=1}^{n} i\n"
                                                "```\n"_s)};
        const QList<FileEntry> entries = readZip(exportWith(u"html-site"_s, in).data);
        const QString page = textOf(entries, u"Brulion/Wzory.html"_s);
        QVERIFY2(page.contains(u"Pole koła: <span class=\"math\"><i>π</i><i>r</i><sup>2</sup></span>, źródło: "
                               "<span class=\"math\">√<span style=\"text-decoration:overline\">2</span></span>."_s),
                 qPrintable(page));
        QVERIFY(page.contains(u"<span class=\"math display\"><sup><i>a</i></sup>&#8260;<sub><i>b</i></sub></span>"_s));
        QVERIFY(page.contains(u"<div class=\"math\">∑<sub><i>i</i> = 1</sub><sup><i>n</i></sup> <i>i</i></div>"_s));
        QVERIFY(!page.contains(u"language-math"_s));
    }

    // `/Wzór matematyczny` in an outline or a journal writes a ```math block inside a list item:
    // the export shows the formula there too, and other fenced items as code blocks.
    void htmlFencesInListItems()
    {
        ExportInput in;
        in.files = {iotest::text(u"Konspekt.md"_s, u"- Wzór w konspekcie:\n"
                                                   "- ```math\n"
                                                   "  a^2 + b^2 = c^2\n"
                                                   "  ```\n"
                                                   "  - ```calc\n"
                                                   "    2 + 2\n"
                                                   "    ```\n"
                                                   "- ~~~LaTeX\n"
                                                   "  \\frac{1}{2}\n"
                                                   "  ~~~\n"
                                                   "- Tekst i `kod`\n"
                                                   "  ```js\n"
                                                   "  x\n"
                                                   "  ```\n"
                                                   "- [ ] ```math\n"
                                                   "- ```\n"
                                                   "- koniec\n"_s)};
        const QList<FileEntry> entries = readZip(exportWith(u"html-site"_s, in).data);
        const QString page = textOf(entries, u"Brulion/Konspekt.html"_s);
        const QString formula = markdown::renderMath(u"a^2 + b^2 = c^2"_s, true);
        QVERIFY2(page.contains(
                         u"<li><div class=\"math\">"_s + formula
                         + u"</div><ul><li><pre><code class=\"language-calc\">2 + 2</code></pre></li></ul></li>"_s),
                 qPrintable(page));
        QVERIFY(page.contains(u"<li><div class=\"math\">"_s + markdown::renderMath(u"\\frac{1}{2}"_s, true)
                              + u"</div></li>"_s));
        // Only an item that is nothing but the fence is a block; a lone opening fence is text.
        QVERIFY(page.contains(u"<li>Tekst i <code>kod</code>"_s));
        QVERIFY(page.contains(u"<li>```</li>"_s));
        QVERIFY(!page.contains(u"<code>math"_s));
    }

    void htmlSingle()
    {
        const ExportedFile f = exportWith(u"html-single"_s);
        QCOMPARE(f.mime, u"text/html"_s);
        const QString html = QString::fromUtf8(f.data);
        QVERIFY(html.startsWith(u"<!DOCTYPE html>"_s));
        QVERIFY(html.contains(u"<nav class=\"toc\">"_s));
        QVERIFY(html.contains(u"<style>"_s));
        QVERIFY(html.contains(u"src=\"data:image/png;base64,"_s
                              + QString::fromLatin1(QByteArray("\x89PNG-salon").toBase64())));
        QVERIFY(html.contains(u"href=\"data:application/pdf;base64,"_s));
        // Pages first (by path), then journals: Bezpieczeństwo n1, Projekty/Dom n2, Zakupy n3, journal n4.
        QVERIFY(html.contains(u"<section class=\"note\" id=\"n2\">"_s));
        QVERIFY(html.contains(u"id=\"n2-block-farba\""_s));
        QVERIFY(html.contains(u"href=\"#n2-block-farba\""_s));
        QVERIFY(html.contains(u"href=\"#n2\""_s));
        QVERIFY(html.contains(u"href=\"#n4\""_s));
        QVERIFY(html.contains(u"<a class=\"tag\" href=\"#n3\">#zakupy</a>"_s)); // the page the tag names
        QVERIFY(!html.contains(u"<script>"_s));
    }

    void htmlHostileFileNames()
    {
        // File names that look like URL schemes ("javascript:…" is a valid Linux file name and
        // arrives through sync) must never turn into a scheme of an href / src.
        ExportInput in;
        in.files = {iotest::text(u"javascript:alert(1).md"_s, u"x #tag"_s),
                    iotest::text(u"Start.md"_s, u"[[javascript:alert(1)]] ![[data:x.png]] [[data:x.png]] #tag"_s),
                    iotest::bytes(u"data:x.png"_s, "PNG")};
        const QList<FileEntry> entries = readZip(exportWith(u"html-site"_s, in).data);
        int pages = 0;
        for (const FileEntry &e : entries) {
            if (!e.path.endsWith(u".html"_s))
                continue;
            ++pages;
            const QString html = QString::fromUtf8(e.data);
            QVERIFY2(!html.contains(u"href=\"javascript:"_s, Qt::CaseInsensitive), qPrintable(e.path));
            QVERIFY2(!html.contains(u"href=\"data:"_s, Qt::CaseInsensitive), qPrintable(e.path));
            QVERIFY2(!html.contains(u"src=\"data:"_s, Qt::CaseInsensitive), qPrintable(e.path));
        }
        QCOMPARE(pages, 4); // two notes, index, tag page
        const QString index = textOf(entries, u"Brulion/index.html"_s);
        QVERIFY(index.contains(u"href=\"./javascript:alert%281%29.html\""_s));
    }

    void markdownLinkTargetsWithHash()
    {
        // "[C#](C%23.md)": the decoded target "C#.md" is a file name, not "C" + a fragment.
        ExportInput in;
        in.files = {iotest::text(u"C#.md"_s, u"język"_s),
                    iotest::text(u"A.md"_s, u"[C#](C%23.md) [x](C%23.md#Sekcja)"_s)};
        const QList<FileEntry> entries = readZip(exportWith(u"html-site"_s, in).data);
        const QString a = textOf(entries, u"Brulion/A.html"_s);
        QVERIFY2(a.contains(u"<a class=\"internal\" href=\"C%23.html\">C#</a>"_s), qPrintable(a));
        QVERIFY2(a.contains(u"href=\"C%23.html#sekcja\""_s), qPrintable(a));
    }

    void htmlSingleActiveAttachments()
    {
        // Attachments become data: URIs; an HTML attachment must never be a data:text/html
        // document (scripts) — it is offered as a download instead.
        ExportInput in;
        in.files = {
                iotest::text(u"A.md"_s, u"[[assets/strona.html]] ![[assets/strona.html]] ![[assets/rysunek.svg]]"_s),
                iotest::bytes(u"assets/strona.html"_s, "<script>alert(1)</script>"),
                iotest::bytes(u"assets/rysunek.svg"_s, "<svg xmlns='http://www.w3.org/2000/svg'/>")};
        const QString html = QString::fromUtf8(exportWith(u"html-single"_s, in).data);
        QVERIFY(!html.contains(u"data:text/html"_s));
        QCOMPARE(html.count(u"href=\"data:application/octet-stream;base64,"_s), 2);
        QVERIFY(html.contains(u"<img src=\"data:image/svg+xml;base64,"_s)); // scripts never run inside <img>
    }

    void json()
    {
        const ExportedFile f = exportWith(u"json"_s);
        QCOMPARE(f.mime, u"application/json"_s);
        QJsonParseError error{};
        const QJsonObject root = QJsonDocument::fromJson(f.data, &error).object();
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(root.value(u"format"_s).toString(), u"brulion-export"_s);
        QCOMPARE(root.value(u"version"_s).toInt(), 1);
        QCOMPARE(root.value(u"vault"_s).toString(), u"Mój skarbiec"_s);
        QCOMPARE(root.value(u"exportedAt"_s).toString(), u"2026-10-15T12:00:00+02:00"_s);
        QCOMPARE(root.value(u"config"_s).toObject().value(u"journalsFolder"_s).toString(), u"journals"_s);
        const QJsonArray notes = root.value(u"notes"_s).toArray();
        QCOMPARE(notes.size(), 4);
        QJsonObject dom;
        for (const QJsonValue &v : notes) {
            if (v.toObject().value(u"path"_s).toString() == u"Projekty/Dom.md")
                dom = v.toObject();
        }
        QCOMPARE(dom.value(u"content"_s).toString(), Dom);
        QCOMPARE(dom.value(u"aliases"_s).toArray().first().toString(), u"Remont"_s);
        const QJsonObject task = dom.value(u"tasks"_s).toArray().first().toObject();
        QCOMPARE(task.value(u"status"_s).toString(), u"todo"_s);
        QCOMPARE(task.value(u"due"_s).toString(), u"2026-10-20"_s);
        QCOMPARE(task.value(u"scheduled"_s).toString(), u"2026-10-18 10:00"_s);
        QCOMPARE(task.value(u"priority"_s).toString(), u"high"_s);
        QCOMPARE(task.value(u"recurrence"_s).toString(), u"every week"_s);
        bool resolvedAttachment = false;
        for (const QJsonValue &l : dom.value(u"links"_s).toArray())
            resolvedAttachment |= l.toObject().value(u"resolved"_s).toString() == u"assets/salon.png";
        QVERIFY(resolvedAttachment);
        const QJsonArray attachments = root.value(u"attachments"_s).toArray();
        QCOMPARE(attachments.size(), 2);
        QCOMPARE(QByteArray::fromBase64(attachments.at(1).toObject().value(u"data"_s).toString().toLatin1()),
                 QByteArray("\x89PNG-salon"));
    }

    void ics()
    {
        const ExportedFile f = exportWith(u"ics"_s);
        // CRLF line endings, folded to 75 octets.
        for (const QByteArray &line : f.data.split('\n')) {
            if (line.isEmpty())
                continue;
            QVERIFY2(line.endsWith('\r'), line.constData());
            QVERIFY(line.size() <= 76);
        }
        QVERIFY(f.data.endsWith("END:VCALENDAR\r\n"));
        const QList<ical::Component> roots = ical::parse(f.data);
        QCOMPARE(roots.size(), 1);
        const ical::Component &cal = roots.first();
        QCOMPARE(cal.text(u"X-WR-CALNAME"), u"Mój skarbiec"_s);
        QMap<QString, const ical::Component *> byUid;
        for (const ical::Component &c : cal.children) {
            QCOMPARE(c.property(u"DTSTAMP")->value, u"20261015T100000Z"_s);
            byUid.insert(c.text(u"UID"), &c);
        }
        const ical::Component *farba = byUid.value(u"farba-task@brulion"_s);
        QVERIFY(farba);
        QCOMPARE(farba->name, u"VTODO"_s);
        QCOMPARE(farba->text(u"SUMMARY"), u"Kup farbę #zakupy"_s);
        QCOMPARE(farba->property(u"DUE")->value, u"20261020"_s);
        QCOMPARE(farba->property(u"DUE")->param(u"VALUE"_s), u"DATE"_s);
        QCOMPARE(farba->property(u"PRIORITY")->value, u"1"_s);
        QCOMPARE(farba->property(u"RRULE")->value, u"FREQ=WEEKLY"_s);
        QCOMPARE(farba->property(u"STATUS")->value, u"NEEDS-ACTION"_s);
        QCOMPARE(farba->text(u"CATEGORIES"), u"zakupy"_s);
        const ical::Component *scheduled = byUid.value(u"farba-scheduled@brulion"_s);
        QVERIFY(scheduled);
        QCOMPARE(scheduled->name, u"VEVENT"_s);
        QCOMPARE(scheduled->property(u"DTSTART")->value, u"20261018T100000"_s);
        QCOMPARE(scheduled->property(u"DURATION")->value, u"PT30M"_s);
        // Reminders: a VEVENT with a VALARM; the alarm (⏰!) gets an audio alarm too.
        int reminders = 0;
        int audio = 0;
        for (const ical::Component &c : cal.children) {
            if (!c.text(u"UID").contains(u"-reminder@"_s))
                continue;
            ++reminders;
            QCOMPARE(c.name, u"VEVENT"_s);
            QVERIFY(!c.childrenNamed(u"VALARM").isEmpty());
            for (const ical::Component *a : c.childrenNamed(u"VALARM"))
                audio += a->text(u"ACTION") == u"AUDIO" ? 1 : 0;
            if (c.text(u"SUMMARY") == u"Rano: zadzwonić")
                QCOMPARE(c.property(u"DTSTART")->value, u"20261014T073000"_s);
        }
        QCOMPARE(reminders, 2);
        QCOMPARE(audio, 1);
        // Done tasks without a due date are not exported; tasks with a due date are VTODOs.
        int todos = 0;
        for (const ical::Component &c : cal.children)
            todos += c.name == u"VTODO" ? 1 : 0;
        QCOMPARE(todos, 2); // Kup farbę, Pranie
        // Stable UIDs and output.
        QCOMPARE(exportWith(u"ics"_s).data, f.data);
    }

    void icsCategoriesAndText()
    {
        // CATEGORIES is a list: the commas between tags are separators, a comma inside a value is
        // escaped. TEXT never carries control characters.
        ExportInput in;
        in.files = {iotest::text(u"A.md"_s, u"- [ ] Zadanie\u0007 #dom #[[a, b]] #praca 📅 2026-10-20\n"_s)};
        const QList<ical::Component> roots = ical::parse(exportWith(u"ics"_s, in).data);
        QCOMPARE(roots.size(), 1);
        const QList<const ical::Component *> todos = roots.first().childrenNamed(u"VTODO");
        QCOMPARE(todos.size(), 1);
        const ical::Property *categories = todos.first()->property(u"CATEGORIES");
        QVERIFY(categories);
        QCOMPARE(categories->value, u"dom,a\\, b,praca"_s);
        QVERIFY(!todos.first()->text(u"SUMMARY").contains(QChar(7)));
    }

    void opml()
    {
        const ExportedFile f = exportWith(u"opml"_s);
        QXmlStreamReader x(f.data);
        QStringList path;  // texts of the open outlines
        QStringList found; // "parent > child" pairs of interest
        QStringList complete;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == u"outline") {
                const QString text = x.attributes().value(u"text"_s).toString();
                if (!path.isEmpty())
                    found.append(path.last() + u" > "_s + text);
                if (x.attributes().value(u"_complete"_s) == u"true")
                    complete.append(text);
                path.append(text);
            } else if (x.isEndElement() && x.name() == u"outline") {
                path.removeLast();
            }
        }
        QVERIFY2(!x.hasError(), qPrintable(x.errorString()));
        QVERIFY(found.contains(u"Dom > Dom"_s));          // page > heading
        QVERIFY(found.contains(u"Dom > Plan remontu"_s)); // heading > paragraph
        QVERIFY(found.contains(u"Dom > Kup farbę 📅 2026-10-20 ⏳ 2026-10-18 10:00 🔁 every week ⏫ #zakupy"_s));
        QVERIFY(found.contains(u"Malowanie > Tapeta"_s));
        QVERIFY(found.contains(u"2026-10-14 > Rano: zadzwonić ⏰! 2026-10-14 07:30"_s));
        QCOMPARE(complete, QStringList({u"Zmierz ściany ✅ 2026-10-10"_s, u"Tapeta"_s}));
    }

    void opmlControlCharacters()
    {
        // XML 1.0 cannot carry most control characters (nor U+FFFE/U+FFFF): the outline must stay
        // well-formed and complete.
        ExportInput in;
        in.files = {iotest::text(u"A.md"_s, u"- zły\u000Bznak\u0001 \uFFFF\u0000koniec\n- dalej\n"_s),
                    iotest::text(u"B.md"_s, u"- druga strona\n"_s)};
        const ExportedFile f = exportWith(u"opml"_s, in);
        QXmlStreamReader x(f.data);
        QStringList texts;
        while (!x.atEnd()) {
            x.readNext();
            if (x.isStartElement() && x.name() == u"outline")
                texts.append(x.attributes().value(u"text"_s).toString());
        }
        QVERIFY2(!x.hasError(), qPrintable(x.errorString()));
        QVERIFY(texts.contains(u"złyznak koniec"_s));
        QVERIFY(texts.contains(u"dalej"_s));
        QVERIFY(texts.contains(u"druga strona"_s));
    }

    void tasksCsvPathInjection()
    {
        // Paths come from the vault (any file name): they are neutralised like titles and text.
        ExportInput in;
        in.files = {iotest::text(u"@pliki/=SUMA.md"_s, u"- [ ] -zadanie\n"_s)};
        const QList<QStringList> rows = parseCsv(QString::fromUtf8(exportWith(u"tasks-csv"_s, in).data));
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[1].value(0), u"'=SUMA"_s);
        QCOMPARE(rows[1].value(1), u"'@pliki/=SUMA.md"_s);
        QCOMPARE(rows[1].value(4), u"'-zadanie"_s);
    }

    void tasksCsv()
    {
        const ExportedFile f = exportWith(u"tasks-csv"_s);
        QVERIFY(f.data.startsWith("\xEF\xBB\xBF"));
        const QString text = QString::fromUtf8(f.data);
        QVERIFY(text.contains(u"\r\n"_s));
        const QList<QStringList> rows = parseCsv(text);
        QCOMPARE(rows.first(), QStringList({u"Page"_s, u"Path"_s, u"Line"_s, u"Status"_s, u"Task"_s, u"Due"_s,
                                            u"Scheduled"_s, u"Start"_s, u"Created"_s, u"Done"_s, u"Cancelled"_s,
                                            u"Priority"_s, u"Recurrence"_s, u"Tags"_s}));
        QCOMPARE(rows.size(), 1 + 8);
        const QStringList farba = rows[1];
        QCOMPARE(farba, QStringList({u"Dom"_s, u"Projekty/Dom.md"_s, u"9"_s, u"todo"_s, u"Kup farbę #zakupy"_s,
                                     u"2026-10-20"_s, u"2026-10-18 10:00"_s, QString(), QString(), QString(), QString(),
                                     u"high"_s, u"every week"_s, u"#zakupy"_s}));
        bool injection = false;
        for (const QStringList &r : rows)
            injection |= r.value(4) == u"'=SUM(A1) podejrzane";
        QVERIFY(injection);
    }
};

QTEST_MAIN(TestIoExport)
#include "tst_io_export.moc"
