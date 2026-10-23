// Inline syntax: wiki links, embeds, block references, Markdown links, URLs and tags.
// Test cases ported from archive/web-v1 (inline tests).
#include "markdown/inline.h"
#include "markdown/inline_p.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

QList<Link> links(const QString &s)
{
    return extractInline(s).links;
}

QStringList tags(const QString &s)
{
    return extractInline(s).tags;
}

QString kindName(LinkKind k)
{
    switch (k) {
    case LinkKind::Wiki: return u"wiki"_s;
    case LinkKind::Embed: return u"embed"_s;
    case LinkKind::BlockRef: return u"blockref"_s;
    case LinkKind::BlockEmbed: return u"blockembed"_s;
    case LinkKind::Markdown: return u"markdown"_s;
    case LinkKind::Url: return u"url"_s;
    }
    return {};
}

/// "kind|target|heading|blockId|display|from-to@line"
QString describe(const Link &l)
{
    return kindName(l.kind) + u'|' + l.target + u'|' + l.heading + u'|' + l.blockId + u'|' + l.display + u'|'
           + QString::number(l.from) + u'-' + QString::number(l.to) + u'@' + QString::number(l.line);
}

QStringList describeAll(const QString &s)
{
    QStringList out;
    for (const Link &l : links(s))
        out.append(describe(l));
    return out;
}

} // namespace

