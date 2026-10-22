// HTML -> Markdown conversion (QTextDocument parsing, own Markdown writer) and Markdown escaping.
#include "iotest.h"

#include "io/htmlmarkdown.h"
#include "io/htmlrender_p.h"
#include "io/htmlutil_p.h"
#include "markdown/inline.h"
#include "markdown/inlinerenderer.h"
#include "markdown/math.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTest>
#include <QTextDocument>

#include <limits>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

constexpr int NoListLimit = std::numeric_limits<int>::max();

/// The parts joined in order. The parts of a braced list are evaluated from left to right; the
/// operands of `+` are not sequenced, so with random parts the documents would depend on the
/// compiler (GCC on aarch64 and on x86_64 build different ones).
QString cat(std::initializer_list<QString> parts)
{
    QString s;
    for (const QString &part : parts)
        s += part;
    return s;
}

/// Random HTML with short lists in every shape QTextDocument has to cope with: nesting,
/// numbering attributes, items without end tags, empty and self-closing items, code and
/// paragraphs inside items, stray text and end tags, table/definition elements that close the
/// list implicitly, comments, entities, `<` in text.
class HtmlListGenerator {
public:
    explicit HtmlListGenerator(quint32 seed) : m_r(seed) { }

    QString document()
    {
        QString s;
        if (chance(20))
            s += u"<html><head><title>Tytuł</title></head><body>"_s;
        const int blocks = 1 + below(4);
        for (int b = 0; b < blocks; ++b) {
            switch (below(10)) {
            case 0: s += u"<p>"_s + inlineContent() + u"</p>"_s; break;
            case 1: s += u"<blockquote>"_s + list(0) + u"</blockquote>"_s; break;
            case 2: s += u"<table><tr><td>"_s + list(0) + u"</td><td>x</td></tr></table>"_s; break;
            case 3: s += u"<pre>kod\n  wcięty</pre>"_s; break;
            case 4: s += cat({u"<div>"_s, inlineContent(), list(1), u"</div>"_s}); break;
            case 5: s += cat({u"<h2>"_s, word(), u"</h2>"_s, list(0)}); break;
            default: s += list(0); break;
            }
            if (chance(20))
                s += pick({u"\n"_s, u"<hr>"_s, u"tekst luzem"_s, u"<p>"_s, u"</div>"_s});
        }
        if (chance(3))
            s += pick({u"</br>"_s, u"<u&#108;><li>x</ul>"_s, u"<!--StartFragment-->"_s});
        return s;
    }

private:
    int below(int n) { return int(m_r.bounded(n)); }
    bool chance(int percent) { return below(100) < percent; }
    QString pick(std::initializer_list<QString> options) { return *(options.begin() + below(int(options.size()))); }

    QString word()
    {
        if (chance(15)) // `<` opens an element for QTextHtmlParser
            return pick({u"x < y"_s, u"a <3"_s, u"1 <= 2"_s});
        return pick({u"żółw"_s, u"Zażółć gęślą jaźń"_s, u"Łódź"_s, u"kot"_s, u"a &amp; b"_s, u"1."_s, u"**"_s, u"[ ]"_s,
                     u"  "_s, u"#tag"_s, u"&nbsp;"_s, u"ćma"_s, u"- nie lista"_s, u"źdźbło"_s, u"pójdź"_s,
                     u"x &lt; y"_s});
    }

    QString inlineContent()
    {
        QString s;
        const int n = below(4);
        for (int i = 0; i < n; ++i) {
            switch (below(14)) {
            case 0: s += cat({u"<b>"_s, word(), chance(70) ? u"</b>"_s : QString()}); break;
            case 1: s += u"<i>"_s + word() + u"</i>"_s; break;
            case 2: s += u"<a href=\"https://x.org/a\">"_s + word() + u"</a>"_s; break;
            case 3: s += u"<br>"_s; break;
            case 4: s += u"<img src=\"https://x.org/i.png\" alt=\"obraz\">"_s; break;
            case 5: s += u"<input type=checkbox"_s + (chance(50) ? u" checked"_s : QString()) + u"> "_s; break;
            case 6: s += u"<code>k`d</code>"_s; break;
            case 7: s += u"<span style=\"background-color:#ff0\">"_s + word() + u"</span>"_s; break;
            case 8: s += u"<!-- komentarz <li> -->"_s; break;
            case 9: s += u"<wbr>"_s; break;
            case 10: s += u"<p>"_s + word(); break;
            default: s += word() + u' '; break;
            }
        }
        return s;
    }

    QString listAttributes()
    {
        QString a;
        const int n = below(3);
        for (int i = 0; i < n; ++i) {
            switch (below(13)) {
            case 0: a += u" start=\"%1\""_s.arg(int(m_r.bounded(-3, 30))); break;
            case 1: a += u" start=x"_s; break;
            case 2: a += u" type=\""_s + pick({u"a"_s, u"A"_s, u"i"_s, u"1"_s, u"square"_s, u"none"_s}) + u'"'; break;
            case 3: a += u" style=\"list-style-type:decimal\""_s; break;
            case 4: a += u" style=\"margin:0px\""_s; break;
            case 5: a += u" style=\"margin-top:0; margin-bottom:5px; -qt-list-indent:2\""_s; break;
            case 6: a += u" class='todo-list'"_s; break;
            case 7: a += u" START=3"_s; break;
            case 8: a += u" id=lista"_s; break;
            case 9: a += u" data-x=\"a>b\""_s; break;
            case 10: a += u" / "_s; break;
            case 11: a += u" style=\"-qt-list-number-prefix:'(';\""_s; break;
            default: a += u"\n title=\"Lista\""_s; break;
            }
        }
        return a;
    }

