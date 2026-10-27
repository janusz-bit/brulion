// The Pandoc Markdown exporter: one document with a section per note, links as identifiers,
// Brulion's syntax translated, text escaped — and, when pandoc is installed, what pandoc reads.
#include "iotest.h"

#include "io/exporter.h"
#include "io/pandocrender_p.h"
#include "io/zip.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

const QString Dom = u"---\n"
                    u"tags: [dom, remont]\n"
                    u"---\n"
                    u"# Dom\n"
                    u"\n"
                    u"Plan remontu ^plan\n"
                    u"\n"
                    u"## Zakupy na już\n"
                    u"\n"
                    u"- [ ] Kup farbę 📅 2026-10-20 ⏳ 2026-10-18 10:00 🔁 every week ⏫ #zakupy ^farba\n"
                    u"- [x] Zmierz ściany ✅ 2026-10-10\n"
                    u"- [/] Malowanie\n"
                    u"\t- [-] Tapeta\n"
                    u"- [>] Meble\n"
                    u"\n"
                    u"1. pierwszy\n"
                    u"2. drugi\n"
                    u"\n"
                    u"```js\n"
                    u"let x = 1; // [[nie link]]\n"
                    u"```\n"
                    u"\n"
                    u"| Pokój | Kolor |\n"
                    u"| --- | :-: |\n"
                    u"| Salon \\| hol | =SUM(A1) |\n"
                    u"\n"
                    u"> [!warning] Uwaga\n"
                    u"> Pamiętaj o ((farba))\n"
                    u"\n"
                    u"Zdjęcie: ![[assets/salon.png]] i [plan](../assets/plan.pdf)\n"
                    u"⏰ 2026-10-16 09:00 Zadzwoń do fachowca\n"_s;

const QString Zakupy = u"- Lista na [[Dom]] i [[Projekty/Dom#^farba|farba]], [[Dom#Zakupy na już]]\n"
                       u"- ![[Dom#^plan]]\n"
                       u"- Odnośnik do ((farba)) i [[Nie ma takiej]]\n"
                       u"- TODO Logseq marker\n"
                       u"  SCHEDULED: <2026-10-21 Wed>\n"
                       u"- [[2026-10-14]] notatka\n"_s;

const QString Wzory = u"Pole koła: $\\pi r^2$, a $$\\frac{a}{b}$$ też.\n"
                      u"\n"
                      u"```math\n"
                      u"\\sum_{i=1}^{n} i\n"
                      u"\n"
                      u"= \\frac{n(n+1)}{2}\n"
                      u"```\n"
                      u"\n"
                      u"- ```math\n"
                      u"  e^{i\\pi} + 1 = 0\n"
                      u"  ```\n"
                      u"- **gruby** i *kursywa* ==zaznaczenie== ~~skreślenie~~ `kod`\n"
                      u"\n"
                      u"Przypis[^1].\n"
                      u"\n"
                      u"[^1]: Treść przypisu.\n"_s;

const QString Unsafe =
        u"<script>alert(1)</script>\n"
        u"\n"
        u"Tekst <b>pogrubiony</b> [x](javascript:alert(1)) & \"cudzysłów\" @jan $5 i $6 x^2 a~b *gwiazdka\n"
        u"# nie nagłówek, bo w akapicie? \n"
        u"linia\n"
        u"- myślnik\n"
        u"1. jeden\n"
        u": dwukropek\n"_s;

const QString Journal = u"- Rano: zadzwonić ⏰! 2026-10-14 07:30\n"
                        u"- ![[Zakupy]]\n"_s;

ExportInput fixture()
{
    ExportInput in;
    in.files = {
            iotest::text(u"Projekty/Dom.md"_s, Dom),
            iotest::text(u"Zakupy.md"_s, Zakupy),
            iotest::text(u"Wzory.md"_s, Wzory),
            iotest::text(u"Bezpieczeństwo.md"_s, Unsafe),
            iotest::text(u"journals/2026-10-14.md"_s, Journal),
            iotest::bytes(u"assets/salon.png"_s, "\x89PNG-salon"),
            iotest::bytes(u"assets/plan.pdf"_s, "%PDF-plan"),
            iotest::bytes(u"assets/nieużywany.png"_s, "\x89PNG-unused"),
            iotest::text(u".brulion/config.json"_s, u"{\"version\": 1}"_s),
    };
    in.vaultName = u"Mój skarbiec"_s;
    in.now = QDateTime(QDate(2026, 10, 15), QTime(12, 0), iotest::warsaw());
    return in;
}