class TestInline : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void wikiLinks()
    {
        QCOMPARE(describeAll(u"see [[Page]] and [[Other Page|alias]]"_s),
                 QStringList({u"wiki|Page||||4-12@0"_s, u"wiki|Other Page|||alias|17-37@0"_s}));
        QCOMPARE(describe(links(u"[[Page#Heading]]"_s).first()), u"wiki|Page|Heading|||0-16@0"_s);
        QCOMPARE(describe(links(u"[[Page#^abc123]]"_s).first()), u"wiki|Page||abc123||0-16@0"_s);
        QCOMPARE(describe(links(u"[[Page#H1#H2|x]]"_s).first()), u"wiki|Page|H1#H2||x|0-16@0"_s);
        QCOMPARE(describe(links(u"[[Zażółć gęślą jaźń]]"_s).first()), u"wiki|Zażółć gęślą jaźń||||0-21@0"_s);
    }

    void samePageLinks()
    {
        QCOMPARE(describe(links(u"[[#^abc]]"_s).first()), u"wiki|||abc||0-9@0"_s);
        const Link h = links(u"[[#Sekcja]]"_s).first();
        QVERIFY(h.target.isEmpty());
        QCOMPARE(h.heading, u"Sekcja"_s);
    }

    void embeds()
    {
        const Link e = links(u"![[Page#^id]]"_s).first();
        QCOMPARE(e.kind, LinkKind::Embed);
        QCOMPARE(e.target, u"Page"_s);
        QCOMPARE(e.blockId, u"id"_s);
        QCOMPARE(e.from, 0);
        QCOMPARE(e.to, 13);
        const Link img = links(u"![[zdjęcie.png|300]]"_s).first();
        QCOMPARE(img.kind, LinkKind::Embed);
        QCOMPARE(img.target, u"zdjęcie.png"_s);
        QCOMPARE(img.display, u"300"_s);
    }

    void escapedTablePipe()
    {
        const Link l = links(u"| [[Page\\|Alias]] |"_s).first();
        QCOMPARE(l.target, u"Page"_s);
        QCOMPARE(l.display, u"Alias"_s);
    }

    void invalidWikiLinks()
    {
        QVERIFY(links(u"[[]] [[ ]] [[open"_s).isEmpty());
        QVERIFY(links(u"[[a\nb]]"_s).isEmpty());
        const auto nested = links(u"[[a [[b]]"_s);
        QCOMPARE(nested.size(), 1);
        QCOMPARE(nested.first().target, u"b"_s);
        QVERIFY(links(u"\\[[not a link]]"_s).isEmpty());
    }

    void blockRefs()
    {
        QCOMPARE(
                describeAll(u"see ((abc-123)) and !((64f0a1b2-0000-4c4c-8888-123456789abc))"_s),
                QStringList(
                        {u"blockref|abc-123||abc-123||4-15@0"_s,
                         u"blockembed|64f0a1b2-0000-4c4c-8888-123456789abc||64f0a1b2-0000-4c4c-8888-123456789abc||20-61@0"_s}));
        QVERIFY(links(u"((not valid))"_s).isEmpty());
    }

    void markdownLinks()
    {
        Link l = links(u"[Brulion](https://example.com/a?b=c#frag)"_s).first();
        QCOMPARE(l.kind, LinkKind::Markdown);
        QCOMPARE(l.target, u"https://example.com/a?b=c#frag"_s);
        QCOMPARE(l.display, u"Brulion"_s);
        l = links(u"[x](folder/Old%20Name.md#My%20Heading)"_s).first();
        QCOMPARE(l.target, u"folder/Old Name.md"_s);
        QCOMPARE(l.heading, u"My Heading"_s);
        l = links(u"[x](Page.md#^blk)"_s).first();
        QCOMPARE(l.target, u"Page.md"_s);
        QCOMPARE(l.blockId, u"blk"_s);
        l = links(u"[x](#local)"_s).first();
        QVERIFY(l.target.isEmpty());
        QCOMPARE(l.heading, u"local"_s);
        QCOMPARE(links(u"[x](<Some Page.md> \"title\")"_s).first().target, u"Some Page.md"_s);
        QCOMPARE(links(u"[x](Strona%20%C5%BC.md)"_s).first().target, u"Strona ż.md"_s);
    }

    void images()
    {
        const Link l = links(u"![wykres](assets/wykres.png)"_s).first();
        QCOMPARE(l.kind, LinkKind::Markdown);
        QCOMPARE(l.target, u"assets/wykres.png"_s);
        QCOMPARE(l.display, u"wykres"_s);
        QCOMPARE(l.from, 0);
    }

    void balancedBrackets()
    {
        const Link l = links(u"[a [b] c](x(1).md)"_s).first();
        QCOMPARE(l.target, u"x(1).md"_s);
        QCOMPARE(l.display, u"a [b] c"_s);
    }

    void notLinks()
    {
        QVERIFY(links(u"note[^1] and [label] and [x]()"_s).isEmpty());
        QCOMPARE(links(u"[x](100%25%zz.md)"_s).first().target, u"100%%zz.md"_s);
    }

    void bareUrls()
    {
        Link l = links(u"Zobacz https://example.com/a_(b). Koniec"_s).first();
        QCOMPARE(l.kind, LinkKind::Url);
        QCOMPARE(l.target, u"https://example.com/a_(b)"_s);
        QCOMPARE(links(u"(https://example.com/x)"_s).first().target, u"https://example.com/x"_s);
        QCOMPARE(links(u"HTTPS://EXAMPLE.COM, ok"_s).first().target, u"HTTPS://EXAMPLE.COM"_s);
        QVERIFY(links(u"xhttps://no.pl"_s).isEmpty());
        QVERIFY(links(u"https://"_s).isEmpty());
        QCOMPARE(links(u"Strona: https://pl.wikipedia.org/wiki/Łódź!"_s).first().target,
                 u"https://pl.wikipedia.org/wiki/Łódź"_s);
    }

    void autolinks()
    {
        QVERIFY(links(u"<https://a.pl/x y>"_s).isEmpty());
        const Link l = links(u"<https://a.pl/x>"_s).first();
        QCOMPARE(l.kind, LinkKind::Url);
        QCOMPARE(l.target, u"https://a.pl/x"_s);
        QCOMPARE(l.from, 0);
        QCOMPARE(l.to, 16);
        QCOMPARE(links(u"<jan@example.com>"_s).first().target, u"mailto:jan@example.com"_s);
    }

    void emailAutolinks_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("target"); // of the first link; empty: no link
        QTest::newRow("plain") << u"<jan@example.com>"_s << u"mailto:jan@example.com"_s;
        QTest::newRow("local part punctuation") << u"Pisz: <jan.k+x!#$%&'*/=?^_`{|}~-@a.example.pl>."_s
                                                << u"mailto:jan.k+x!#$%&'*/=?^_`{|}~-@a.example.pl"_s;
        QTest::newRow("inner of double brackets") << u"<<jan@example.com>>"_s << u"mailto:jan@example.com"_s;
        QTest::newRow("after other tags") << u"<b>x</b> <i>y <a@b.pl>"_s << u"mailto:a@b.pl"_s;
        QTest::newRow("hyphen inside a label") << u"<a@my-host.pl>"_s << u"mailto:a@my-host.pl"_s;
        QTest::newRow("label of 63") << u"<a@%1.pl>"_s.arg(QString(63, u'x'))
                                     << u"mailto:a@%1.pl"_s.arg(QString(63, u'x'));
        QTest::newRow("label of 64") << u"<a@%1.pl>"_s.arg(QString(64, u'x')) << QString();
        QTest::newRow("label starts with hyphen") << u"<a@-host.pl>"_s << QString();
        QTest::newRow("label ends with hyphen") << u"<a@host-.pl>"_s << QString();
        QTest::newRow("empty label") << u"<a@host..pl>"_s << QString();
        QTest::newRow("trailing dot") << u"<a@host.>"_s << QString();
        QTest::newRow("underscore in domain") << u"<a@ho_st.pl>"_s << QString();
        QTest::newRow("no local part") << u"<@host.pl>"_s << QString();
        QTest::newRow("two at signs") << u"<a@b@c.pl>"_s << QString();
        QTest::newRow("space") << u"<jan @example.com>"_s << QString();
        QTest::newRow("not closed") << u"<jan@example.com"_s << QString();
        QTest::newRow("Polish letter") << u"<żaneta@example.com>"_s << QString();
        QTest::newRow("bracket inside") << u"<jan<x@example.com>"_s << u"mailto:x@example.com"_s;
        // PCRE2 checks the UTF-16 from the match offset to the end of the paragraph: an e-mail
        // autolink followed by a lone surrogate was never linked, one after it was; kept as it was.
        QTest::newRow("lone surrogate after") << u"<jan@example.com> "_s + QChar(0xD800) << QString();
        QTest::newRow("lone low surrogate after") << u"<jan@example.com>"_s + QChar(0xDC00) << QString();
        QTest::newRow("lone surrogate before")
                << QChar(0xD800) + u" żółw <jan@example.com>"_s << u"mailto:jan@example.com"_s;
        QTest::newRow("lone low surrogate before") << u"x"_s + QChar(0xDFFF) + u"<a@b.pl> y"_s << u"mailto:a@b.pl"_s;
        QTest::newRow("surrogate pair after") << u"<jan@example.com> 😀"_s << u"mailto:jan@example.com"_s;
    }
    void emailAutolinks()
    {
        QFETCH(QString, text);
        QFETCH(QString, target);
        const QList<Link> found = links(text);
        QCOMPARE(found.isEmpty() ? QString() : found.first().target, target);
    }

    void emailAutolinksMatchTheRegexOnTheWholeText()
    {
        // Property: an e-mail autolink at the start of a text is what the CommonMark regex
        // finds when matched on the whole text there (the parser matches only a short slice).
        static const QRegularExpression whole(
                uR"(<[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*>)"_s);
        const QString alphabet = u"abz09@.-_+~>< ąż\t!"_s;
        QRandomGenerator rng(1017);
        for (int k = 0; k < 20000; ++k) {
            QString s = u"<jan.kowalski@poczta.example.pl>"_s;
            // A valid autolink with up to four random edits, or random text.
            if (rng.bounded(4) == 0) {
                s = u"<"_s;
                for (int n = int(rng.bounded(1, 30)); n > 0; --n)
                    s += alphabet.at(rng.bounded(int(alphabet.size())));
            } else {
                for (int edits = int(rng.bounded(5)); edits > 0; --edits) {
                    const qsizetype at = 1 + rng.bounded(int(s.size()));
                    const QChar c = alphabet.at(rng.bounded(int(alphabet.size())));
                    switch (rng.bounded(3)) {
                    case 0: s.insert(at, c); break;
                    case 1:
                        if (at < s.size())
                            s.remove(at, 1);
                        break;
                    default:
                        if (at < s.size())
                            s[at] = c;
                        break;
                    }
                }
            }
            const auto m =
                    whole.match(s, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
            const QList<Link> found = links(s);
            const bool atStart = !found.isEmpty() && found.first().from == 0;
            QVERIFY2(atStart == m.hasMatch(), qPrintable(s));
            if (atStart) {
                QCOMPARE(found.first().to, m.capturedEnd());
                QCOMPARE(found.first().target, u"mailto:"_s + s.sliced(1, m.capturedEnd() - 2));
            }
        }
    }

    void emailAutolinksMatchTheRegexAtTheirOffset()
    {
        // Property: with lone surrogates and surrogate pairs around it, an e-mail autolink is
        // exactly what the CommonMark regex matched on the whole paragraph at the '<' (as the
        // parser did before it matched only a slice; PCRE2 checks the UTF-16 from that offset).
        static const QRegularExpression whole(
                uR"(<[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*>)"_s);
        QStringList pieces = u"<a@b.pl>|<jan@example.com>|<x@y>|<a@b| |żółw|😀|<|>|@|ab"_s.split(u'|');
        for (const int lone : {0xD800, 0xDBFF, 0xDC00, 0xDFFF})
            pieces.append(QString(QChar(lone)));
        QRandomGenerator rng(1018);
        int linked = 0;
        int refused = 0;
        for (int k = 0; k < 20000; ++k) {
            QString s;
            for (int n = int(rng.bounded(1, 9)); n > 0; --n)
                s += pieces.at(rng.bounded(int(pieces.size())));
            struct Expected {
                qsizetype from, to;
                QString target;
            };
            QList<Expected> expected;
            qsizetype from = 0;
            while ((from = s.indexOf(u'<', from)) >= 0) {
                const auto m = whole.matchView(s, from, QRegularExpression::NormalMatch,
                                               QRegularExpression::AnchorAtOffsetMatchOption);
                if (m.hasMatch()) {
                    expected.append(
                            {from, m.capturedEnd(), u"mailto:"_s + s.sliced(from + 1, m.capturedEnd() - from - 2)});
                    from = m.capturedEnd();
                } else {
                    // An autolink on its own, refused for a lone surrogate after it?
                    const qsizetype close = s.indexOf(u'>', from);
                    if (close > 0
                        && whole.matchView(QStringView(s).sliced(from, close + 1 - from), 0,
                                           QRegularExpression::NormalMatch,
                                           QRegularExpression::AnchorAtOffsetMatchOption)
                                   .hasMatch())
                        ++refused;
                    ++from;
                }
            }
            const QList<Link> found = links(s);
            QCOMPARE(found.size(), expected.size());
            for (qsizetype l = 0; l < found.size(); ++l) {
                QCOMPARE(found[l].from, expected[l].from);
                QCOMPARE(found[l].to, expected[l].to);
                QCOMPARE(found[l].target, expected[l].target);
            }
            linked += int(found.size());
        }
        // Both sides of the rule are exercised: links before a lone surrogate are refused.
        QVERIFY2(linked > 1000 && refused > 1000, qPrintable(u"%1 %2"_s.arg(linked).arg(refused)));
    }

    void manyAngleBracketsAreLinear()
    {
        // Review regression (perf plan N6): the e-mail autolink regex was matched with the whole
        // paragraph as the subject once per '<', and QRegularExpression checks the UTF-16 of
        // the whole subject on every call — 20 000 inline tags took seconds.
        const QString tags = u"<span>a</span> "_s.repeated(20000) + u"<jan@example.com>"_s;
        const QString lessThan = u"a < b "_s.repeated(50000) + u"<jan@example.com>"_s;
        QElapsedTimer t;
        t.start();
        const QList<Link> a = links(tags);
        const QList<Link> b = links(lessThan);
        QVERIFY2(t.elapsed() < 2000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(a.size(), 1);
        QCOMPARE(a.first().target, u"mailto:jan@example.com"_s);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.first().from, lessThan.size() - 17);
    }

    void tagsUnicode()
    {
        QCOMPARE(tags(u"#zakupy/spożywcze #Łódź #a_b-c #[[dwa słowa]]"_s),
                 QStringList({u"zakupy/spożywcze"_s, u"Łódź"_s, u"a_b-c"_s, u"dwa słowa"_s}));
    }

    void tagRules()
    {
        QCOMPARE(tags(u"#1 #2026 #1a (#ok) a#nie x.#nie"_s), QStringList({u"1a"_s, u"ok"_s}));
        QVERIFY(tags(u"# Heading"_s).isEmpty());
        QVERIFY(tags(u"`#code` ``#x``"_s).isEmpty());
        QVERIFY(tags(u"[[Page#Heading]] [x](#anchor) https://x.pl/#frag \\#esc %%#hidden%%"_s).isEmpty());
        QCOMPARE(tags(u"#Praca #praca #PRACA"_s), QStringList({u"Praca"_s}));
        QCOMPARE(tags(u"#a/b/"_s), QStringList({u"a/b"_s}));
        QCOMPARE(tags(u"#spoz\u0307ywcze"_s), QStringList({u"spożywcze"_s}));
        QVERIFY(tags(u"TODO [#A] zadanie"_s).isEmpty());
        QCOMPARE(tags(u"tekst\u00a0#tag"_s), QStringList({u"tag"_s}));
        // Numbers in any script are no tags; a letter or another digit kind makes one.
        QVERIFY(tags(u"#٣ #١٢ #１２ #- #_ #/"_s).isEmpty());
        QCOMPARE(tags(u"#٣a #12_"_s), QStringList({u"٣a"_s, u"12_"_s}));
    }

    void findTagsGivesRanges()
    {
        const QString text = u"a #Łódź i (#x/y/) #[[dwa słowa]] `#kod` [[S#n]] #1"_s;
        const QList<TagSpan> spans = findTags(text);
        QCOMPARE(spans.size(), 3);
        QCOMPARE(spans[0].name, u"Łódź"_s);
        QCOMPARE(text.mid(spans[0].from, spans[0].to - spans[0].from), u"#Łódź"_s);
        QCOMPARE(spans[1].name, u"x/y"_s);
        QCOMPARE(text.mid(spans[1].from, spans[1].to - spans[1].from), u"#x/y"_s);
        QCOMPARE(spans[2].name, u"dwa słowa"_s);
        QCOMPARE(text.mid(spans[2].from, spans[2].to - spans[2].from), u"#[[dwa słowa]]"_s);
        // Same tags as extractInline(), not deduplicated.
        QStringList names;
        for (const TagSpan &span : findTags(u"#a #A #b"_s))
            names.append(span.name);
        QCOMPARE(names, QStringList({u"a"_s, u"A"_s, u"b"_s}));
        QCOMPARE(extractInline(u"#a #A #b"_s).tags, QStringList({u"a"_s, u"b"_s}));
        QVERIFY(findTags(QString()).isEmpty());
    }

    // tagSyntax(): the tag that names a page, simple when that reads back as the same name.
    void tagSyntax_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("tag");
        QTest::newRow("word") << u"praca"_s << u"#praca"_s;
        QTest::newRow("polish") << u"żółw"_s << u"#żółw"_s;
        QTest::newRow("nested") << u"Remont/Łazienka"_s << u"#Remont/Łazienka"_s;
        QTest::newRow("dash and underscore") << u"do-zrobienia_2"_s << u"#do-zrobienia_2"_s;
        QTest::newRow("trimmed") << u"  praca  "_s << u"#praca"_s;
        QTest::newRow("trailing slash") << u"a/"_s << u"#a"_s;
        QTest::newRow("date") << u"2026-10-13"_s << u"#2026-10-13"_s;
        QTest::newRow("space") << u"Jan Kowalski"_s << u"#[[Jan Kowalski]]"_s;
        QTest::newRow("plus") << u"C++"_s << u"#[[C++]]"_s;
        QTest::newRow("hash") << u"C#"_s << u"#[[C#]]"_s;
        QTest::newRow("digits") << u"2026"_s << u"#[[2026]]"_s;
        QTest::newRow("arabic digits") << u"١٢"_s << u"#[[١٢]]"_s;
        QTest::newRow("dot") << u"Dr. Nowak"_s << u"#[[Dr. Nowak]]"_s;
        QTest::newRow("only punctuation") << u"-_"_s << u"#[[-_]]"_s;
        QTest::newRow("emoji") << u"☕ kawa"_s << u"#[[☕ kawa]]"_s;
        QTest::newRow("NFD") << u"z\u0307o\u0301\u0142w"_s << u"#\u017C\u00F3\u0142w"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("blank") << u"   "_s << QString();
        QTest::newRow("slashes") << u"//"_s << QString();
        QTest::newRow("bracket") << u"a]b"_s << QString();
        QTest::newRow("open bracket") << u"a[b"_s << QString();
        QTest::newRow("pipe") << u"x|y"_s << QString();
        QTest::newRow("line break") << u"a\nb"_s << QString();
    }

    void tagSyntax()
    {
        QFETCH(QString, name);
        QFETCH(QString, tag);
        QCOMPARE(markdown::tagSyntax(name), tag);
        // What it writes reads back as the name (the key).
        if (!tag.isEmpty()) {
            const QStringList read = tags(u"x "_s + tag + u" y"_s);
            QCOMPARE(read.size(), 1);
            QCOMPARE(read.first().toLower(), name.trimmed()
                                                     .normalized(QString::NormalizationForm_C)
                                                     .toLower()
                                                     .remove(QRegularExpression(u"/+$"_s)));
        }
    }

    // What starts inside a formula is part of the formula (SYNTAX §11), as the editor shows it:
    // no tag, link or URL for the index; whatever starts first wins, as in the InlineRenderer.
    void formulasHideWhatStartsInside_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<QStringList>("links");
        QTest::newRow("tag and wiki link in formulas")
                << u"$x \\in A #B$ oraz $[[Strona]]$"_s << QStringList() << QStringList();
        // The Trilium importer writes 0.6x macros like this one as a formula.
        QTest::newRow("trilium macro")
                << u"Makro $% \\f is #1f(#2) \\int_{-\\infty}^\\infty \\hat\\xi\\,e^{2 \\pi i}$ koniec"_s
                << QStringList() << QStringList();
        QTest::newRow("tags around a formula")
                << u"#przed $x^2$ #po i [[Łąka]]"_s << QStringList({u"przed"_s, u"po"_s}) << QStringList({u"Łąka"_s});
        QTest::newRow("formula in a wiki alias")
                << u"[[Wzory|$x^2$ #wzór]] #tag"_s << QStringList({u"tag"_s}) << QStringList({u"Wzory"_s});
        QTest::newRow("formula in a link label") << u"[$\\alpha$ i #tag](Wzory.md) #źródło"_s
                                                 << QStringList({u"źródło"_s}) << QStringList({u"Wzory.md"_s});
        QTest::newRow("link starts before the formula")
                << u"[[a $b]] c$ #d"_s << QStringList({u"d"_s}) << QStringList({u"a $b"_s});
        QTest::newRow("url in a formula") << u"$f(https://example.org)$ i https://brulion.pl"_s << QStringList()
                                          << QStringList({u"https://brulion.pl"_s});
        QTest::newRow("embed and block ref in formulas")
                << u"$![[obraz.png]]$ $((abc123))$ ((def456))"_s << QStringList() << QStringList({u"def456"_s});
        QTest::newRow("prices are no formula")
                << u"Stawka 10$/h #pilne, w weekend 15$/h."_s << QStringList({u"pilne"_s}) << QStringList();
        QTest::newRow("escaped dollars") << u"\\$5 #tag \\$"_s << QStringList({u"tag"_s}) << QStringList();
        QTest::newRow("dollars in code") << u"`$x` #tag [[Ś]] `$`"_s << QStringList({u"tag"_s})
                                         << QStringList({u"Ś"_s});
        QTest::newRow("over a line break") << u"$a\n#b$ #c"_s << QStringList({u"c"_s}) << QStringList();
        QTest::newRow("not over a blank line") << u"$a\n\n#b$"_s << QStringList({u"b"_s}) << QStringList();
        QTest::newRow("display over lines") << u"$$\n#b [[S]]\n\n#c\n$$ #d"_s << QStringList({u"d"_s}) << QStringList();
    }

    void formulasHideWhatStartsInside()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, tags);
        QFETCH(QStringList, links);
        const InlineScan scan = extractInline(text);
        QStringList targets;
        for (const Link &l : scan.links)
            targets.append(l.target);
        QCOMPARE(scan.tags, tags);
        QCOMPARE(targets, links);
        QStringList names;
        for (const TagSpan &t : findTags(text))
            names.append(t.name);
        QCOMPARE(names, tags);
    }

    void offsetsAndLines()
    {
        auto r = extractInline(u"first\nsecond [[P]]"_s, 100, 7);
        QCOMPARE(r.links.first().line, 8);
        QCOMPARE(r.links.first().from, 113);
        QCOMPARE(r.links.first().to, 118);
        r = extractInline(u"a\r\nb [[P]]"_s, 10, 0);
        QCOMPARE(r.links.first().line, 1);
        QCOMPARE(r.links.first().from, 15);
        QCOMPARE(r.links.first().to, 20);
    }

    void garbage()
    {
        QString junk;
        for (int i = 0; i < 50; ++i)
            junk += u"[[((([](<>)!#\\`%%]]((!![[#^|]]](((("_s;
        extractInline(junk); // must not crash
        QVERIFY(true);
    }

    void unclosedLinkTitlesAreLinear()
    {
        // Review regression: a link title opened with '(' (or a quote) that never closes made every
        // '[' search to the end of the text (quadratic).
        QString s;
        for (int i = 0; i < 300000; ++i)
            s += u"[a](b ("_s;
        QString quotes;
        for (int i = 0; i < 100000; ++i)
            quotes += u"[ż](c \"x"_s;
        QElapsedTimer t;
        t.start();
        QVERIFY(extractInline(s).links.isEmpty());
        QVERIFY(extractInline(u"[a](b (tytuł)) [c](d \"t\")"_s).links.size() == 2);
        extractInline(quotes);
        QVERIFY2(t.elapsed() < 4000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
    }

    void pathologicalInputIsLinear()
    {
        QString brackets;
        for (int i = 0; i < 20000; ++i)
            brackets += u"[["_s;
        for (int i = 0; i < 20000; ++i)
            brackets += u"[("_s;
        QString url = u"see https://a.b/"_s;
        for (int i = 0; i < 5000; ++i)
            url += u"()"_s;
        const QString expectedUrl = url.sliced(4);
        url += QString(20000, u')');
        QString many;
        for (int i = 0; i < 60000; ++i)
            many += u"[[a]] "_s;
        QString openImages;
        QString openLinks;
        for (int i = 0; i < 100000; ++i) {
            openImages += u"!["_s;
            openLinks += u"[x "_s;
        }
        QElapsedTimer t;
        t.start();
        extractInline(brackets);
        const auto u = extractInline(url);
        const auto m = extractInline(many);
        extractInline(openImages);
        extractInline(openLinks);
        QVERIFY2(t.elapsed() < 4000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
        QCOMPARE(u.links.first().target, expectedUrl);
        QCOMPARE(m.links.size(), 60000);
    }

    void helpers()
    {
        auto w = detail::parseWikiInner(u"  A  #  H  | d ");
        QVERIFY(w);
        QCOMPARE(w->target, u"A"_s);
        QCOMPARE(w->heading, u"H"_s);
        QCOMPARE(w->display, u"d"_s);
        QVERIFY(!detail::parseWikiInner(u"|x"));
        QCOMPARE(detail::parseMdDestination(u"C:\\notes\\a.md").target, u"C:\\notes\\a.md"_s);
        QCOMPARE(detail::parseMdDestination(u"mailto:a@b.pl").target, u"mailto:a@b.pl"_s);
        QVERIFY(!detail::parseMdLink(u"[a](b", 0));
        QVERIFY(detail::hasUrlScheme(u"https://x"));
        QVERIFY(!detail::hasUrlScheme(u"C:/x"));
        QVERIFY(!detail::hasUrlScheme(u"Page.md"));
        QCOMPARE(uniqueTags({u"A"_s, u"a"_s, u"B"_s}), QStringList({u"A"_s, u"B"_s}));
    }
};

QTEST_GUILESS_MAIN(TestInline)
#include "tst_markdown_inline.moc"