    QString itemContent(int depth)
    {
        if (chance(3))
            return pick({u"<pre>"_s, u"<pre style=\"margin:0\">"_s, u"<pre style=\"margin-top:0\">"_s})
                   + u"a  b</pre>"_s;
        switch (below(16)) {
        case 0: return QString();
        case 1: return depth < 3 ? cat({inlineContent(), list(depth + 1)}) : inlineContent();
        case 2: return depth < 3 ? cat({list(depth + 1), word()}) : word();
        case 3: return cat({u"<p>"_s, word(), u"</p><p>"_s, word(), u"</p>"_s});
        case 4: return u"<span style=\"white-space:nowrap\">"_s + word() + u"</span>"_s;
        case 5: return cat({u"<div>"_s, word(), u"</div>"_s, word()});
        case 6: return cat({pick({u"<dt>"_s, u"<dd>"_s, u"<td>"_s, u"<tr>"_s, u"<tbody>"_s, u"<caption>"_s}), word()});
        case 7:
            return cat(
                    {word(), pick({u"</b>"_s, u"</li>"_s, u"</ul>"_s, u"</ol>"_s, u"</div>"_s, u"</p>"_s, u"</>"_s})});
        case 8: return cat({u"<b>"_s, word(), u"<p>"_s, word()});
        case 9: return u"<table><tr><td>"_s + word() + u"</td></tr></table>"_s;
        default: return inlineContent();
        }
    }

    QString list(int depth)
    {
        const bool ordered = chance(50);
        QString tag = ordered ? u"ol"_s : u"ul"_s;
        if (chance(10))
            tag = tag.toUpper();
        QString s = u'<' + tag + listAttributes() + u'>';
        if (chance(10))
            s += u"luzem "_s;
        const int items = below(depth == 0 ? 12 : 5);
        for (int i = 0; i < items; ++i) {
            if (chance(25))
                s += pick({u"\n  "_s, u" "_s, u"<!-- x -->"_s, u"luzem"_s, u"\n"_s});
            // White space kept (white-space: pre, <pre>) makes a list stay whole: rarely.
            s += cat({u"<li"_s,
                      chance(3)
                              ? u" style=\"white-space:pre\""_s
                              : pick({QString(), QString(), QString(), u" class=\"checked\""_s, u" class=unchecked"_s,
                                      u" style=\"margin-top:0\""_s, u" style=\"margin-bottom:-5px\""_s, u" value=7"_s}),
                      chance(5) ? u"/>"_s : u">"_s, itemContent(depth)});
            if (chance(60))
                s += u"</li>"_s;
        }
        if (chance(85))
            s += u"</"_s + tag + u'>';
        return s;
    }

    QRandomGenerator m_r;
};

QString numberedItems(int from, int count, const QString &indent = QString())
{
    QString md;
    for (int i = 0; i < count; ++i)
        md += indent + QString::number(from + i) + u". pozycja "_s + QString::number(i) + u'\n';
    return md;
}

} // namespace

