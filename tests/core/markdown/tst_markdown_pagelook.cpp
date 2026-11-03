// A page's icon and cover (markdown/pagelook.h, docs/SYNTAX.md §4 "Page icon and cover"): the
// values read (emoji, Iconize's Lucide names, Banners' covers and positions, Logseq lines), what
// Brulion writes, property round trips that change only the edited line, and the icons in front
// of rendered links.
#include "markdown/inlinerenderer.h"
#include "markdown/pagelook.h"
#include "markdown/pageprops.h"
#include "markdown/parse.h"

#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

PageIcon emojiIcon(const QString &e)
{
    return PageIcon{PageIcon::Kind::Emoji, e};
}

PageIcon appIcon(const QString &name)
{
    return PageIcon{PageIcon::Kind::App, name};
}

PageLook lookOf(const QString &content)
{
    return pageLook(parseNote(u"Strona.md"_s, content, VaultConfig()).properties);
}

/// The lines of `a` and `b` that differ (both split at '\n', the line endings kept).
int changedLines(const QString &a, const QString &b)
{
    const QStringList x = a.split(u'\n');
    const QStringList y = b.split(u'\n');
    if (x.size() != y.size())
        return -1;
    int n = 0;
    for (qsizetype i = 0; i < x.size(); ++i)
        n += x[i] != y[i] ? 1 : 0;
    return n;
}

} // namespace