ExportedFile exportPandoc(const ExportInput &input = fixture())
{
    const ExportResult r = runExport(*exporterById(u"pandoc"), input);
    Q_ASSERT(r.files.size() == 1);
    return r.files.first();
}

/// The document of an export (the .md in the archive folder).
QString documentOf(const ExportedFile &f, const QString &folder = u"Mój skarbiec"_s)
{
    return QString::fromUtf8(iotest::entryData(readZip(f.data), folder + u'/' + folder + u".md"_s));
}

/// Ids and internal link targets of a pandoc JSON AST, and the node types in it.
struct Ast {
    QSet<QString> ids;
    QStringList targets;
    QSet<QString> types;
};

void walk(const QJsonValue &v, Ast &ast)
{
    if (v.isArray()) {
        for (const QJsonValue &e : v.toArray())
            walk(e, ast);
        return;
    }
    if (!v.isObject())
        return;
    const QJsonObject o = v.toObject();
    const QString t = o.value(u"t").toString();
    const QJsonArray c = o.value(u"c").toArray();
    if (!t.isEmpty())
        ast.types.insert(t);
    const auto id = [&](const QJsonValue &attr) {
        const QString s = attr.toArray().at(0).toString();
        if (!s.isEmpty())
            ast.ids.insert(s);
    };
    if (t == u"Header")
        id(c.at(1));
    else if (t == u"Span" || t == u"Div" || t == u"CodeBlock")
        id(c.at(0));
    else if (t == u"Link") {
        id(c.at(0));
        const QString url = c.at(2).toArray().at(0).toString();
        if (url.startsWith(u'#'))
            ast.targets.append(url.sliced(1));
    }
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        walk(it.value(), ast);
}

QString inlineOf(QStringView markdown)
{
    return detail::PandocRenderer().renderInline(markdown);
}

} // namespace