class TestIoHtml : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void convert_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("markdown");
        QTest::newRow("paragraphs") << u"<p>Akapit pierwszy</p><p>Zażółć gęślą jaźń</p>"_s
                                    << u"Akapit pierwszy\n\nZażółć gęślą jaźń\n"_s;
        QTest::newRow("div lines and blank div")
                << u"<div>Linia 1</div><div>Linia 2</div><div><br></div><div>Po przerwie</div>"_s
                << u"Linia 1\nLinia 2\n\nPo przerwie\n"_s;
        QTest::newRow("line breaks") << u"<p>a<br>b<br/>c</p>"_s << u"a\nb\nc\n"_s;
        QTest::newRow("bullet list nested")
                << u"<ul><li>a</li><li>b<ul><li>c</li></ul></li></ul><p>po</p>"_s << u"- a\n- b\n\t- c\n\npo\n"_s;
        QTest::newRow("ordered list") << u"<ol><li>jeden</li><li>dwa</li></ol>"_s << u"1. jeden\n2. dwa\n"_s;
        QTest::newRow("ordered list with start")
                << u"<ol start=\"5\"><li>piąty</li><li>szósty</li></ol>"_s << u"5. piąty\n6. szósty\n"_s;
        QTest::newRow("checkbox list")
                << u"<ul><li><input type=checkbox checked> zrobione</li><li><input type='checkbox'> otwarte</li></ul>"_s
                << u"- [x] zrobione\n- [ ] otwarte\n"_s;
        QTest::newRow("checkbox div") << u"<div><input type=\"checkbox\"/> Kup mleko</div><div>zwykły</div>"_s
                                      << u"- [ ] Kup mleko\n\nzwykły\n"_s;
        QTest::newRow("other inputs dropped") << u"<p>a <input type=text value=x> b</p>"_s << u"a b\n"_s;
        QTest::newRow("inline styles")
                << u"<p><b>bold</b> <i>it</i> <s>strike</s> <code>x*y</code> <mark>hl</mark> <u>pod</u></p>"_s
                << u"**bold** *it* ~~strike~~ `x*y` ==hl== pod\n"_s;
        QTest::newRow("css styles") << u"<p><span style=\"font-weight:bold\">mocno</span> "
                                       "<span style=\"font-family:Menlo, monospace\">kod</span> "
                                       "<span style=\"background-color: rgb(255, 250, 165)\">żółte</span></p>"_s
                                    << u"**mocno** `kod` ==żółte==\n"_s;
        QTest::newRow("spaces outside markers") << u"<p><b> spaced </b>x</p>"_s << u"**spaced** x\n"_s;
        QTest::newRow("code with backtick") << u"<p><code>a`b</code></p>"_s << u"`` a`b ``\n"_s;
        QTest::newRow("links") << u"<p><a href=\"https://example.com/a b\">Przykład</a> i "
                                  "<a href=\"https://x.org\">https://x.org</a></p>"_s
                               << u"[Przykład](https://example.com/a%20b) i <https://x.org>\n"_s;
        QTest::newRow("mailto") << u"<p><a href=\"mailto:jan@x.pl\">jan@x.pl</a></p>"_s << u"<mailto:jan@x.pl>\n"_s;
        QTest::newRow("unsafe links keep text") << u"<p><a href=\"javascript:alert(1)\">kliknij</a> "
                                                   "<a href=\"#sekcja\">kotwica</a></p>"_s
                                                << u"kliknij kotwica\n"_s;
        QTest::newRow("bold inside link")
                << u"<p><a href=\"https://x.org\"><b>B</b> t</a></p>"_s << u"[**B** t](https://x.org)\n"_s;
        // Brackets in a link label or an image alt would end the label (or make a wiki link).
        QTest::newRow("brackets in labels")
                << u"<p><a href=\"https://x.org\">a]b</a> <a href=\"https://x.org/(x)\">[x]</a> "
                   "<img src=\"https://x.org/i.png\" alt=\"[[alt]]\"></p>"_s
                << u"[a\\]b](https://x.org) [\\[x\\]](https://x.org/%28x%29) ![\\[\\[alt\\]\\]](https://x.org/i.png)\n"_s;
        // Attributes are read in order: a name inside another attribute's quoted value is text.
        QTest::newRow("attribute inside a value") << u"<p><img alt=\"x src=evil.png\" src=\"https://x.org/good.png\"> "
                                                     "<input value='type=checkbox checked' type=text>t</p>"_s
                                                  << u"![x src=evil.png](https://x.org/good.png) t\n"_s;
        QTest::newRow("quoted > in attributes")
                << u"<p><img alt=\"a > b\" src=\"https://x.org/c.png\"><input title='x>y' type=checkbox checked> z</p>"_s
                << u"![a > b](https://x.org/c.png)[x] z\n"_s;
        QTest::newRow("task-like text in list") << u"<ul><li>[ ] nie zadanie</li><li>[x] też nie</li></ul>"_s
                                                << u"- \\[ ] nie zadanie\n- \\[x] też nie\n"_s;
        QTest::newRow("images") << u"<p><img src=\"https://x.org/a.png\" alt=\"Obraz\"> "
                                   "<img src=\"data:image/png;base64,AAAA\"> <img src=\"local dir/b.png\"></p>"_s
                                << u"![Obraz](https://x.org/a.png) ![](local%20dir/b.png)\n"_s;
        QTest::newRow("table") << u"<table><tr><th>Nazwa</th><th>Cena</th></tr>"
                                  "<tr><td>a|b</td><td>5 <b>zł</b></td></tr><tr><td></td><td>x<br>y</td></tr></table>"_s
                               << u"| Nazwa | Cena |\n| --- | --- |\n| a\\|b | 5 **zł** |\n|  | x y |\n"_s;
        QTest::newRow("pre with language")
                << u"<pre><code class=\"language-cpp\">int x = 1;\nreturn x;</code></pre><p>po</p>"_s
                << u"```cpp\nint x = 1;\nreturn x;\n```\n\npo\n"_s;
        QTest::newRow("pre with fence inside") << u"<pre>```\nx\n```</pre>"_s << u"````\n```\nx\n```\n````\n"_s;
        QTest::newRow("two pre blocks") << u"<pre>a</pre><pre>b</pre>"_s << u"```\na\n```\n\n```\nb\n```\n"_s;
        // Code is content: blank-line runs and indentation inside <pre> are kept as written.
        QTest::newRow("pre keeps blank line runs")
                << u"<pre><code class=\"language-py\">def a():\n    pass\n\n\ndef b():\n    pass</code></pre>"_s
                << u"```py\ndef a():\n    pass\n\n\ndef b():\n    pass\n```\n"_s;
        // Private-use characters of the input (icon fonts) never turn into checkboxes or fences.
        QTest::newRow("private use input") << u"<p>\uE000 ikona \uE001 \uE002js\uE003</p><pre>\uE002x\uE003kod</pre>"_s
                                           << u"ikona js\n\n```\nxkod\n```\n"_s;
        QTest::newRow("quote") << u"<blockquote><p>Cytat</p><p>dalej</p></blockquote><p>po</p>"_s
                               << u"> Cytat\n>\n> dalej\n\npo\n"_s;
        QTest::newRow("nested quote with list")
                << u"<blockquote><p>A</p><blockquote><p>B</p></blockquote><ul><li>c</li></ul></blockquote>"
                   "<p style=\"margin-left:40px\">wcięty, nie cytat</p>"_s
                << u"> A\n>\n>> B\n>\n> - c\n\nwcięty, nie cytat\n"_s;
        QTest::newRow("rule") << u"<p>a</p><hr><p>b</p>"_s << u"a\n\n---\n\nb\n"_s;
        QTest::newRow("headings") << u"<h1>Tytuł</h1><h2>Sekcja <b>x</b></h2><h6>mały</h6>"_s
                                  << u"# Tytuł\n\n## Sekcja x\n\n###### mały\n"_s;
        QTest::newRow("escaping") << u"<p>* nie lista</p><p># nie nagłówek</p><p>1. nie numer</p>"
                                     "<p>a *b* [[x]] `c` ==d== %%e%% ((f)) &lt;tag&gt; ^id</p>"_s
                                  << u"\\* nie lista\n\n\\# nie nagłówek\n\n1\\. nie numer\n\n"
                                     "a \\*b\\* \\[\\[x\\]\\] \\`c\\` \\==d\\== \\%%e\\%% \\((f)) \\<tag> \\^id\n"_s;
        QTest::newRow("entities and nbsp") << u"<p>a&nbsp;b &amp; c &lt; d</p>"_s << u"a b & c < d\n"_s;
        QTest::newRow("scripts and styles dropped")
                << u"<html><head><style>p{color:red}</style><script>alert('x')</script></head>"
                   "<body><p>Treść</p><script>evil()</script><noscript>ns</noscript></body></html>"_s
                << u"Treść\n"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("whitespace only") << u"<p>  </p><div><br></div>"_s << QString();
        // Removed elements take only themselves: a void <embed>, a self-closing <svg/>, an
        // unclosed <video> (its start tag); an unclosed <script> runs to the end as in browsers.
        QTest::newRow("embed is void") << u"<p>a</p><embed src=\"x.swf\"><p>b</p>"_s << u"a\n\nb\n"_s;
        QTest::newRow("self-closing svg") << u"<p>a</p><svg viewBox=\"0 0 1 1\"/><p>b</p>"_s << u"a\n\nb\n"_s;
        QTest::newRow("unclosed video") << u"<p>a</p><video src=\"v.mp4\"><p>b</p>"_s << u"a\n\nb\n"_s;
        QTest::newRow("embed in object") << u"<p>a</p><object><embed src=x></object><p>b</p>"_s << u"a\n\nb\n"_s;
        QTest::newRow("unclosed script") << u"<p>a</p><script>evil()<p>b</p>"_s << u"a\n"_s;
        QTest::newRow("code span with backticks") << u"<p><code>a``b</code></p>"_s << u"``` a``b ```\n"_s;
        // Dollars of the text never make a formula (SYNTAX §11), also across styles, lines and the
        // pieces of a paragraph; prices that pair with nothing, list items, table cells and
        // separate paragraphs stay as written; code and link destinations are never changed.
        QTest::newRow("dollars across styles")
                << u"<p>Cena $10, <b>Netflix</b> 15$</p>"_s << u"Cena \\$10, **Netflix** 15$\n"_s;
        QTest::newRow("dollars across lines") << u"<p>Bilet $10<br>parking 5$</p>"_s << u"Bilet \\$10\nparking 5$\n"_s;
        QTest::newRow("dollars in two paragraphs")
                << u"<p>Bilet $10</p><p>parking 5$</p>"_s << u"Bilet $10\n\nparking 5$\n"_s;
        QTest::newRow("dollars of two list items")
                << u"<ul><li>$10 bilet</li><li>parking 5$</li></ul>"_s << u"- $10 bilet\n- parking 5$\n"_s;
        QTest::newRow("dollars of table cells") << u"<table><tr><td>$15</td><td>10$</td><td>$x$</td></tr></table>"_s
                                                << u"| $15 | 10$ | \\$x$ |\n| --- | --- | --- |\n"_s;
        QTest::newRow("dollars of code and links")
                << u"<p><code>$x$</code> i <a href=\"https://x.org/?a=$5\">cena $5</a> 10$</p><pre>$y$ i $z$</pre>"_s
                << u"`$x$` i [cena \\$5](https://x.org/?a=$5) 10\\$\n\n```\n$y$ i $z$\n```\n"_s;
        QTest::newRow("display formula lines")
                << u"<div>$$</div><div>a + b</div><div>$$</div>"_s << u"\\$\\$\na + b\n$$\n"_s;
        QTest::newRow("prices pairing with nothing") << u"<p>$5 i $10</p><p>15$, stawka 10$/h, $PATH:$HOME</p>"_s
                                                     << u"$5 i $10\n\n15$, stawka 10$/h, $PATH:$HOME\n"_s;
    }

    void convert()
    {
        QFETCH(QString, html);
        QFETCH(QString, markdown);
        QCOMPARE(htmlToMarkdown(html).markdown, markdown);
    }

    void title()
    {
        const HtmlMarkdown r =
                htmlToMarkdown(u"<html><head><title>  Mój\n tytuł </title></head><body>x</body></html>"_s);
        QCOMPARE(r.title, u"Mój tytuł"_s);
        QCOMPARE(r.markdown, u"x\n"_s);
        QCOMPARE(htmlToMarkdown(u"<p>no title</p>"_s).title, QString());
    }

    void callbacks()
    {
        QList<HtmlImage> seen;
        HtmlToMarkdownOptions o;
        o.image = [&](const HtmlImage &img) {
            seen.append(img);
            return img.src.startsWith(u"data:"_s) ? u"![[assets/x.png]]"_s : QString();
        };
        o.link = [](const QString &href, const QString &label) -> std::optional<QString> {
            if (href == u"inna.html")
                return u"[["_s + label + u"]]"_s;
            return std::nullopt;
        };
        const HtmlMarkdown r = htmlToMarkdown(u"<p><img src=\"/etc/passwd\"><img src=\"file:///dev/zero\" alt=\"zero\">"
                                              "<img src='data:image/png;base64,iVBO' alt=\"Wykres &amp; dane\"></p>"
                                              "<p><a href=\"inna.html\">Inna</a> <a href=\"https://x.org\">x</a></p>"_s,
                                              o);
        QCOMPARE(seen.size(), 3);
        QCOMPARE(seen[0].src, u"/etc/passwd"_s);
        QCOMPARE(seen[1].src, u"file:///dev/zero"_s);
        QCOMPARE(seen[1].alt, u"zero"_s);
        QCOMPARE(seen[2].alt, u"Wykres & dane"_s);
        QCOMPARE(r.markdown, u"![[assets/x.png]]\n\n[[Inna]] [x](https://x.org)\n"_s);
    }

    void neverLoadsResources()
    {
        // A stylesheet link, an import and background images must not be read (no hang on
        // /dev/zero, no file access).
        const HtmlMarkdown r = htmlToMarkdown(
                u"<link rel=\"stylesheet\" href=\"file:///dev/zero\"><style>@import url(/dev/zero);</style>"
                "<table background=\"/dev/zero\"><tr><td style=\"background-image:url(/dev/zero)\">x</td></tr></table>"_s);
        QVERIFY(r.markdown.contains(u"x"_s));
    }

    void malformed()
    {
        // Unclosed tags, stray closers, deep nesting: no crash, text survives.
        QString deep;
        for (int i = 0; i < 300; ++i)
            deep += u"<div><ul><li>"_s;
        deep += u"głęboko"_s;
        QVERIFY(htmlToMarkdown(deep).markdown.contains(u"głęboko"_s));
        QVERIFY(htmlToMarkdown(u"<p><b>a<i>b</p></b>c</i><table><tr><td>d"_s).markdown.contains(u'd'));
        QVERIFY(htmlToMarkdown(u"</div></p><<<>>>&#xFFFFFF;&#0;"_s).markdown.size() < 40);
    }

    void deepNesting_data()
    {
        QTest::addColumn<QString>("html");
        // About 1 MB each: QTextDocument needs time quadratic in the depth, so nesting deeper
        // than 64 elements is flattened (text and shallow structure stay).
        QTest::newRow("spans") << u"<span>"_s.repeated(150'000) + u"głęboko"_s;
        QTest::newRow("divs") << u"<div>"_s.repeated(150'000) + u"głęboko"_s;
        QTest::newRow("closed divs") << u"<div>"_s.repeated(80'000) + u"głęboko"_s + u"</div>"_s.repeated(80'000);
        QTest::newRow("lists") << u"<ol><li>"_s.repeated(60'000) + u"głęboko"_s;
        QTest::newRow("unclosed paragraphs") << u"<p>akapit"_s.repeated(5'000) + u"<p>głęboko"_s;
    }

    void deepNesting()
    {
        QFETCH(QString, html);
        QElapsedTimer timer;
        timer.start();
        const QString md = htmlToMarkdown(html).markdown;
        QVERIFY2(timer.elapsed() < 20000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(md.contains(u"głęboko"_s));
    }

    void unclosedParagraphsStaySeparate()
    {
        // Elements HTML closes implicitly do not count as nesting.
        const QString md = htmlToMarkdown(u"<p>akapit"_s.repeated(200)).markdown;
        QCOMPARE(md.count(u"akapit"_s), 200);
        QCOMPARE(md.count(u"\n\n"_s), 199);
    }

    void longLists_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("markdown");
        QString html = u"<ul>"_s;
        QString md;
        for (int i = 1; i <= 250; ++i) {
            html += u"<li>punkt %1 żółw</li>"_s.arg(i);
            md += u"- punkt %1 żółw\n"_s.arg(i);
        }
        QTest::newRow("bullets") << html + u"</ul><p>po liście</p>"_s << md + u"\npo liście\n"_s;

        // Items without end tags, an <ol start>: the numbering runs on over the parts.
        html = u"<p>przed</p><ol start=\"5\" type=\"1\">"_s;
        for (int i = 0; i < 450; ++i)
            html += u"\n<li>pozycja "_s + QString::number(i);
        QTest::newRow("ordered from 5") << html + u"</ol>"_s << u"przed\n\n"_s + numberedItems(5, 450);

        html = u"<ol>"_s;
        md.clear();
        for (int i = 0; i < 330; ++i) {
            html += u"<li>pozycja "_s + QString::number(i);
            md += QString::number(i + 1) + u". pozycja "_s + QString::number(i) + u'\n';
            if (i % 10 == 0) {
                html += u"<ul><li>a</li><li><b>b</b></li></ul>"_s;
                md += u"\t- a\n\t- **b**\n"_s;
            }
            html += u"</li>"_s;
        }
        QTest::newRow("nested") << html + u"</ol>"_s << md;

        // A long nested list is split too; the outer numbering is unaffected.
        html = u"<ol><li>pierwszy<ol>"_s;
        for (int i = 0; i < 120; ++i)
            html += u"<li>pozycja "_s + QString::number(i) + u"</li>"_s;
        html += u"</ol></li><li>drugi</li></ol>"_s;
        QTest::newRow("long nested list")
                << html << u"1. pierwszy\n"_s + numberedItems(1, 120, u"\t"_s) + u"2. drugi\n"_s;

        html = u"<blockquote><ul>"_s;
        md.clear();
        for (int i = 0; i < 220; ++i) {
            html += u"<li><input type=checkbox%1> zadanie %2</li>"_s.arg(i % 3 ? QString() : u" checked"_s).arg(i);
            md += u"> - [%1] zadanie %2\n"_s.arg(i % 3 ? u" "_s : u"x"_s).arg(i);
        }
        QTest::newRow("tasks in a quote") << html + u"</ul></blockquote>"_s << md;

        // Empty items: QTextDocument reuses their block for the next item, so they are not
        // counted; the numbering of the parts follows QTextDocument's count.
        html = u"<ol>"_s;
        md.clear();
        int number = 0;
        for (int i = 0; i < 300; ++i) {
            if (i % 7 == 3) {
                html += u"<li></li>"_s;
                continue;
            }
            html += u"<li>pozycja "_s + QString::number(i) + u"</li>"_s;
            md += QString::number(++number) + u". pozycja "_s + QString::number(i) + u'\n';
        }
        QTest::newRow("empty items") << html + u"</ol>"_s << md;

        // Two long lists, one after the other: still two lists (a blank line between them).
        html = u"<ol>"_s;
        for (int i = 0; i < 150; ++i)
            html += u"<li>pozycja "_s + QString::number(i) + u"</li>"_s;
        html += u"</ol><ol start=\"1000\">"_s;
        for (int i = 0; i < 150; ++i)
            html += u"<li>pozycja "_s + QString::number(i) + u"</li>"_s;
        QTest::newRow("two lists") << html + u"</ol>"_s << numberedItems(1, 150) + numberedItems(1000, 150);
    }

    void longLists()
    {
        QFETCH(QString, html);
        QFETCH(QString, markdown);
        int parts = 0;
        detail::splitLongLists(html, 100, &parts);
        QVERIFY(parts > 0);
        QCOMPARE(htmlToMarkdown(html).markdown, markdown);
        QCOMPARE(detail::htmlToMarkdownWithListLimit(html, {}, NoListLimit).markdown, markdown);
        QCOMPARE(htmlToPlainText(html), detail::htmlToPlainTextWithListLimit(html, NoListLimit));
    }

    void splitListsKeepOutput()
    {
        // Property: splitting lists (after every item, or every 2nd / 3rd) changes nothing in the
        // Markdown or the plain text of any document.
        constexpr int Documents = 1500;
        HtmlListGenerator gen(20261017);
        int split = 0; // documents with a list split after every item
        for (int n = 0; n < Documents; ++n) {
            const QString html = gen.document();
            const QString whole = detail::htmlToMarkdownWithListLimit(html, {}, NoListLimit).markdown;
            for (const int limit : {1, 2, 3}) {
                int parts = 0;
                detail::splitLongLists(html, limit, &parts);
                if (limit == 1 && parts > 0)
                    ++split;
                const QString md = detail::htmlToMarkdownWithListLimit(html, {}, limit).markdown;
                QVERIFY2(md == whole,
                         qPrintable(u"limit %1, html:\n%2\nwhole:\n%3\nsplit:\n%4"_s.arg(limit).arg(html, whole, md)));
            }
            const QString text = detail::htmlToPlainTextWithListLimit(html, NoListLimit);
            QVERIFY2(detail::htmlToPlainTextWithListLimit(html, 1) == text, qPrintable(html));
        }
        QVERIFY2(split > Documents / 4, qPrintable(QString::number(split))); // the splitting was exercised
    }

    void splitLongListsLeavesOthers_data()
    {
        QTest::addColumn<QString>("html");
        const QString items = u"<li>a</li>"_s.repeated(5);
        QTest::newRow("short list") << u"<ul>"_s + u"<li>a</li>"_s.repeated(3) + u"</ul>"_s;
        QTest::newRow("start fragment") << u"<!--StartFragment--><ul>"_s + items + items + u"</ul>"_s;
        QTest::newRow("void end tag") << u"<ul>"_s + items + u"</br>"_s + items + u"</ul>"_s;
        QTest::newRow("entity in a tag name") << u"<ul>"_s + items + u"<l&#105;>b"_s + items + u"</ul>"_s;
        QTest::newRow("slash in the start tag") << u"<ul / class=x>"_s + items + items + u"</ul>"_s;
        QTest::newRow("unquoted value with a quote") << u"<ul class=a\"b>"_s + items + items + u"</ul>"_s;
        QTest::newRow("single quote after backslash") << u"<ol title='a\\'>"_s + items + items + u"</ol>"_s;
        QTest::newRow("items inside an inline element") << u"<ul><li><b>x"_s + items + items + u"</ul>"_s;
        QTest::newRow("no list") << u"<p>"_s + u"<li>a"_s.repeated(10) + u"</p>"_s;
    }

    void splitLongListsLeavesOthers()
    {
        QFETCH(QString, html);
        int parts = -1;
        QCOMPARE(detail::splitLongLists(html, 3, &parts), html);
        QCOMPARE(parts, 0);
    }

    void splitLongListsSplitsAtItems()
    {
        int parts = 0;
        QString split = detail::splitLongLists(u"<OL start=3 class=\"x\">\n<li>a<li>b</li> <li>c<ol><li>d</ol>e</ol>"_s,
                                               2, &parts);
        QCOMPARE(parts, 1);
        // The parts' number prefix: a marker, a nonce (hash of the document), the list's number.
        static const QRegularExpression nonce(u"\uE004[0-9a-f]+:"_s);
        QVERIFY(split.contains(nonce));
        split.replace(nonce, u"#:"_s);
        QCOMPARE(
                split,
                u"<ol start=3 class=\"x\" style=\"-qt-list-number-prefix:'#:0'; margin-bottom:0px\">\n<li>a<li>b</li> "
                "</ol><ol start=3 class=\"x\" style=\"-qt-list-number-prefix:'#:0'; margin-top:0px\"><li>c<ol><li>d</ol>e</ol>"_s);
    }

    void listPartPrefixCannotBeForged()
    {
        // A document's own -qt-list-number-prefix (a CSS escape of the marker) never makes a
        // list continue the numbering of a split one; the marker character in text stays text.
        QString html = u"<ol>"_s + u"<li>a</li>"_s.repeated(60) + u"</ol>"_s;
        for (const QString &prefix : {u"\\E004 0"_s, u"\\E004 0:0"_s, u"\\E004 "_s})
            html += u"<p>p</p><ol style=\"-qt-list-number-prefix:'"_s + prefix + u"'\"><li>b</li><li>c</li></ol>"_s;
        const QString md = htmlToMarkdown(html).markdown;
        QString split; // the 60 items are split (50 + 10) and still numbered 1 … 60
        for (int i = 1; i <= 60; ++i)
            split += QString::number(i) + u". a\n"_s;
        QVERIFY(md.startsWith(split));
        QCOMPARE(md.count(u"1. b\n2. c\n"_s), 3);
        QCOMPARE(htmlToMarkdown(u"<p>x\uE004y</p>"_s).markdown, u"x\uE004y\n"_s);
    }

    void longListIsLinear_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<int>("lines");
        constexpr int Items = 20000;
        QString flat = u"<ul>"_s;
        QString ordered = u"<ol start=\"7\">"_s;
        QString nested = u"<ul>"_s;
        for (int i = 0; i < Items; ++i) {
            flat += u"<li>punkt zażółć "_s + QString::number(i) + u"</li>"_s;
            ordered += u"<li>pozycja "_s + QString::number(i);
            nested += u"<li>punkt "_s + QString::number(i)
                      + (i % 10 == 0 ? u"<ul><li>a</li><li>b</li></ul>"_s : QString()) + u"</li>"_s;
        }
        QTest::newRow("flat") << flat + u"</ul>"_s << Items;
        QTest::newRow("ordered") << ordered + u"</ol>"_s << Items;
        QTest::newRow("nested") << nested + u"</ul>"_s << Items + Items / 10 * 2;
    }

    void longListIsLinear()
    {
        // QTextDocument alone needs about 13 s for 20 000 items of one list (x86_64, Release).
        QFETCH(QString, html);
        QFETCH(int, lines);
        QElapsedTimer timer;
        timer.start();
        const QString md = htmlToMarkdown(html).markdown;
        QVERIFY2(timer.elapsed() < 4000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(md.count(u'\n'), lines);
        QVERIFY(md.endsWith(u"19999\n"_s));
    }

    void escape_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("lineStart");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain polish") << u"Zażółć gęślą jaźń"_s << true << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("lone star") << u"5 * 3"_s << true << u"5 * 3"_s;
        QTest::newRow("emphasis star") << u"a*b*"_s << true << u"a\\*b\\*"_s;
        QTest::newRow("intraword underscore") << u"snake_case_name"_s << true << u"snake_case_name"_s;
        QTest::newRow("underscore emphasis") << u"_x_"_s << true << u"\\_x\\_"_s;
        QTest::newRow("dash list") << u"- x"_s << true << u"\\- x"_s;
        QTest::newRow("dash list mid line") << u"- x"_s << false << u"- x"_s;
        QTest::newRow("plus list") << u"+ x"_s << true << u"\\+ x"_s;
        QTest::newRow("number paren") << u"12) x"_s << true << u"12\\) x"_s;
        QTest::newRow("quote") << u"> x"_s << true << u"\\> x"_s;
        QTest::newRow("heading") << u"## x"_s << true << u"\\## x"_s;
        QTest::newRow("tag kept") << u"#praca"_s << true << u"#praca"_s;
        QTest::newRow("rule") << u"---"_s << true << u"\\---"_s;
        QTest::newRow("tilde fence") << u"~~~"_s << true << u"\\~~~"_s;
        QTest::newRow("strike") << u"~~x~~"_s << false << u"\\~~x\\~~"_s;
        QTest::newRow("backslash") << u"a\\b \\* c\\"_s << false << u"a\\b \\\\\\* c\\\\"_s;
        QTest::newRow("markdown link") << u"[a](b)"_s << false << u"[a\\](b)"_s;
        QTest::newRow("embed") << u"![x]"_s << false << u"!\\[x]"_s;
        QTest::newRow("footnote") << u"[^1]"_s << false << u"\\[^1]"_s;
        QTest::newRow("html") << u"<b>x</b> a < b"_s << false << u"\\<b>x\\</b> a < b"_s;
        QTest::newRow("block id") << u"tekst ^abc"_s << false << u"tekst \\^abc"_s;
        QTest::newRow("checkbox at line start") << u"[ ] x"_s << true << u"\\[ ] x"_s;
        QTest::newRow("checkbox mid line") << u"[ ] x"_s << false << u"[ ] x"_s;
        // Any one character in brackets is a task state for Brulion (`[?]`, `[…]`).
        QTest::newRow("custom checkbox at line start") << u"[…] dalej"_s << true << u"\\[…] dalej"_s;
        QTest::newRow("checkbox alone") << u"[?]"_s << true << u"\\[?]"_s;
        QTest::newRow("brackets with a word") << u"[ab] x"_s << true << u"[ab] x"_s;
        // Dollars that would pair into a formula (SYNTAX §11): the opening one is escaped.
        QTest::newRow("prices pairing") << u"Cena $10, Netflix 15$"_s << false << u"Cena \\$10, Netflix 15$"_s;
        QTest::newRow("formula look-alikes") << u"$x$ i $y$"_s << true << u"\\$x$ i \\$y$"_s;
        QTest::newRow("display look-alike") << u"$$E=mc^2$$"_s << false << u"\\$\\$E=mc^2$$"_s;
        QTest::newRow("dollars pairing in turn") << u"$a $b$ c$"_s << false << u"\\$a \\$b$ c$"_s;
        QTest::newRow("dollar after a backslash") << u"a\\$x$"_s << false << u"a\\\\\\$x$"_s;
        QTest::newRow("prices pairing with nothing") << u"$5 i $10 za żółwia"_s << false << u"$5 i $10 za żółwia"_s;
        QTest::newRow("suffix prices pairing with nothing")
                << u"15$, 10$/h i $PATH:$HOME"_s << false << u"15$, 10$/h i $PATH:$HOME"_s;
        // A bare URL keeps its dollars (its link would change): the text's dollar is escaped.
        QTest::newRow("bare URL with a dollar") << u"Zobacz https://x.org/?p=$5 – kosztuje 10$"_s << false
                                                << u"Zobacz https://x.org/?p=$5 – kosztuje 10\\$"_s;
    }

    void escape()
    {
        QFETCH(QString, text);
        QFETCH(bool, lineStart);
        QFETCH(QString, expected);
        QCOMPARE(escapeMarkdownText(text, lineStart), expected);
    }

    // What the importers write with escapeMarkdownText() reads back as the original text, in the
    // editor (InlineRenderer) and in the HTML export (HtmlRenderer): `\[`, `\(` are escapes, no
    // formulas (SYNTAX §11), and dollars of plain text stay text.
    void escapedTextShowsAsWritten_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("linkLabel");
        QTest::newRow("link label") << u"[1] Źródło"_s << true;
        QTest::newRow("brackets in a link label") << u"Rozdział [a] i [[b]]"_s << true;
        QTest::newRow("double brackets") << u"[[Nie link]] w tekście"_s << false;
        QTest::newRow("footnote, block ref, image") << u"Zobacz [^1] i ((x)) oraz ![obraz]"_s << false;
        QTest::newRow("citations") << u"Indeks[2] i Historia [edytuj], Geografia [edytuj]"_s << false;
        QTest::newRow("emphasis and comments")
                << u"a\\b, *gwiazdki*, _podkreślniki_, `kod`, ~~skreślenie~~, ==zaznaczenie==, %%uwaga%%"_s << false;
        QTest::newRow("markup") << u"<b>znacznik</b> i a < b"_s << false;
        QTest::newRow("prefix prices and variables") << u"Cena $5 i $10, $PATH:$HOME/bin"_s << false;
        QTest::newRow("suffix prices") << u"Netflix 15$, Spotify 10$, stawka 10$/h"_s << false;
        QTest::newRow("prices pairing") << u"Bilet $10, parking 5$ – razem"_s << false;
        QTest::newRow("formula look-alikes") << u"Wzór $x$, $$y$$ i $a $b$ c$"_s << false;
        QTest::newRow("prices in a link label") << u"Cena $10 – 15$"_s << true;
        QTest::newRow("dollar after a backslash") << u"C:\\$x$"_s << false;
        QTest::newRow("bare URL with a dollar") << u"Zobacz https://x.org/?p=$5 – kosztuje 10$"_s << false;
    }

    // escapeTextFormulas(): Markdown an importer wrote from plain text, checked block by block as
    // the editor reads it.
    void textFormulas_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("expected");
        QTest::newRow("no dollars") << u"Zażółć gęślą jaźń\n"_s << u"Zażółć gęślą jaźń\n"_s;
        QTest::newRow("paragraph lines") << u"Cena $10\nrazem 15$\n"_s << u"Cena \\$10\nrazem 15$\n"_s;
        QTest::newRow("separate blocks") << u"# Cena $10\n\n- razem 15$\n- $5\n\nkoniec 3$\n"_s
                                         << u"# Cena $10\n\n- razem 15$\n- $5\n\nkoniec 3$\n"_s;
        QTest::newRow("list item with lines")
                << u"- [ ] Bilet $10\n  parking 5$\n"_s << u"- [ ] Bilet \\$10\n  parking 5$\n"_s;
        QTest::newRow("code lines end a formula") << u"- Bilet $10\n  ```\n  $x$\n  ```\n  parking 5$\n"_s
                                                  << u"- Bilet $10\n  ```\n  $x$\n  ```\n  parking 5$\n"_s;
        // A property's value is shown with the text (its dollars pair with others) but stays as
        // written: the dollar of the text is escaped instead.
        QTest::newRow("property values stay") << u"- Spotkanie $10\n  location:: Sala $5\n  koszt 5$\n"_s
                                              << u"- Spotkanie $10\n  location:: Sala $5\n  koszt 5\\$\n"_s;
        QTest::newRow("callout title apart")
                << u"> [!note] Cena $10\n> razem 15$\n"_s << u"> [!note] Cena $10\n> razem 15$\n"_s;
        QTest::newRow("callout text") << u"> [!note] Ceny\n> Cena $10\n> razem 15$\n"_s
                                      << u"> [!note] Ceny\n> Cena \\$10\n> razem 15$\n"_s;
        QTest::newRow("blank quote line ends a formula")
                << u"> Cena $10\n>\n> razem 15$\n"_s << u"> Cena $10\n>\n> razem 15$\n"_s;
        QTest::newRow("quote lines") << u"> Cena $10\n> razem 15$\n"_s << u"> Cena \\$10\n> razem 15$\n"_s;
        // A line with only a bullet is no blank line: the editor shows the bullet.
        QTest::newRow("bullet line in a quote")
                << u"> Cena $10\n> -\n> razem 15$\n"_s << u"> Cena \\$10\n> -\n> razem 15$\n"_s;
        QTest::newRow("fenced and inline code")
                << u"`$x$` i $5\n\n```\n$y$\n```\n"_s << u"`$x$` i $5\n\n```\n$y$\n```\n"_s;
        QTest::newRow("frontmatter") << u"---\ntytuł: $x$\n---\nCena $x$\n"_s
                                     << u"---\ntytuł: $x$\n---\nCena \\$x$\n"_s;
        QTest::newRow("table cells") << u"| $15 | 10$ |\n| --- | --- |\n| $x$ | =$a$ |\n"_s
                                     << u"| $15 | 10$ |\n| --- | --- |\n| \\$x$ | =$a$ |\n"_s;
        // Rows split at every pipe, also inside code ("`a" and "b`" are two cells, neither is
        // code); a header cell is never a table formula.
        QTest::newRow("table code split by pipes")
                << u"| `a | $x$ | b` |\n| --- | --- | --- |\n"_s << u"| `a | \\$x$ | b` |\n| --- | --- | --- |\n"_s;
        QTest::newRow("table header with =")
                << u"| =$a$ |\n| --- |\n| =$b$ |\n"_s << u"| =\\$a$ |\n| --- |\n| =$b$ |\n"_s;
        QTest::newRow("wiki link and text") << u"Kurs $1 [[Plik$a]] i 15$\n"_s << u"Kurs \\$1 [[Plik$a]] i 15$\n"_s;
        QTest::newRow("link targets stay")
                << u"[[Plik $a|etykieta]] ((abc$)) <https://x.org/$1> https://x.org/b$ ![o](a$b.png)\n"_s
                << u"[[Plik $a|etykieta]] ((abc$)) <https://x.org/$1> https://x.org/b$ ![o](a$b.png)\n"_s;
        QTest::newRow("formula opened in a link")
                << u"[a](https://x.org/$a) b$\n"_s << u"[a](https://x.org/$a) b\\$\n"_s;
        QTest::newRow("display formula lines") << u"Tekst\n\n$$\n- a\n$$\n"_s << u"Tekst\n\n\\$\\$\n- a\n$$\n"_s;
        // The importer's own formulas (between the formula marks) stay; text dollars that would
        // pair with them are escaped.
        QTest::newRow("importer formulas") << u"Cena $5,\uE0F2$x$\uE0F3 i \uE0F2$y$\uE0F3 15$\n"_s
                                           << u"Cena \\$5,\uE0F2$x$\uE0F3 i \uE0F2$y$\uE0F3 15$\n"_s;
        QTest::newRow("escaped dollars") << u"\\$x$ i \\$5 15$\n"_s << u"\\$x$ i \\$5 15$\n"_s;
    }

    void textFormulas()
    {
        QFETCH(QString, markdown);
        QFETCH(QString, expected);
        const QString out = detail::escapeTextFormulas(markdown);
        QCOMPARE(out, expected);
        QCOMPARE(detail::escapeTextFormulas(out), out);
    }

    // A hostile chain of dollars pairing in turn ("$a $b $c … z$ y$ …") ends after a few rounds
    // with every dollar escaped that may change: no formula is left, in linear time per round.
    void textFormulasChain()
    {
        QString chain;
        for (int i = 0; i < 2000; ++i)
            chain += u"$a "_s;
        for (int i = 0; i < 2000; ++i)
            chain += u"b$ "_s;
        QElapsedTimer timer;
        timer.start();
        const QString out = detail::escapeTextFormulas(chain);
        QVERIFY2(timer.elapsed() < 4000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(markdown::findMathSpans(markdown::maskInlineCode(out)).isEmpty());
        QCOMPARE(QString(out).remove(u'\\'), chain);
        const QString escaped = escapeMarkdownText(chain, false);
        QVERIFY(markdown::findMathSpans(escaped).isEmpty());

        // Link syntax without its end ("<a", "[[", "((") is looked for once per line, not once
        // per opening, and a bare URL by the start of each word only: still linear (each took
        // seconds on such a line).
        const QString open = QString(u"<a[[(("_s).repeated(30000);
        timer.restart();
        const QString links = detail::escapeTextFormulas(u"$x$ "_s + open + u" 5$\n"_s);
        QVERIFY2(timer.elapsed() < 2000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
        QCOMPARE(links, u"\\$x$ "_s + open + u" 5$\n"_s);
    }

    void escapedTextShowsAsWritten()
    {
        QFETCH(QString, text);
        QFETCH(bool, linkLabel);
        const QString escaped = escapeMarkdownText(text, false, linkLabel);
        const QString md = linkLabel ? u"["_s + escaped + u"](https://example.org)"_s : escaped;
        const auto shown = [](const QString &html) {
            QTextDocument doc;
            doc.setHtml(html);
            return doc.toPlainText().replace(QChar::Nbsp, u' ');
        };
        QCOMPARE(shown(markdown::InlineRenderer().render(md)), text);
        QCOMPARE(shown(detail::HtmlRenderer().renderInline(md)), text);
    }
};

QTEST_MAIN(TestIoHtml)
#include "tst_io_html.moc"