class TestPageLook : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void icons_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<int>("kind"); // PageIcon::Kind
        QTest::addColumn<QString>("name");
        const int none = int(PageIcon::Kind::None);
        const int e = int(PageIcon::Kind::Emoji);
        const int app = int(PageIcon::Kind::App);
        QTest::newRow("emoji") << u"🚀"_s << e << u"🚀"_s;
        QTest::newRow("trimmed") << u"  📚 "_s << e << u"📚"_s;
        QTest::newRow("quoted") << u"\"🏠\""_s << e << u"🏠"_s;
        QTest::newRow("variation selector") << u"❤️"_s << e << u"❤️"_s;
        QTest::newRow("zwj family") << u"👨‍👩‍👧"_s << e << u"👨‍👩‍👧"_s;
        QTest::newRow("flag") << u"🇵🇱"_s << e << u"🇵🇱"_s;
        QTest::newRow("keycap") << u"1️⃣"_s << e << u"1️⃣"_s;
        QTest::newRow("two emoji") << u"🌱🌳"_s << e << u"🌱🌳"_s;
        QTest::newRow("symbol") << u"★"_s << e << u"★"_s;
        QTest::newRow("lucide") << u"LiBookOpen"_s << app << u"book-open"_s;
        QTest::newRow("lucide one word") << u"LiBell"_s << app << u"bell"_s;
        QTest::newRow("lucide digits") << u"LiHeading1"_s << app << u"heading-1"_s;
        QTest::newRow("lucide three words") << u"LiAlarmClock"_s << app << u"alarm-clock"_s;
        QTest::newRow("quoted lucide") << u"'LiCalendar'"_s << app << u"calendar"_s;
        // Nothing Brulion shows.
        QTest::newRow("empty") << QString() << none << QString();
        QTest::newRow("three emoji") << u"🌱🌳🌲"_s << none << QString();
        QTest::newRow("word") << u"dom"_s << none << QString();
        QTest::newRow("polish letters") << u"żółw"_s << none << QString();
        QTest::newRow("digit") << u"1"_s << none << QString();
        QTest::newRow("ascii punctuation") << u"-"_s << none << QString();
        QTest::newRow("emoji and word") << u"🚀 rakieta"_s << none << QString();
        QTest::newRow("font awesome") << u"FasHome"_s << none << QString();
        QTest::newRow("remix") << u"RiHomeLine"_s << none << QString();
        QTest::newRow("li lower") << u"Libook"_s << none << QString();
        QTest::newRow("li only") << u"Li"_s << none << QString();
        QTest::newRow("li with dash") << u"LiBook-Open"_s << none << QString();
        QTest::newRow("html") << u"<img src=x>"_s << none << QString();
        QTest::newRow("link") << u"[[🚀]]"_s << none << QString();
        QTest::newRow("line break") << u"🚀\n🚀"_s << none << QString();
        QTest::newRow("lone surrogate") << QString(QChar(0xD83D)) << none << QString();
        QTest::newRow("control") << u"🚀\u0007"_s << none << QString();
        QTest::newRow("too long") << QString(20, QChar(0x2605)) << none << QString();
    }

    void icons()
    {
        QFETCH(QString, value);
        QFETCH(int, kind);
        QFETCH(QString, name);
        const PageIcon icon = parsePageIcon(value);
        QCOMPARE(int(icon.kind), kind);
        QCOMPARE(icon.name, name);
        // What Brulion writes reads back as the same icon.
        if (!icon.isNone())
            QCOMPARE(parsePageIcon(pageIconValue(icon)), icon);
    }

    void iconValues()
    {
        QCOMPARE(pageIconValue(appIcon(u"book-open"_s)), u"LiBookOpen"_s);
        QCOMPARE(pageIconValue(appIcon(u"heading-1"_s)), u"LiHeading1"_s);
        QCOMPARE(pageIconValue(emojiIcon(u"🚀"_s)), u"🚀"_s);
        QCOMPARE(pageIconValue(PageIcon()), QString());
        QCOMPARE(pageIconValue(appIcon(u"Zła nazwa"_s)), QString());
        QCOMPARE(pageIconValue(appIcon(u"-x"_s)), QString());
        QCOMPARE(pageIconValue(emojiIcon(u"tekst"_s)), QString());
    }

    void covers_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<int>("kind"); // PageCover::Kind
        QTest::addColumn<QString>("target");
        const int none = int(PageCover::Kind::None);
        const int file = int(PageCover::Kind::File);
        const int web = int(PageCover::Kind::Web);
        QTest::newRow("embed") << QVariant(u"![[assets/okładka.jpg]]"_s) << file << u"assets/okładka.jpg"_s;
        QTest::newRow("link") << QVariant(u"[[okładka.png]]"_s) << file << u"okładka.png"_s;
        QTest::newRow("alias") << QVariant(u"[[góry.png|Góry]]"_s) << file << u"góry.png"_s;
        QTest::newRow("path") << QVariant(u"assets/Mój kot.png"_s) << file << u"assets/Mój kot.png"_s;
        QTest::newRow("quoted") << QVariant(u"\"![[a.png]]\""_s) << file << u"a.png"_s;
        QTest::newRow("markdown picture")
                << QVariant(u"![kot](assets/M%C3%B3j%20kot.png)"_s) << file << u"assets/Mój kot.png"_s;
        QTest::newRow("markdown angle") << QVariant(u"![](<assets/mój kot.png>)"_s) << file << u"assets/mój kot.png"_s;
        QTest::newRow("unquoted yaml link") << QVariant(QStringList{u"[[a.png]]"_s}) << file << u"a.png"_s;
        QTest::newRow("logseq list") << QVariant(QStringList{u"a.png"_s}) << file << u"a.png"_s;
        QTest::newRow("url") << QVariant(u"https://example.com/góry.jpg"_s) << web << u"https://example.com/góry.jpg"_s;
        QTest::newRow("url in picture") << QVariant(u"![](https://example.com/a.jpg)"_s) << web
                                        << u"https://example.com/a.jpg"_s;
        QTest::newRow("http") << QVariant(u"http://example.com/a.jpg"_s) << web << u"http://example.com/a.jpg"_s;
        // Not covers.
        QTest::newRow("empty") << QVariant(QString()) << none << QString();
        QTest::newRow("empty list") << QVariant(QStringList()) << none << QString();
        QTest::newRow("number") << QVariant(12) << none << QString();
        QTest::newRow("javascript") << QVariant(u"javascript:alert(1)"_s) << none << QString();
        QTest::newRow("data") << QVariant(u"data:image/png;base64,AAAA"_s) << none << QString();
        QTest::newRow("file url") << QVariant(u"file:///etc/passwd"_s) << none << QString();
        QTest::newRow("no host") << QVariant(u"https:///a.png"_s) << none << QString();
        QTest::newRow("line break") << QVariant(u"a.png\nb.png"_s) << none << QString();
        QTest::newRow("empty link") << QVariant(u"[[]]"_s) << none << QString();
        QTest::newRow("two links") << QVariant(u"[[a.png]] [[b.png]]"_s) << none << QString();
        QTest::newRow("picture and text") << QVariant(u"![](a.png) i tekst"_s) << none << QString();
        QTest::newRow("bang text") << QVariant(u"!a.png"_s) << none << QString();
    }

    void covers()
    {
        QFETCH(QVariant, value);
        QFETCH(int, kind);
        QFETCH(QString, target);
        const PageCover cover = parsePageCover(value);
        QCOMPARE(int(cover.kind), kind);
        QCOMPARE(cover.target, target);
        if (cover.kind != PageCover::Kind::None) {
            const PageCover again = parsePageCover(pageCoverValue(cover));
            QCOMPARE(again, cover);
        }
    }

    void positions_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<double>("position");
        QTest::newRow("number") << QVariant(0.25) << 0.25;
        QTest::newRow("zero") << QVariant(qlonglong(0)) << 0.0;
        QTest::newRow("one") << QVariant(qlonglong(1)) << 1.0;
        QTest::newRow("text") << QVariant(u"0.7"_s) << 0.7;
        QTest::newRow("decimal comma") << QVariant(u"0,3"_s) << 0.3;
        QTest::newRow("missing") << QVariant() << 0.5;
        QTest::newRow("too big") << QVariant(2.0) << 0.5;
        QTest::newRow("negative") << QVariant(u"-0.1"_s) << 0.5;
        QTest::newRow("percent") << QVariant(u"30%"_s) << 0.5;
        QTest::newRow("nan") << QVariant(u"nan"_s) << 0.5;
        QTest::newRow("word") << QVariant(u"środek"_s) << 0.5;
    }

    void positions()
    {
        QFETCH(QVariant, value);
        QFETCH(double, position);
        QCOMPARE(parseCoverPosition(value), position);
    }

    void positionValues()
    {
        QCOMPARE(coverPositionValue(0.5), u"0.5"_s);
        QCOMPARE(coverPositionValue(0.333), u"0.33"_s);
        QCOMPARE(coverPositionValue(0), u"0"_s);
        QCOMPARE(coverPositionValue(1), u"1"_s);
        QCOMPARE(coverPositionValue(7), u"1"_s);
        QCOMPARE(coverPositionValue(-1), u"0"_s);
        QCOMPARE(coverPositionValue(std::nan("")), u"0.5"_s);
    }

    void looks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("icon"); // "" none, else the PageIcon's name
        QTest::addColumn<QString>("cover");
        QTest::addColumn<double>("position");
        QTest::newRow("yaml") << u"---\nicon: 🚀\nbanner: \"![[assets/góry.jpg]]\"\nbanner_y: 0.2\n---\nTekst\n"_s
                              << u"🚀"_s << u"assets/góry.jpg"_s << 0.2;
        QTest::newRow("yaml unquoted link")
                << u"---\nbanner: [[góry.jpg]]\n---\n"_s << QString() << u"góry.jpg"_s << 0.5;
        QTest::newRow("upper-case keys") << u"---\nIcon: LiHouse\nBanner: https://example.com/a.png\n---\n"_s
                                         << u"house"_s << u"https://example.com/a.png"_s << 0.5;
        QTest::newRow("banners icon") << u"---\nbanner_icon: 🌳\n---\n"_s << u"🌳"_s << QString() << 0.5;
        QTest::newRow("icon wins") << u"---\nicon: 🚀\nbanner_icon: 🌳\n---\n"_s << u"🚀"_s << QString() << 0.5;
        QTest::newRow("unusable icon falls back")
                << u"---\nicon: FasHome\nbanner_icon: 🌳\n---\n"_s << u"🌳"_s << QString() << 0.5;
        QTest::newRow("logseq") << u"icon:: 📚\nbanner:: ![[assets/półka.png]]\n\n- blok\n"_s << u"📚"_s
                                << u"assets/półka.png"_s << 0.5;
        QTest::newRow("logseq link") << u"banner:: [[półka.png]]\n\n- blok\n"_s << QString() << u"półka.png"_s << 0.5;
        QTest::newRow("none") << u"# Tytuł\n"_s << QString() << QString() << 0.5;
        QTest::newRow("block property is no page icon") << u"- blok\n  icon:: 🚀\n"_s << QString() << QString() << 0.5;
        QTest::newRow("list icon") << u"---\nicon: [a, b]\n---\n"_s << QString() << QString() << 0.5;
    }

    void looks()
    {
        QFETCH(QString, content);
        QFETCH(QString, icon);
        QFETCH(QString, cover);
        QFETCH(double, position);
        const PageLook look = lookOf(content);
        QCOMPARE(look.icon.name, icon);
        QCOMPARE(look.cover.target, cover);
        QCOMPARE(look.cover.position, position);
    }

    /// Setting and removing the icon and the cover through the property edits changes only their
    /// lines: the rest of the file stays byte-for-byte.
    void propertyRoundTrips_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("new yaml icon") << u"---\ntags: [dom]\n# komentarz\n---\nZażółć gęślą jaźń\r\n"_s << u"icon"_s
                                       << u"🚀"_s
                                       << u"---\ntags: [dom]\n# komentarz\nicon: 🚀\n---\nZażółć gęślą jaźń\r\n"_s;
        QTest::newRow("replace icon") << u"---\nicon: 🌳  # drzewo\nb: 2\n---\n"_s << u"icon"_s << u"LiBookOpen"_s
                                      << u"---\nicon: LiBookOpen  # drzewo\nb: 2\n---\n"_s;
        QTest::newRow("cover is quoted") << u"---\na: 1\n---\n"_s << u"banner"_s << u"![[assets/góry.jpg]]"_s
                                         << u"---\na: 1\nbanner: \"![[assets/góry.jpg]]\"\n---\n"_s;
        QTest::newRow("web cover") << u"---\na: 1\n---\n"_s << u"banner"_s << u"https://example.com/a.jpg"_s
                                   << u"---\na: 1\nbanner: https://example.com/a.jpg\n---\n"_s;
        QTest::newRow("no frontmatter") << u"# Tytuł\r\n\r\nTekst\r\n"_s << u"icon"_s << u"📚"_s
                                        << u"---\r\nicon: 📚\r\n---\r\n# Tytuł\r\n\r\nTekst\r\n"_s;
        QTest::newRow("logseq") << u"tags:: dom\n\n- blok\n"_s << u"icon"_s << u"📚"_s
                                << u"tags:: dom\nicon:: 📚\n\n- blok\n"_s;
        QTest::newRow("logseq replace") << u"icon:: 🌳\ntags:: dom\n\n- blok\n"_s << u"icon"_s << u"🚀"_s
                                        << u"icon:: 🚀\ntags:: dom\n\n- blok\n"_s;
        QTest::newRow("logseq cover") << u"tags:: dom\n\n- blok\n"_s << u"banner"_s << u"![[assets/a.png]]"_s
                                      << u"tags:: dom\nbanner:: ![[assets/a.png]]\n\n- blok\n"_s;
        QTest::newRow("position") << u"---\nbanner: \"![[a.png]]\"\n---\n"_s << u"banner_y"_s << u"0.25"_s
                                  << u"---\nbanner: \"![[a.png]]\"\nbanner_y: 0.25\n---\n"_s;
    }

    void propertyRoundTrips()
    {
        QFETCH(QString, content);
        QFETCH(QString, key);
        QFETCH(QString, value);
        QFETCH(QString, expected);
        const PageProperties props = readPageProperties(content);
        PropertySource source = props.addTarget();
        for (const PageProperty &p : props.items) {
            if (p.key.compare(key, Qt::CaseInsensitive) == 0)
                source = p.source;
        }
        const PropertyType type = key == u"banner_y" ? PropertyType::Number : PropertyType::Text;
        const QVariant v = key == u"banner_y" ? QVariant(value.toDouble()) : QVariant(value);
        const PropertyEditResult set = setPageProperty(content, source, key, v, type);
        QCOMPARE(set.content, expected);
        // It reads back as the look written.
        const PageLook look = lookOf(set.content);
        if (key == u"icon")
            QCOMPARE(pageIconValue(look.icon), value);
        else if (key == u"banner")
            QCOMPARE(pageCoverValue(look.cover), value);
        else
            QCOMPARE(coverPositionValue(look.cover.position), value);
        // Removing it again: the file as it was when the key was new; else one line changed.
        const PropertyEditResult removed = removePageProperty(set.content, source, key);
        QCOMPARE(removed.status, PropertyEditStatus::Changed);
        if (!readPageProperties(content).find(source, key))
            QCOMPARE(removed.content, content);
        else
            QCOMPARE(changedLines(content, set.content), 1);
    }

    void renderedLinkIcons()
    {
        RenderContext c;
        c.palette.link = u"L"_s;
        c.palette.unresolved = u"U"_s;
        c.resolvePage = [](const QString &target) -> std::optional<QString> {
            if (target == u"Brak")
                return std::nullopt;
            return target + u".md"_s;
        };
        c.pageIcon = [](const QString &target) {
            if (target == u"Dom" || target == u"Brak")
                return emojiIcon(u"🏠"_s);
            if (target == u"Książki")
                return appIcon(u"book-open"_s);
            return PageIcon();
        };
        const InlineRenderer r(c);
        const QString dom = u"<a href=\"page:Dom\" style=\"color:L;text-decoration:none\">"_s;
        QCOMPARE(r.render(u"[[Dom]]"_s), dom + u"🏠</a>&nbsp;"_s + dom + u"Dom</a>"_s);
        QCOMPARE(r.render(u"[[Dom|mój dom]]"_s), dom + u"🏠</a>&nbsp;"_s + dom + u"mój dom</a>"_s);
        QCOMPARE(r.render(u"[x](Dom)"_s), dom + u"🏠</a>&nbsp;"_s + dom + u"x</a>"_s);
        const QString books = u"page:Ksi%C4%85%C5%BCki"_s;
        QCOMPARE(r.render(u"[[Książki]]"_s), u"<a href=\""_s + books
                                                     + u"\" name=\"brulion-icon:book-open\" "
                                                       u"style=\"color:L;text-decoration:none;background-color:"
                                                       u"transparent\">&nbsp;&nbsp;&nbsp;</a>&nbsp;"
                                                       u"<a href=\""_s
                                                     + books
                                                     + u"\" style=\"color:L;text-decoration:none\">Książki</a>"_s);
        // No icon: a page without one, a missing page, a link to a site.
        QCOMPARE(r.render(u"[[Inna]]"_s), u"<a href=\"page:Inna\" style=\"color:L;text-decoration:none\">Inna</a>"_s);
        QCOMPARE(r.render(u"[[Brak]]"_s), u"<a href=\"page:Brak\" style=\"color:U;text-decoration:none\">Brak</a>"_s);
        QVERIFY(!r.render(u"[x](https://example.com/Dom)"_s).contains(u"🏠"));
        // The hover underline still finds the link's own anchor.
        QVERIFY(InlineRenderer::withHoveredLink(r.render(u"[[Książki]]"_s), books).contains(u"underline\">Książki"));
    }
};

QTEST_GUILESS_MAIN(TestPageLook)
#include "tst_markdown_pagelook.moc"
