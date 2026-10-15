// Logseq syntax helpers (journal titles, page names, block conversion, UUIDs, timestamps) and the
// Logseq importer.
#include "iotest.h"

#include "io/importer.h"
#include "io/logseq_p.h"
#include "io/zip.h"
#include "markdown/frontmatter.h"

#include <QRegularExpression>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace brulion::io::detail;
using namespace Qt::StringLiterals;

namespace {

QString bodyOf(const QString &content)
{
    return content.sliced(markdown::parseFrontmatter(content).bodyStart);
}

QVariantMap frontmatterOf(const QString &content)
{
    return markdown::parseFrontmatter(content).data;
}

const QString Uuid = u"6523f6a0-1234-4abc-9def-0123456789ab"_s;

} // namespace

class TestIoLogseq : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void journalTitles_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QDate>("date"); // invalid = no match
        QTest::addColumn<QString>("formatted");
        const QDate d(2026, 10, 14);
        QTest::newRow("logseq default") << u"MMM do, yyyy"_s << u"Oct 14th, 2026"_s << d << u"Oct 14th, 2026"_s;
        QTest::newRow("lower case") << u"MMM do, yyyy"_s << u"oct 14th, 2026"_s << d << u"Oct 14th, 2026"_s;
        QTest::newRow("1st") << u"MMM do, yyyy"_s << u"Jan 1st, 2026"_s << QDate(2026, 1, 1) << u"Jan 1st, 2026"_s;
        QTest::newRow("22nd") << u"MMM do, yyyy"_s << u"Mar 22nd, 2026"_s << QDate(2026, 3, 22) << u"Mar 22nd, 2026"_s;
        QTest::newRow("sept") << u"MMM do, yyyy"_s << u"Sept 3rd, 2026"_s << QDate(2026, 9, 3) << u"Sep 3rd, 2026"_s;
        QTest::newRow("no ordinal") << u"MMM do, yyyy"_s << u"Oct 14, 2026"_s << QDate() << QString();
        QTest::newRow("impossible") << u"MMM do, yyyy"_s << u"Feb 30th, 2026"_s << QDate() << QString();
        QTest::newRow("roam") << u"MMMM do, yyyy"_s << u"October 14th, 2026"_s << d << u"October 14th, 2026"_s;
        QTest::newRow("iso") << u"yyyy-MM-dd"_s << u"2026-10-14"_s << d << u"2026-10-14"_s;
        QTest::newRow("file name") << u"yyyy_MM_dd"_s << u"2026_10_14"_s << d << u"2026_10_14"_s;
        QTest::newRow("weekday") << u"EEE, dd.MM.yyyy"_s << u"Wed, 14.10.2026"_s << d << u"Wed, 14.10.2026"_s;
        QTest::newRow("long weekday") << u"EEEE, MMMM d, yyyy"_s << u"Wednesday, October 14, 2026"_s << d
                                      << u"Wednesday, October 14, 2026"_s;
        QTest::newRow("day first") << u"do MMMM yyyy"_s << u"14th October 2026"_s << d << u"14th October 2026"_s;
        QTest::newRow("us") << u"MM/dd/yyyy"_s << u"10/14/2026"_s << d << u"10/14/2026"_s;
        QTest::newRow("quoted literal") << u"'Dzień' yyyy-MM-dd"_s << u"Dzień 2026-10-14"_s << d
                                        << u"Dzień 2026-10-14"_s;
        QTest::newRow("trailing text") << u"yyyy-MM-dd"_s << u"2026-10-14 notatka"_s << QDate() << QString();
    }

    void journalTitles()
    {
        QFETCH(QString, format);
        QFETCH(QString, title);
        QFETCH(QDate, date);
        QFETCH(QString, formatted);
        const JournalTitleFormat f(format);
        QCOMPARE(f.parse(title).value_or(QDate()), date);
        if (date.isValid())
            QCOMPARE(f.format(date), formatted);
    }

    void parseJournalTitleFallback()
    {
        const QList<JournalTitleFormat> formats = {JournalTitleFormat(u"MMM do, yyyy"_s)};
        QCOMPARE(parseJournalTitle(u"Oct 14th, 2026"_s, formats), std::optional<QDate>(QDate(2026, 10, 14)));
        QCOMPARE(parseJournalTitle(u"2026-10-15"_s, formats), std::optional<QDate>(QDate(2026, 10, 15)));
        QVERIFY(!parseJournalTitle(u"Projekty"_s, formats));
    }

    void pageNames_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("page");
        QTest::newRow("plain") << u"Zażółć.md"_s << u"Zażółć"_s;
        QTest::newRow("namespace") << u"Projekty___Dom___Kuchnia.md"_s << u"Projekty/Dom/Kuchnia"_s;
        QTest::newRow("legacy slash") << u"a%2Fb.md"_s << u"a/b"_s;
        QTest::newRow("question") << u"What%3F.md"_s << u"What?"_s;
        QTest::newRow("percent") << u"100%25 done.md"_s << u"100% done"_s;
        QTest::newRow("invalid escape") << u"bad%ZZ.md"_s << u"bad%ZZ"_s;
        QTest::newRow("org") << u"notatka.org"_s << u"notatka"_s;
    }

    void pageNames()
    {
        QFETCH(QString, file);
        QFETCH(QString, page);
        QCOMPARE(logseqPageNameFromFile(file), page);
    }

    void fileNames_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("file");
        QTest::newRow("namespace") << u"a/b"_s << u"a___b"_s;
        QTest::newRow("question") << u"What?"_s << u"What%3F"_s;
        QTest::newRow("percent") << u"100% done"_s << u"100%25 done"_s;
        QTest::newRow("hidden") << u".hidden"_s << u"%2Ehidden"_s;
        QTest::newRow("polish colon") << u"Zażółć: gęślą"_s << u"Zażółć%3A gęślą"_s;
        QTest::newRow("hash") << u"C# notes"_s << u"C%23 notes"_s;
    }

    void fileNames()
    {
        QFETCH(QString, page);
        QFETCH(QString, file);
        QCOMPARE(logseqFileNameForPage(page), file);
        QCOMPARE(logseqPageNameFromFile(file + u".md"_s), page);
    }

    void uuids()
    {
        static const QRegularExpression shape(
                u"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"_s);
        const QString a = uuidFromSeed(u"abc123"_s);
        QVERIFY2(shape.match(a).hasMatch(), qPrintable(a));
        QCOMPARE(uuidFromSeed(u"abc123"_s), a);
        QVERIFY(uuidFromSeed(u"abc124"_s) != a);
        QVERIFY(shape.match(uuidFromSeed(QString())).hasMatch());
        QVERIFY(shape.match(uuidFromSeed(u"żółw"_s)).hasMatch());
    }

    void timestamps()
    {
        QCOMPARE(logseqTimestamp(DateSpec{QDate(2026, 10, 15), std::nullopt}), u"<2026-10-15 Thu>"_s);
        QCOMPARE(logseqTimestamp(DateSpec{QDate(2026, 10, 15), QTime(10, 0)}, u".+1w"_s),
                 u"<2026-10-15 Thu 10:00 .+1w>"_s);
    }

    void pageProperties_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("keys");
        QTest::addColumn<QString>("body");
        QTest::newRow("leading lines") << u"title:: Projekty/Dom\nalias:: Dom, Remont\ntags:: praca\n\n- blok\n"_s
                                       << QStringList({u"title"_s, u"alias"_s, u"tags"_s}) << u"- blok\n"_s;
        QTest::newRow("first bullet") << u"- alias:: X\n  tags:: a\n- blok"_s << QStringList({u"alias"_s, u"tags"_s})
                                      << u"- blok"_s;
        QTest::newRow("normal block") << u"- zwykły blok\n  key:: v"_s << QStringList()
                                      << u"- zwykły blok\n  key:: v"_s;
        QTest::newRow("mixed bullet") << u"- tags:: a\n  zwykła linia\n- b"_s << QStringList()
                                      << u"- tags:: a\n  zwykła linia\n- b"_s;
        QTest::newRow("upper case key") << u"Type:: Book\n- x"_s << QStringList({u"type"_s}) << u"- x"_s;
        QTest::newRow("empty") << QString() << QStringList() << QString();
    }

    void pageProperties()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, keys);
        QFETCH(QString, body);
        const LogseqPageProperties p = takeLogseqPageProperties(content);
        QCOMPARE(p.keys, keys);
        QCOMPARE(p.body, body);
    }

    void pagePropertyValues()
    {
        const LogseqPageProperties p = takeLogseqPageProperties(u"alias:: Dom, Remont\ntags:: [[praca]], #dom\n- x"_s);
        QCOMPARE(p.values.value(u"alias"_s).toString(), u"Dom, Remont"_s);
        QCOMPARE(p.values.value(u"tags"_s).toString(), u"[[praca]], #dom"_s);
    }

    void blocks_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("markers")
                << u"- TODO kup mleko\n- DOING pisz\n- DONE gotowe\n- LATER potem\n- NOW teraz\n- CANCELED nie\n"
                   "- CANCELLED nie2\n- WAITING czekam\n- WAIT też\n- IN-PROGRESS w toku\n- TODOs nie zadanie\n- TODO\n"_s
                << u"- [ ] kup mleko\n- [/] pisz\n- [x] gotowe\n- [ ] potem\n- [/] teraz\n- [-] nie\n- [-] nie2\n"
                   "- [>] czekam\n- [>] też\n- [/] w toku\n- TODOs nie zadanie\n- [ ] \n"_s;
        QTest::newRow("priorities") << u"- TODO [#A] pilne\n- TODO zwykłe [#C]\n- [#B] nie zadanie\n"_s
                                    << u"- [ ] pilne ⏫\n- [ ] zwykłe 🔽\n- [#B] nie zadanie\n"_s;
        QTest::newRow("planning")
                << u"- TODO raport\n  SCHEDULED: <2026-10-15 Thu 10:00 .+1w>\n  DEADLINE: <2026-10-20 Tue>\n"_s
                << u"- [ ] raport ⏳ 2026-10-15 10:00 📅 2026-10-20 🔁 every week when done\n"_s;
        QTest::newRow("planning one line")
                << u"- TODO x\n  SCHEDULED: <2026-10-15 Thu> DEADLINE: <2026-10-16 Fri +1m>\n"_s
                << u"- [ ] x ⏳ 2026-10-15 📅 2026-10-16 🔁 every month\n"_s;
        QTest::newRow("id and collapsed") << u"- blok z id\n  id:: "_s + Uuid + u"\n  collapsed:: true\n\t- dziecko\n"_s
                                          << u"- blok z id ^"_s + Uuid + u"\n\t- dziecko\n"_s;
        QTest::newRow("logbook") << u"- DONE zadanie\n  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:00]--[2026-10-14 Wed "
                                    u"11:00] =>  01:00:00\n"
                                    "  :END:\n  dalszy tekst\n"_s
                                 << u"- [x] zadanie\n  dalszy tekst\n"_s;
        QTest::newRow("embeds")
                << u"- {{embed [[Strona]]}}\n- {{embed ((abc-123))}}\n- `{{embed [[kod]]}}`\nTekst {{embed [[Inna]]}}\n"_s
                << u"- ![[Strona]]\n- !((abc-123))\n- `{{embed [[kod]]}}`\nTekst ![[Inna]]\n"_s;
        QTest::newRow("code block item") << u"- ```js\n  TODO nie\n  id:: x\n  ```\n- TODO tak\n"_s
                                         << u"- ```js\n  TODO nie\n  id:: x\n  ```\n- [ ] tak\n"_s;
        QTest::newRow("code inside item") << u"- TODO zadanie\n  ```\n  SCHEDULED: <2026-10-15 Thu>\n  ```\n"_s
                                          << u"- [ ] zadanie\n  ```\n  SCHEDULED: <2026-10-15 Thu>\n  ```\n"_s;
        QTest::newRow("crlf tabs") << u"- TODO a\r\n\t- DONE b\r\n\t  id:: abc\r\n"_s
                                   << u"- [ ] a\r\n\t- [x] b ^abc\r\n"_s;
        QTest::newRow("text kept") << u"Tekst TODO nie\n# Nagłówek\n\nSCHEDULED: <2026-10-15 Thu>\n"_s
                                   << u"Tekst TODO nie\n# Nagłówek\n\nSCHEDULED: <2026-10-15 Thu>\n"_s;
        QTest::newRow("logseq id wins") << u"- blok ^old\n  id:: new-id\n"_s << u"- blok ^new-id\n"_s;
        QTest::newRow("uuid sanitised") << u"- blok\n  id:: a_b.c\n"_s << u"- blok ^a-b-c\n"_s;
        QTest::newRow("empty bullet planning") << u"- \n  SCHEDULED: <2026-10-15 Thu>\n"_s << u"- ⏳ 2026-10-15\n"_s;
        QTest::newRow("properties kept") << u"- blok\n  typ:: książka\n"_s << u"- blok\n  typ:: książka\n"_s;
        QTest::newRow("star bullet") << u"* TODO gwiazdka\n"_s << u"* [ ] gwiazdka\n"_s;
        QTest::newRow("removed last line") << u"- x\n  id:: y"_s << u"- x ^y\n"_s;
        // Lines that cannot be converted stay as written (never silently dropped).
        QTest::newRow("unparsable planning kept")
                << u"- TODO x\n  SCHEDULED: <2026-13-45 Thu>\n"_s << u"- [ ] x\n  SCHEDULED: <2026-13-45 Thu>\n"_s;
        QTest::newRow("unterminated logbook kept")
                << u"- DONE x\n  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:00]\n  notatka\n- następny\n"_s
                << u"- [x] x\n  :LOGBOOK:\n  CLOCK: [2026-10-14 Wed 10:00]\n  notatka\n- następny\n"_s;
    }

    void blocks()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(logseqBlocksToBrulion(input), expected);
    }

    // --- importer ---------------------------------------------------------------------------------

    void importGraph()
    {
        const QByteArray graph = writeZip({
                {u"graf/logseq/config.edn"_s, "{:meta/version 1\n :journal/page-title-format \"MMM do, yyyy\"\n "
                                              ":journal/file-name-format \"yyyy_MM_dd\"}"},
                {u"graf/logseq/bak/pages/old.md"_s, "- stare"},
                {u"graf/pages/Projekty___Dom.md"_s,
                 (u"title:: Projekty/Dom\nalias:: Remont\ntags:: praca, dom\n\n- TODO [#A] kup farbę\n"
                  "  SCHEDULED: <2026-10-15 Thu>\n  id:: "_s
                  + Uuid + u"\n- zdjęcie ![plan](../assets/plan_1.png)\n- spotkanie [[Oct 14th, 2026]]\n"_s)
                         .toUtf8()},
                {u"graf/pages/What%3F.md"_s, (u"- pytanie {{embed (("_s + Uuid + u"))}}\n"_s).toUtf8()},
                {u"graf/journals/2026_10_14.md"_s, (u"- DONE rano\n- ref (("_s + Uuid + u"))\n"_s).toUtf8()},
                {u"graf/journals/notadate.md"_s, "- x"},
                {u"graf/assets/plan_1.png"_s, "PNG"},
                {u"graf/draws/rysunek.excalidraw"_s, "{}"},
                {u"graf/pages/stara.org"_s, "* org"},
        });
        const ImportResult r = runImport(*importerById(u"logseq"), {iotest::bytes(u"graf.zip"_s, graph)},
                                         iotest::options(u"Logseq"_s));
        QCOMPARE(iotest::pathsOf(r),
                 QStringList({u"Logseq/Projekty/Dom.md"_s, u"Logseq/What.md"_s, u"Logseq/notadate.md"_s,
                              u"assets/Logseq/draws/rysunek.excalidraw"_s, u"assets/Logseq/pages/stara.org"_s,
                              u"assets/Logseq/plan_1.png"_s, u"journals/2026-10-14.md"_s}));
        const QString dom = iotest::textAt(r, u"Logseq/Projekty/Dom.md"_s);
        QCOMPARE(frontmatterOf(dom).value(u"aliases"_s).toStringList(), QStringList({u"Remont"_s}));
        QCOMPARE(frontmatterOf(dom).value(u"tags"_s).toStringList(), QStringList({u"praca"_s, u"dom"_s}));
        QVERIFY(!frontmatterOf(dom).contains(u"title"_s));
        QCOMPARE(bodyOf(dom),
                 u"- [ ] kup farbę ⏫ ⏳ 2026-10-15 ^"_s + Uuid
                         + u"\n- zdjęcie ![plan](../../assets/Logseq/plan_1.png)\n- spotkanie [[2026-10-14]]\n"_s);
        const QString what = iotest::textAt(r, u"Logseq/What.md"_s);
        QCOMPARE(frontmatterOf(what).value(u"aliases"_s).toStringList(), QStringList({u"What?"_s}));
        QCOMPARE(bodyOf(what), u"- pytanie !(("_s + Uuid + u"))\n"_s);
        QCOMPARE(iotest::textAt(r, u"journals/2026-10-14.md"_s), u"- [x] rano\n- ref (("_s + Uuid + u"))\n"_s);
        QVERIFY(iotest::fileAt(r, u"journals/2026-10-14.md"_s)->append);
        QCOMPARE(r.warnings.size(), 1); // the .org page
    }

    void importCustomJournalFormats()
    {
        const QList<InputFile> files = {
                iotest::text(u"logseq/config.edn"_s,
                             u"{:journal/page-title-format \"dd.MM.yyyy\" :journal/file-name-format \"yyyy-MM-dd\"}"_s),
                iotest::text(u"journals/2026-10-14.md"_s, u"- dziś\n"_s),
                iotest::text(u"pages/Plan.md"_s, u"- zob. [[14.10.2026]] i [[Oct 1st, 2026]] i [[Plan B]]\n"_s),
        };
        ImportOptions o = iotest::options();
        o.config.journalsFolder = u"Dziennik"_s;
        o.config.journalFileFormat = u"dd.MM.yyyy"_s;
        const ImportResult r = runImport(*importerById(u"logseq"), files, o);
        QCOMPARE(iotest::pathsOf(r), QStringList({u"Dziennik/14.10.2026.md"_s, u"Plan.md"_s}));
        QCOMPARE(iotest::textAt(r, u"Plan.md"_s), u"- zob. [[14.10.2026]] i [[01.10.2026]] i [[Plan B]]\n"_s);
    }

    void importConfigComments_data()
    {
        QTest::addColumn<QString>("edn");
        QTest::newRow("commented old value") << u"{;; :journal/page-title-format \"MMM do, yyyy\"\n "
                                                u":journal/page-title-format \"dd.MM.yyyy\" ; ;-)\n"
                                                " :journal/file-name-format \"yyyy-MM-dd\"}"_s;
        QTest::newRow("legacy date-formatter") << u"{:date-formatter \"dd.MM.yyyy\"}"_s;
        QTest::newRow("semicolon in a string") << u"{:custom \"a;b\" :journal/page-title-format \"dd.MM.yyyy\"}"_s;
    }

    void importConfigComments()
    {
        QFETCH(QString, edn);
        const ImportResult r = runImport(
                *importerById(u"logseq"),
                {iotest::text(u"logseq/config.edn"_s, edn), iotest::text(u"pages/Plan.md"_s, u"- [[14.10.2026]]\n"_s)},
                iotest::options());
        QCOMPARE(iotest::textAt(r, u"Plan.md"_s), u"- [[2026-10-14]]\n"_s);
    }

    void importJournalLinksNestedFormat_data()
    {
        // Journal formats with folders: the day alone ("14") names no page uniquely, the link
        // then names the journal by its path.
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("expected");
        QTest::newRow("folders, day only") << u"yyyy/MM/dd"_s << u"- [[journals/2026/10/14]]\n"_s;
        QTest::newRow("folders, full date") << u"yyyy/MM/yyyy-MM-dd"_s << u"- [[2026-10-14]]\n"_s;
        QTest::newRow("flat") << u"dd.MM.yyyy"_s << u"- [[14.10.2026]]\n"_s;
    }

    void importJournalLinksNestedFormat()
    {
        QFETCH(QString, format);
        QFETCH(QString, expected);
        ImportOptions o = iotest::options();
        o.config.journalFileFormat = format;
        const ImportResult r = runImport(*importerById(u"logseq"),
                                         {iotest::text(u"pages/Plan.md"_s, u"- [[Oct 14th, 2026]]\n"_s),
                                          iotest::text(u"journals/2026_10_14.md"_s, u"- dziś\n"_s)},
                                         o);
        QCOMPARE(iotest::textAt(r, u"Plan.md"_s), expected);
    }
};

QTEST_MAIN(TestIoLogseq)
#include "tst_io_logseq.moc"