class TestIoPandoc : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void archive()
    {
        const ExportedFile f = exportPandoc();
        QCOMPARE(f.name, u"Mój skarbiec-2026-10-15-pandoc.zip"_s);
        QCOMPARE(f.mime, u"application/zip"_s);
        const QList<FileEntry> entries = readZip(f.data);
        // The document and the attachments it links to — not the unused picture, not the config.
        QCOMPARE(iotest::entryPaths(entries),
                 QStringList({u"Mój skarbiec/Mój skarbiec.md"_s, u"Mój skarbiec/assets/plan.pdf"_s,
                              u"Mój skarbiec/assets/salon.png"_s}));
        QCOMPARE(iotest::entryData(entries, u"Mój skarbiec/assets/salon.png"_s), QByteArray("\x89PNG-salon"));
        QCOMPARE(exportPandoc().data, f.data); // deterministic
        // An empty notebook is a document with a title only.
        const QString empty =
                documentOf(runExport(*exporterById(u"pandoc"), ExportInput()).files.first(), u"Brulion"_s);
        QCOMPARE(empty, u"---\ntitle: 'Brulion'\n---\n"_s);
    }

    void sections()
    {
        const QString doc = documentOf(exportPandoc());
        QVERIFY2(doc.startsWith(u"---\ntitle: 'Mój skarbiec'\ndate: '2026-10-15'\n---\n\n# Bezpieczeństwo {#n1}\n"_s),
                 qPrintable(doc));
        // Pages by path, then journals; the note's own "# Dom" is its section heading, once.
        QStringList headings;
        for (const QString &line : doc.split(u'\n')) {
            if (line.startsWith(u"# "))
                headings.append(line);
        }
        QCOMPARE(headings, QStringList({u"# Bezpieczeństwo {#n1}"_s, u"# Dom {#n2}"_s, u"# Wzory {#n3}"_s,
                                        u"# Zakupy {#n4}"_s, u"# 2026-10-14 {#n5}"_s}));
        QVERIFY(!doc.contains(u"## Dom"_s));
        // The note's headings one level down, tags under the title (a tag naming a page links to it).
        QVERIFY(doc.contains(u"\n### Zakupy na już {#n2-zakupy-na-już}\n"_s));
        QVERIFY(doc.contains(u"# Dom {#n2}\n\n[#dom](#n2){.tag} [#remont]{.tag} [#zakupy](#n4){.tag}\n\n"
                             u"[]{#n2-block-plan}Plan remontu\n"_s));
    }

    void links()
    {
        const QString doc = documentOf(exportPandoc());
        QVERIFY2(doc.contains(u"- Lista na [Dom](#n2) i [farba](#n2-block-farba), "
                              u"[Dom > Zakupy na już](#n2-zakupy-na-już)\n"_s),
                 qPrintable(doc));
        // Block references show the block's text; a missing page is its name.
        QVERIFY(doc.contains(u"- Odnośnik do [Kup farbę \\#zakupy](#n2-block-farba) i [Nie ma takiej]{.missing}\n"_s));
        QVERIFY(doc.contains(u"- [2026-10-14](#n5) notatka\n"_s));
        // Attachments relative to the document; pictures as images.
        QVERIFY(doc.contains(u"Zdjęcie: ![salon.png](assets/salon.png) i [plan](assets/plan.pdf)\\\n"
                             u"Zadzwoń do fachowca [reminder: 2026-10-16 09:00]{.reminder}\n"_s));
    }

    void embeds()
    {
        const QString doc = documentOf(exportPandoc());
        // A block embed and a page embed: a copy without identifiers, embeds inside it as links.
        QVERIFY2(doc.contains(u"- ::: {.embed}\n  **[Dom](#n2-block-plan)**\n\n  Plan remontu\n  :::\n"_s),
                 qPrintable(doc));
        QVERIFY(doc.contains(
                u"- ::: {.embed}\n  **[Zakupy](#n4)**\n\n"
                u"  - Lista na [Dom](#n2) i [farba](#n2-block-farba), [Dom > Zakupy na już](#n2-zakupy-na-już)\n"
                u"  - [Dom](#n2-block-plan)\n"_s));
        QCOMPARE(doc.count(u"{#n2-block-farba}"_s), 1);
    }

    void tasks()
    {
        const QString doc = documentOf(exportPandoc());
        QVERIFY2(doc.contains(u"- [ ] []{#n2-block-farba}Kup farbę [#zakupy](#n4){.tag} [due: 2026-10-20]{.due} "
                              u"[scheduled: 2026-10-18 10:00]{.scheduled} [repeats: every week]{.recurrence} "
                              u"[priority: high]{.priority}\n"
                              u"- [x] Zmierz ściany [done: 2026-10-10]{.done}\n"
                              u"- [ ] Malowanie [in progress]{.status}\n"
                              u"  - [x] ~~Tapeta~~\n"
                              u"- [ ] Meble [deferred]{.status}\n\n"
                              u"1. pierwszy\n2. drugi\n"_s),
                 qPrintable(doc));
        // Logseq markers and planning lines, reminders and alarms.
        QVERIFY(doc.contains(u"- [ ] Logseq marker [scheduled: 2026-10-21]{.scheduled}\n"_s));
        QVERIFY(doc.contains(u"- Rano: zadzwonić [alarm: 2026-10-14 07:30]{.alarm}\n"_s));
        QVERIFY(!doc.contains(u"📅"_s) && !doc.contains(u"⏰"_s) && !doc.contains(u"⏫"_s));
    }

    void blocks()
    {
        const QString doc = documentOf(exportPandoc());
        QVERIFY2(doc.contains(u"```js\nlet x = 1; // [[nie link]]\n```\n"_s), qPrintable(doc));
        QVERIFY(doc.contains(u"| Pokój | Kolor |\n|---|:-:|\n| Salon \\| hol | `=SUM(A1)` |\n"_s));
        QVERIFY(doc.contains(u"::: {.callout .callout-warning}\n**Uwaga**\n\n"
                             u"Pamiętaj o [Kup farbę \\#zakupy](#n2-block-farba)\n:::\n"_s));
        // Raw HTML is text (a code block), as in the app.
        QVERIFY(doc.contains(u"```\n<script>alert(1)</script>\n```\n"_s));
    }

    void math()
    {
        const QString doc = documentOf(exportPandoc());
        QVERIFY2(doc.contains(u"Pole koła: $\\pi r^2$, a $$\\frac{a}{b}$$ też.\n"_s), qPrintable(doc));
        // ```math as display math (blank lines dropped), also in a list item.
        QVERIFY(doc.contains(u"\n$$\n\\sum_{i=1}^{n} i\n= \\frac{n(n+1)}{2}\n$$\n"_s));
        QVERIFY(doc.contains(u"- $$\n  e^{i\\pi} + 1 = 0\n  $$\n"_s));
        QVERIFY(doc.contains(u"- **gruby** i *kursywa* [zaznaczenie]{.mark} ~~skreślenie~~ `kod`\n"_s));
    }

    void footnotes()
    {
        const QString doc = documentOf(exportPandoc());
        // Labels per note: two notes may both have [^1].
        QVERIFY2(doc.contains(u"Przypis[^n3-1].\n\n[^n3-1]: Treść przypisu.\n"_s), qPrintable(doc));
    }

    void escaping()
    {
        const QString doc = documentOf(exportPandoc());
        QVERIFY2(doc.contains(u"Tekst \\<b>pogrubiony\\</b> \\[x\\](javascript:alert(1)) \\& \"cudzysłów\" \\@jan "
                              u"\\$5 i \\$6 x\\^2 a\\~b \\*gwiazdka\n"_s),
                 qPrintable(doc));
    }

    void singlePage()
    {
        ExportInput one;
        one.files = {iotest::text(u"Projekty/Dom.md"_s, Dom), iotest::bytes(u"assets/salon.png"_s, "\x89PNG")};
        one.vaultName = u"Dom"_s;
        const ExportedFile f = exportPandoc(one);
        const QString doc = documentOf(f, u"Dom"_s);
        // The page is the document: its title as the title, headings as written, no own heading.
        QVERIFY2(doc.startsWith(u"---\ntitle: 'Dom'\n---\n\n[#dom]{.tag} [#remont]{.tag} [#zakupy]{.tag}\n\n"
                                u"[]{#n1-block-plan}Plan remontu\n\n## Zakupy na już {#n1-zakupy-na-już}\n"_s),
                 qPrintable(doc));
        QVERIFY(doc.contains(u"Pamiętaj o [Kup farbę \\#zakupy](#n1-block-farba)"_s));
        QCOMPARE(iotest::entryPaths(readZip(f.data)), QStringList({u"Dom/Dom.md"_s, u"Dom/assets/salon.png"_s}));
    }

    void hostileNames()
    {
        ExportInput in;
        in.files = {iotest::text(u"Strona.md"_s, u"![[javascript:x.png]] ![[a b (1).png]]\n"_s),
                    iotest::bytes(u"javascript:x.png"_s, "x"), iotest::bytes(u"a b (1).png"_s, "y")};
        in.vaultName = u"Notatki 'Ani' #1"_s;
        const ExportedFile f = exportPandoc(in);
        const QList<FileEntry> entries = readZip(f.data);
        const QString folder = iotest::entryPaths(entries).first().section(u'/', 0, 0);
        const QString doc = QString::fromUtf8(iotest::entryData(entries, folder + u'/' + folder + u".md"_s));
        QVERIFY2(doc.contains(u"![javascript:x.png](./javascript:x.png) ![a b (1).png](a%20b%20%281%29.png)"_s),
                 qPrintable(doc));
        QVERIFY(doc.startsWith(u"---\ntitle: 'Strona'\n"_s));
        ExportInput two = in;
        two.files.append(iotest::text(u"Druga.md"_s, u"x"_s));
        QVERIFY(documentOf(exportPandoc(two), folder).startsWith(u"---\ntitle: 'Notatki ''Ani'' \\#1'\n"_s));
    }

    void inlineText_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("pandoc");
        QTest::newRow("plain") << u"Zażółć gęślą jaźń"_s << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("specials") << u"a*b_c `x` <y> [z] @w $1 ^ ~ & # |"_s
                                  << u"a\\*b\\_c `x` \\<y> \\[z\\] \\@w \\$1 \\^ \\~ \\& \\# \\|"_s;
        QTest::newRow("emphasis") << u"**mocno** *lekko* __też__ ~~nie~~ ==to=="_s
                                  << u"**mocno** *lekko* **też** ~~nie~~ [to]{.mark}"_s;
        QTest::newRow("unpaired") << u"2 * 3 = 6, ==sam"_s << u"2 \\* 3 = 6, ==sam"_s;
        QTest::newRow("line breaks") << u"pierwsza\ndruga\n"_s << u"pierwsza\\\ndruga"_s;
        QTest::newRow("line starts") << u"a\n# b\n> c\n- d\n12. e\n: f\n=== g"_s
                                     << u"a\\\n\\# b\\\n\\> c\\\n\\- d\\\n12\\. e\\\n\\: f\\\n\\=== g"_s;
        QTest::newRow("code with backticks") << u"`` a`b ``"_s << u"``a`b``"_s;
        QTest::newRow("code fence padding") << u"`` `x ``"_s << u"`` `x ``"_s;
        QTest::newRow("comment hidden") << u"widać %%ukryte%% dalej"_s << u"widać  dalej"_s;
        QTest::newRow("escape kept") << u"\\*nie kursywa\\*"_s << u"\\*nie kursywa\\*"_s;
        QTest::newRow("url") << u"zobacz https://example.com/a_b i <https://x.pl>"_s
                             << u"zobacz <https://example.com/a_b> i <https://x.pl>"_s;
        QTest::newRow("external link") << u"[strona **x**](https://example.com/a_b)"_s
                                       << u"[strona **x**](https://example.com/a_b)"_s;
        QTest::newRow("unsafe link") << u"[x](javascript:alert(1))"_s << u"\\[x\\](javascript:alert(1))"_s;
        QTest::newRow("image url") << u"![logo](https://example.com/l.png)"_s
                                   << u"![logo](https://example.com/l.png)"_s;
        QTest::newRow("embedded picture url") << u"![[https://example.com/kot.jpg]]"_s
                                              << u"![](https://example.com/kot.jpg)"_s;
        QTest::newRow("embedded picture url with alias") << u"![[https://example.com/kot.jpg|Mój kot]]"_s
                                                         << u"![Mój kot](https://example.com/kot.jpg)"_s;
        QTest::newRow("embedded picture url without extension")
                << u"![[https://example.com/zdjęcie?id=5]]"_s << u"![](https://example.com/zdjęcie?id=5)"_s;
        QTest::newRow("tag") << u"zrób #zakupy/spożywcze"_s << u"zrób [#zakupy/spożywcze]{.tag}"_s;
        QTest::newRow("math") << u"$x_1 + y$ i $$\\int f$$"_s << u"$x_1 + y$ i $$\\int f$$"_s;
        QTest::newRow("tokens") << u"📅 2026-10-20 🛫 2026-10-01 ❌ 2026-10-02 🔺"_s
                                << u"[due: 2026-10-20]{.due} [starts: 2026-10-01]{.start} "
                                   u"[cancelled: 2026-10-02]{.cancelled} [priority: highest]{.priority}"_s;
        QTest::newRow("block id hidden") << u"tekst ^abc123"_s << u"tekst"_s;
        QTest::newRow("footnote ref") << u"x[^a]"_s << u"x[^a]"_s;
        QTest::newRow("missing wiki link") << u"[[Strona|inna nazwa]]"_s << u"[inna nazwa]{.missing}"_s;
    }

    void inlineText()
    {
        QFETCH(QString, markdown);
        QFETCH(QString, pandoc);
        QCOMPARE(inlineOf(markdown).trimmed(), pandoc);
    }

    // `![[javascript:…]]` and `![[data:…]]` name files (which do not exist), never a URL.
    void embeddedPictureSchemes_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::newRow("javascript") << u"![[javascript:alert(1).png]]"_s;
        QTest::newRow("data") << u"![[data:image/png;base64,AAAA]]"_s;
        QTest::newRow("ftp") << u"![[ftp://example.com/a.png]]"_s;
    }
    void embeddedPictureSchemes()
    {
        QFETCH(QString, markdown);
        const QString out = inlineOf(markdown);
        QVERIFY2(!out.contains(u"]("_s), qPrintable(out));
        QVERIFY2(out.contains(u"{.missing}"_s), qPrintable(out));
    }

    // "Obrazy z internetu: dołącz kopie": the copies are files of the archive, linked relatively;
    // pictures without a copy keep their URL.
    void webPictureCopies()
    {
        ExportInput in;
        in.files = {iotest::text(u"Strona.md"_s,
                                 u"![kot](https://a.pl/x/kot.jpg) ![[https://b.pl/y/kot.jpg|drugi]]\n\n"
                                 u"![łąka](https://c.pl/Zdj%C4%99cie%20%C5%82%C4%85ki.jpg) "
                                 u"![rysunek](https://d.pl/rysunek?id=1) ![bez kopii](https://e.pl/e.png)\n\n"
                                 u"```\n![kod](https://a.pl/x/kot.jpg)\n```\n"_s),
                    iotest::bytes(u"assets/web/kot.jpg"_s, "local-kot")};
        in.vaultName = u"Notatki"_s;
        in.webPictures = {
                {u"https://a.pl/x/kot.jpg"_s, {QByteArray("JPEG-a"), u"jpg"_s, u"kot.jpg"_s}},
                {u"https://b.pl/y/kot.jpg"_s, {QByteArray("JPEG-b"), u"jpg"_s, u"kot.jpg"_s}},
                {u"https://c.pl/Zdj%C4%99cie%20%C5%82%C4%85ki.jpg"_s,
                 {QByteArray("JPEG-c"), u"jpg"_s, u"Zdjęcie łąki.jpg"_s}},
                {u"https://d.pl/rysunek?id=1"_s, {QByteArray("<svg/>"), u"svg"_s, u"rysunek.svg"_s}},
                {u"https://f.pl/nieużywany.png"_s, {QByteArray("PNG-f"), u"png"_s, u"nieużywany.png"_s}},
                {u"https://e.pl/e.png"_s, {QByteArray(), u"png"_s, u"e.png"_s}}, // no bytes: no copy
        };
        const QList<FileEntry> entries = readZip(exportPandoc(in).data);
        const QString doc = QString::fromUtf8(iotest::entryData(entries, u"Notatki/Notatki.md"_s));
        QCOMPARE(iotest::entryData(entries, u"Notatki/assets/web/kot 2.jpg"_s), QByteArray("JPEG-a"));
        QCOMPARE(iotest::entryData(entries, u"Notatki/assets/web/kot 3.jpg"_s), QByteArray("JPEG-b"));
        QCOMPARE(iotest::entryData(entries, u"Notatki/assets/web/Zdjęcie łąki.jpg"_s), QByteArray("JPEG-c"));
        QCOMPARE(iotest::entryData(entries, u"Notatki/assets/web/rysunek.svg"_s), QByteArray("<svg/>"));
        QVERIFY2(doc.contains(u"![kot](assets/web/kot%202.jpg) ![drugi](assets/web/kot%203.jpg)"_s), qPrintable(doc));
        QVERIFY2(doc.contains(u"![łąka](assets/web/Zdjęcie%20łąki.jpg)"_s), qPrintable(doc));
        QVERIFY2(doc.contains(u"![rysunek](assets/web/rysunek.svg)"_s), qPrintable(doc));
        QVERIFY2(doc.contains(u"![bez kopii](https://e.pl/e.png)"_s), qPrintable(doc));
        QVERIFY2(doc.contains(u"![kod](https://a.pl/x/kot.jpg)"_s), qPrintable(doc)); // code stays code
        const QStringList paths = iotest::entryPaths(entries);
        QVERIFY2(!paths.join(u'\n').contains(u"nieużywany"_s), qPrintable(paths.join(u", "_s)));
        QVERIFY(!paths.contains(u"Notatki/assets/web/e.png"_s));

        // Without copies every picture keeps its URL.
        in.webPictures.clear();
        const QString plain = documentOf(exportPandoc(in), u"Notatki"_s);
        QVERIFY2(plain.contains(u"![kot](https://a.pl/x/kot.jpg) ![drugi](https://b.pl/y/kot.jpg)"_s), qPrintable(plain));
    }

    void malformed_data()
    {
        QTest::addColumn<QString>("note");
        QTest::newRow("unclosed fence") << u"```js\nlet x\n"_s;
        QTest::newRow("unclosed math") << u"$$\\frac{a}{b\n\n- ```math\n"_s;
        QTest::newRow("deep nesting") << u"- a\n\t- b\n\t\t- c\n\t\t\t\t\t\t\t\t- d\n"_s;
        QTest::newRow("empty items") << u"- \n- [ ] \n  - \n"_s;
        QTest::newRow("broken table") << u"| a |\n| b | c | d |\n"_s;
        QTest::newRow("only frontmatter") << u"---\ntags: [x\n---\n"_s;
        QTest::newRow("self embed") << u"![[Strona]]\n![[Strona#^nie]]\n((nie))\n"_s;
        QTest::newRow("control chars") << u"a\u0001b\u0000c\n"_s;
    }

    void malformed()
    {
        QFETCH(QString, note);
        ExportInput in;
        in.files = {iotest::text(u"Strona.md"_s, note), iotest::text(u"Inna.md"_s, u"![[Strona]]"_s)};
        const ExportResult r = runExport(*exporterById(u"pandoc"), in);
        QCOMPARE(r.files.size(), 1);
        QVERIFY(documentOf(r.files.first(), u"Brulion"_s).startsWith(u"---\ntitle: 'Brulion'\n---\n"_s));
    }

    // What pandoc itself reads (when it is installed): no raw HTML, and every internal link
    // leads to an identifier of the document.
    void throughPandoc()
    {
        const QString pandoc = QStandardPaths::findExecutable(u"pandoc"_s);
        if (pandoc.isEmpty())
            QSKIP("pandoc is not installed");
        // Files, not pipes: pandoc's output does not fit a pipe's buffer.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile in(dir.filePath(u"doc.md"_s));
        QVERIFY(in.open(QIODevice::WriteOnly));
        in.write(documentOf(exportPandoc()).toUtf8());
        in.close();
        QProcess p;
        p.start(pandoc,
                {u"-f"_s, u"markdown"_s, u"-t"_s, u"json"_s, u"-o"_s, dir.filePath(u"doc.json"_s), in.fileName()});
        QVERIFY(p.waitForFinished(60000));
        QVERIFY2(p.exitCode() == 0, p.readAllStandardError().constData());
        QFile out(dir.filePath(u"doc.json"_s));
        QVERIFY(out.open(QIODevice::ReadOnly));
        const QJsonDocument json = QJsonDocument::fromJson(out.readAll());
        QVERIFY(json.isObject());
        Ast ast;
        walk(json.object().value(u"blocks"), ast);
        QVERIFY2(!ast.types.contains(u"RawInline"_s) && !ast.types.contains(u"RawBlock"_s),
                 qPrintable(QStringList(ast.types.begin(), ast.types.end()).join(u", "_s)));
        QVERIFY(!ast.types.contains(u"Cite"_s));
        QVERIFY(!ast.types.contains(u"Superscript"_s) && !ast.types.contains(u"Subscript"_s));
        for (const QString &t : {u"Math"_s, u"Table"_s, u"Note"_s, u"Strikeout"_s, u"Div"_s, u"Image"_s})
            QVERIFY2(ast.types.contains(t), qPrintable(t));
        QVERIFY(ast.targets.size() > 10);
        for (const QString &target : std::as_const(ast.targets))
            QVERIFY2(ast.ids.contains(target), qPrintable(target));
        const QString meta = QString::fromUtf8(QJsonDocument(json.object().value(u"meta").toObject()).toJson());
        QVERIFY2(meta.contains(u"skarbiec"_s), qPrintable(meta));
    }
};

QTEST_MAIN(TestIoPandoc)
#include "tst_io_pandoc.moc"
