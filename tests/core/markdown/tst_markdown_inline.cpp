// Inline syntax: wiki links, embeds, block references, Markdown links, URLs and tags.
// Test cases ported from archive/web-v1 (inline tests).
#include "markdown/inline.h"
#include "markdown/inline_p.h"

#include <QElapsedTimer>
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
        QVERIFY2(t.elapsed() < 4000, qPrintable(QString::number(t.elapsed())));
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
        QVERIFY2(t.elapsed() < 4000, qPrintable(QString::number(t.elapsed())));
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
