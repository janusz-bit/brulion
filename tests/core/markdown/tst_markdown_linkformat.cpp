// Links Brulion copies (markdown/linkformat.h, docs/SYNTAX.md §2 "Copied links"): heading
// fragments (markup, tags and characters that break wiki links), the heading matcher shared by
// navigation, embeds and copying (Obsidian heading paths, legacy fragments), unique fragments for
// duplicate headings, and the text of every kind of reference — which must read back as the link
// it was made for.
#include "markdown/inline.h"
#include "markdown/linkformat.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

/// A page with duplicate heading names under different parents.
QList<Heading> sampleHeadings()
{
    return {
            {1, u"# Projekt"_s, 0},           // 0
            {2, u"## Plan"_s, 2},             // 1
            {3, u"### Zadania"_s, 4},         // 2
            {2, u"## Budżet **2026**"_s, 6},  // 3
            {3, u"### Zadania"_s, 8},         // 4
            {1, u"# Archiwum"_s, 10},         // 5
            {2, u"## Plan"_s, 12},            // 6
            {2, u"## C# i C++"_s, 14},        // 7
            {2, u"## Spotkanie #praca"_s, 16} // 8
    };
}

} // namespace

class TestLinkFormat : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void headingFragments_data()
    {
        QTest::addColumn<QString>("heading");
        QTest::addColumn<QString>("fragment");
        QTest::newRow("plain polish") << u"Plan na wrzesień"_s << u"Plan na wrzesień"_s;
        QTest::newRow("bold") << u"Zażółć **gęślą** jaźń"_s << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("italic") << u"*Ważne* sprawy"_s << u"Ważne sprawy"_s;
        QTest::newRow("strike highlight code") << u"~~stare~~ ==nowe== `kod`"_s << u"stare nowe kod"_s;
        QTest::newRow("single = and ~ stay") << u"a = b ~ c"_s << u"a = b ~ c"_s;
        QTest::newRow("underscores stay") << u"funkcja __init__ i snake_case"_s << u"funkcja __init__ i snake_case"_s;
        QTest::newRow("tag") << u"Plan #praca"_s << u"Plan praca"_s;
        QTest::newRow("nested tag") << u"Remont #dom/łazienka"_s << u"Remont dom/łazienka"_s;
        QTest::newRow("bracket tag") << u"Spotkanie #[[Dział sprzedaży]]"_s << u"Spotkanie Dział sprzedaży"_s;
        QTest::newRow("pipe") << u"A | B"_s << u"A B"_s;
        QTest::newRow("hash in a word") << u"C# i C++"_s << u"C i C++"_s;
        QTest::newRow("wiki alias") << u"Spotkanie [[Ania|Anią]]"_s << u"Spotkanie Anią"_s;
        QTest::newRow("wiki link") << u"Spotkanie z [[Ania]]"_s << u"Spotkanie z Ania"_s;
        QTest::newRow("wiki heading") << u"Notatki [[Projekty/Dom#Plan]]"_s << u"Notatki Projekty/Dom Plan"_s;
        QTest::newRow("markdown link") << u"Zobacz [opis](https://example.com/a:b)"_s << u"Zobacz opis"_s;
        QTest::newRow("block id") << u"Tytuł ^abc123"_s << u"Tytuł"_s;
        QTest::newRow("caret inside") << u"x^2 i y^3"_s << u"x 2 i y 3"_s;
        QTest::newRow("colon") << u"Q&A: test"_s << u"Q&A test"_s;
        QTest::newRow("comment") << u"Plan %%szkic%% końcowy"_s << u"Plan szkic końcowy"_s;
        QTest::newRow("backslash") << u"a\\b\\#c"_s << u"a b c"_s;
        QTest::newRow("brackets") << u"Lista [x] i [y]"_s << u"Lista x i y"_s;
        QTest::newRow("emoji") << u"📅 Plan 🎉"_s << u"📅 Plan 🎉"_s;
        QTest::newRow("atx markers") << u"## Nagłówek ##"_s << u"Nagłówek"_s;
        QTest::newRow("atx and tag") << u"### #praca i dom"_s << u"praca i dom"_s;
        QTest::newRow("nbsp and tabs") << u"  Plan\u00a0na\twrzesie\u0144\u00a0 "_s << u"Plan na wrzesie\u0144"_s;
        QTest::newRow("nfd to nfc") << u"Z\u0307o\u0301\u0142w"_s << u"\u017b\u00f3\u0142w"_s;
        QTest::newRow("first line only") << u"Plan\n  id:: 6f1c0e2a-1b2c-4d5e-8f90-a1b2c3d4e5f6"_s << u"Plan"_s;
        QTest::newRow("crlf") << u"Plan\r\n"_s << u"Plan"_s;
        QTest::newRow("block ref stays") << u"Zadanie ((abc123))"_s << u"Zadanie ((abc123))"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("blank") << u"   \t "_s << QString();
        QTest::newRow("only reserved") << u"#|^"_s << QString();
        QTest::newRow("only markup") << u"****"_s << QString();
        QTest::newRow("unclosed link") << u"Plan [[niedokończony"_s << u"Plan niedokończony"_s;
    }

    void headingFragments()
    {
        QFETCH(QString, heading);
        QFETCH(QString, fragment);
        QCOMPARE(headingFragment(heading), fragment);
        // Nothing in a fragment splits or ends a wiki link.
        for (const QChar c : fragment)
            QVERIFY2(QStringLiteral("#|^[]\\:").indexOf(c) < 0, qPrintable(fragment));
    }

    void longHeading()
    {
        QString heading;
        for (int i = 0; i < 1000; ++i)
            heading += u"ż#[["_s + QString::number(i % 10);
        QElapsedTimer timer;
        timer.start();
        const QString fragment = headingFragment(heading);
        QVERIFY(timer.elapsed() < 2000);
        QVERIFY(fragment.size() <= heading.size());
        QVERIFY(fragment.startsWith(u"ż 0ż 1ż 2"_s));
        // Too long to be named in a link: no unique fragment (a block id is used instead).
        QCOMPARE(uniqueHeadingFragment({{1, heading, 0}}, 0), QString());
    }

    void keys()
    {
        QCOMPARE(headingKey(u"Zażółć **Gęślą** Jaźń #Praca"_s), u"zazolc gesla jazn praca"_s);
        QCOMPARE(headingKey(u"ŻÓŁW"_s), headingKey(u"żółw"_s));
        QCOMPARE(headingKey(QString()), QString());
    }

    void matches_data()
    {
        QTest::addColumn<QString>("fragment");
        QTest::addColumn<QString>("heading");
        QTest::addColumn<bool>("match");
        QTest::newRow("folded") << u"zazolc gesla jazn"_s << u"Zażółć **gęślą** jaźń"_s << true;
        QTest::newRow("upper case") << u"PLAN NA WRZESIEŃ"_s << u"## Plan na wrzesień"_s << true;
        QTest::newRow("path") << u"Rodzic#Dziecko"_s << u"Dziecko"_s << true;
        QTest::newRow("legacy whole") << u"C# i C++"_s << u"C# i C++"_s << true;
        QTest::newRow("copied") << u"C i C++"_s << u"## C# i C++"_s << true;
        QTest::newRow("trailing spaces") << u"Plan  "_s << u"Plan"_s << true;
        QTest::newRow("tag") << u"Plan praca"_s << u"Plan #praca"_s << true;
        QTest::newRow("colon") << u"Q&A test"_s << u"Q&A: test"_s << true;
        QTest::newRow("block id") << u"Tytuł"_s << u"Tytuł ^abc123"_s << true;
        QTest::newRow("prefix only") << u"Plan"_s << u"Planowanie"_s << false;
        QTest::newRow("tag alone") << u"praca"_s << u"Plan #praca"_s << false;
        QTest::newRow("empty fragment") << QString() << u"Plan"_s << false;
        QTest::newRow("empty heading") << u"Plan"_s << QString() << false;
        QTest::newRow("both empty") << QString() << QString() << false;
        QTest::newRow("only hashes") << u"##"_s << u"##"_s << false;
    }

    void matches()
    {
        QFETCH(QString, fragment);
        QFETCH(QString, heading);
        QFETCH(bool, match);
        QCOMPARE(headingMatches(fragment, heading), match);
    }

    void find_data()
    {
        QTest::addColumn<QString>("fragment");
        QTest::addColumn<int>("index");
        QTest::newRow("first of duplicates") << u"Plan"_s << 1;
        QTest::newRow("folded") << u"zadania"_s << 2;
        QTest::newRow("path") << u"Budżet 2026#Zadania"_s << 4;
        QTest::newRow("folded path") << u"budzet 2026#ZADANIA"_s << 4;
        QTest::newRow("full path") << u"Projekt#Budżet 2026#Zadania"_s << 4;
        QTest::newRow("skipped level") << u"Projekt#Zadania"_s << 2;
        QTest::newRow("other parent") << u"Archiwum#Plan"_s << 6;
        QTest::newRow("unknown parent: last segment") << u"Archiwum#Zadania"_s << 2;
        QTest::newRow("legacy whole fragment") << u"C# i C++"_s << 7;
        QTest::newRow("copied fragment") << u"C i C++"_s << 7;
        QTest::newRow("tag hash") << u"Spotkanie praca"_s << 8;
        QTest::newRow("written with the tag") << u"Spotkanie #praca"_s << 8;
        QTest::newRow("empty segments") << u"#Archiwum##Plan#"_s << 6;
        QTest::newRow("unknown") << u"Nieznany"_s << -1;
        QTest::newRow("empty") << QString() << -1;
        QTest::newRow("hashes only") << u"###"_s << -1;
        QTest::newRow("spaces") << u"   "_s << -1;
    }

    void find()
    {
        QFETCH(QString, fragment);
        QFETCH(int, index);
        QCOMPARE(findHeading(sampleHeadings(), fragment), index);
        QCOMPARE(findHeading({}, fragment), -1);
    }

    void unique_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<QString>("fragment");
        QTest::newRow("unique name") << 0 << u"Projekt"_s;
        QTest::newRow("duplicate: parent path") << 1 << u"Projekt#Plan"_s;
        QTest::newRow("nearest parent") << 2 << u"Plan#Zadania"_s;
        QTest::newRow("markup in the parent") << 4 << u"Budżet 2026#Zadania"_s;
        QTest::newRow("other branch") << 6 << u"Archiwum#Plan"_s;
        QTest::newRow("hash removed") << 7 << u"C i C++"_s;
        QTest::newRow("tag") << 8 << u"Spotkanie praca"_s;
        QTest::newRow("out of range") << 9 << QString();
        QTest::newRow("negative") << -1 << QString();
    }

    void unique()
    {
        QFETCH(int, index);
        QFETCH(QString, fragment);
        const QList<Heading> headings = sampleHeadings();
        QCOMPARE(uniqueHeadingFragment(headings, index), fragment);
        if (!fragment.isEmpty())
            QCOMPARE(findHeading(headings, fragment), index); // the copied link leads back
    }

    void notUnique()
    {
        // The same path twice: no fragment names the second one alone.
        const QList<Heading> twice{{1, u"A"_s, 0}, {2, u"B"_s, 1}, {1, u"A"_s, 2}, {2, u"B"_s, 3}};
        QCOMPARE(uniqueHeadingFragment(twice, 3), QString());
        QCOMPARE(uniqueHeadingFragment(twice, 1), QString());
        // Top-level duplicates have no parents to tell them apart.
        const QList<Heading> top{{1, u"Notatki"_s, 0}, {1, u"notatki"_s, 1}};
        QCOMPARE(uniqueHeadingFragment(top, 0), QString());
        // A heading with no text, an empty parent skipped.
        const QList<Heading> empty{{1, u"#"_s, 0}, {2, u"X"_s, 1}, {1, u"P"_s, 2}, {2, u"X"_s, 3}};
        QCOMPARE(uniqueHeadingFragment(empty, 0), QString());
        QCOMPARE(uniqueHeadingFragment(empty, 1), QString());
        QCOMPARE(uniqueHeadingFragment(empty, 3), u"P#X"_s);
        // A path that the whole-fragment rule would give to another heading is not used.
        const QList<Heading> clash{{1, u"A B"_s, 0}, {1, u"A"_s, 1}, {2, u"B"_s, 2}, {2, u"B"_s, 3}};
        QCOMPARE(findHeading(clash, u"A#B"_s), 0);
        QCOMPARE(uniqueHeadingFragment(clash, 2), QString());
    }

    void references_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("fragment");
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("text");
        const auto row = [](const char *name, ReferenceKind kind, const QString &page, const QString &fragment,
                            const QString &id, const QString &text) {
            QTest::newRow(name) << int(kind) << page << fragment << id << text;
        };
        const QString uuid = u"6f1c0e2a-1b2c-4d5e-8f90-a1b2c3d4e5f6"_s;
        row("page", ReferenceKind::PageLink, u"Zakupy/Spożywcze"_s, {}, {}, u"[[Zakupy/Spożywcze]]"_s);
        row("page embed", ReferenceKind::PageEmbed, u"Zakupy/Spożywcze"_s, {}, {}, u"![[Zakupy/Spożywcze]]"_s);
        row("heading", ReferenceKind::HeadingLink, u"Dom"_s, u"Plan na wrzesień"_s, {}, u"[[Dom#Plan na wrzesień]]"_s);
        row("heading embed", ReferenceKind::HeadingEmbed, u"Dom"_s, u"Plan na wrzesień"_s, {},
            u"![[Dom#Plan na wrzesień]]"_s);
        row("heading path", ReferenceKind::HeadingLink, u"Dom"_s, u"Rodzic#Dziecko"_s, {}, u"[[Dom#Rodzic#Dziecko]]"_s);
        row("journal block", ReferenceKind::BlockLink, u"2026-10-22"_s, {}, u"abc123"_s, u"[[2026-10-22#^abc123]]"_s);
        row("block link embed", ReferenceKind::BlockLinkEmbed, u"2026-10-22"_s, {}, u"abc123"_s,
            u"![[2026-10-22#^abc123]]"_s);
        row("block ref", ReferenceKind::BlockRef, u"Dom"_s, {}, u"abc123"_s, u"((abc123))"_s);
        row("logseq uuid", ReferenceKind::BlockRef, u"Dom"_s, {}, uuid, u"(("_s + uuid + u"))"_s);
        row("block embed", ReferenceKind::BlockEmbed, u"Dom"_s, {}, uuid, u"!(("_s + uuid + u"))"_s);
        row("block ref without page", ReferenceKind::BlockRef, {}, {}, u"abc123"_s, u"((abc123))"_s);
        row("no fragment", ReferenceKind::HeadingLink, u"Dom"_s, {}, {}, u"[[Dom]]"_s);
        row("no fragment embed", ReferenceKind::HeadingEmbed, u"Dom"_s, {}, {}, u"![[Dom]]"_s);
        row("no id link", ReferenceKind::BlockLink, u"Dom"_s, {}, {}, u"[[Dom]]"_s);
        row("no id ref", ReferenceKind::BlockRef, u"Dom"_s, {}, {}, u"[[Dom]]"_s);
        row("no id embed", ReferenceKind::BlockEmbed, u"Dom"_s, {}, {}, u"![[Dom]]"_s);
        row("same page heading", ReferenceKind::HeadingLink, {}, u"Plan"_s, {}, u"[[#Plan]]"_s);
        row("same page block", ReferenceKind::BlockLink, {}, {}, u"abc123"_s, u"[[#^abc123]]"_s);
        row("no page", ReferenceKind::PageLink, {}, {}, {}, QString());
        row("no page embed", ReferenceKind::PageEmbed, {}, {}, {}, QString());
        row("nothing at all", ReferenceKind::BlockRef, {}, {}, {}, QString());
    }

    void references()
    {
        QFETCH(int, kind);
        QFETCH(QString, page);
        QFETCH(QString, fragment);
        QFETCH(QString, id);
        QFETCH(QString, text);
        QCOMPARE(formatReference(ReferenceKind(kind), page, fragment, id), text);
    }

    void copiedLinksReadBack_data()
    {
        QTest::addColumn<QString>("heading");
        QTest::newRow("polish") << u"Zażółć **gęślą** jaźń"_s;
        QTest::newRow("tag and colon") << u"Q&A: #praca | notatki"_s;
        QTest::newRow("link") << u"Spotkanie [[Ania|Anią]] ^abc"_s;
        QTest::newRow("brackets") << u"Lista [x]]"_s;
        QTest::newRow("hash") << u"C# i C++"_s;
        QTest::newRow("comment") << u"Plan %%[[x]]%%"_s;
    }

    void copiedLinksReadBack()
    {
        // What the copy commands write parses back as a link to that heading of that page.
        QFETCH(QString, heading);
        const QString fragment = headingFragment(heading);
        QVERIFY(!fragment.isEmpty());
        const QString page = u"Projekty/Dom remontu"_s;
        for (const auto kind : {ReferenceKind::HeadingLink, ReferenceKind::HeadingEmbed}) {
            const QString text = formatReference(kind, page, fragment);
            const InlineScan scan = extractInline(QString(u"Zobacz "_s + text + u" koniec"_s));
            QCOMPARE(scan.links.size(), 1);
            const Link &link = scan.links.first();
            QCOMPARE(link.kind, kind == ReferenceKind::HeadingLink ? LinkKind::Wiki : LinkKind::Embed);
            QCOMPARE(link.target, page);
            QCOMPARE(link.heading, fragment);
            QVERIFY(link.blockId.isEmpty());
            QVERIFY(headingMatches(link.heading, heading));
            QCOMPARE(findHeading({{2, u"Inny"_s, 0}, {2, heading, 1}}, link.heading), 1);
            QVERIFY(scan.tags.isEmpty());
        }
        const InlineScan block = extractInline(formatReference(ReferenceKind::BlockLink, page, {}, u"abc-123"_s));
        QCOMPARE(block.links.size(), 1);
        QCOMPARE(block.links.first().blockId, u"abc-123"_s);
        const InlineScan ref = extractInline(formatReference(ReferenceKind::BlockRef, page, {}, u"abc-123"_s));
        QCOMPARE(ref.links.size(), 1);
        QCOMPARE(ref.links.first().kind, LinkKind::BlockRef);
        QCOMPARE(ref.links.first().target, u"abc-123"_s);
    }
};

QTEST_GUILESS_MAIN(TestLinkFormat)
#include "tst_markdown_linkformat.moc"
