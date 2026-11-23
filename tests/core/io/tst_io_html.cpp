// HTML -> Markdown conversion (QTextDocument parsing, own Markdown writer) and Markdown escaping.
#include "iotest.h"

#include "io/clipboardhtml.h"
#include "io/htmlmarkdown.h"
#include "io/htmlmath_p.h"
#include "io/htmlrender_p.h"
#include "io/htmlutil_p.h"
#include "markdown/inline.h"
#include "markdown/inlinerenderer.h"
#include "markdown/math.h"

#include "testtiming.h"

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

/// KaTeX's output of a formula: the MathML with the TeX source, then its HTML copy.
QString katex(const QString &tex, const QString &mathml, bool display = false)
{
    const QString span = u"<span class=\"katex\"><span class=\"katex-mathml\"><math xmlns=\"http://www.w3.org/1998/Math/MathML\""_s
                         + (display ? u" display=\"block\""_s : QString()) + u"><semantics><mrow>"_s + mathml
                         + u"</mrow><annotation encoding=\"application/x-tex\">"_s + tex
                         + u"</annotation></semantics></math></span><span class=\"katex-html\" aria-hidden=\"true\">"
                           "<span class=\"base\"><span class=\"mord mathnormal\">ZDUBLOWANE</span></span></span></span>"_s;
    return display ? u"<span class=\"katex-display\">"_s + span + u"</span>"_s : span;
}

HtmlMarkdown withMath(const QString &html)
{
    HtmlToMarkdownOptions o;
    o.mathFormulas = true;
    return htmlToMarkdown(html, o);
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
                                       u"<span style=\"font-family:Menlo, monospace\">kod</span> "
                                       u"<span style=\"background-color: rgb(255, 250, 165)\">żółte</span></p>"_s
                                    << u"**mocno** `kod` ==żółte==\n"_s;
        QTest::newRow("spaces outside markers") << u"<p><b> spaced </b>x</p>"_s << u"**spaced** x\n"_s;
        QTest::newRow("code with backtick") << u"<p><code>a`b</code></p>"_s << u"`` a`b ``\n"_s;
        QTest::newRow("links") << u"<p><a href=\"https://example.com/a b\">Przykład</a> i "
                                  u"<a href=\"https://x.org\">https://x.org</a></p>"_s
                               << u"[Przykład](https://example.com/a%20b) i <https://x.org>\n"_s;
        QTest::newRow("mailto") << u"<p><a href=\"mailto:jan@x.pl\">jan@x.pl</a></p>"_s << u"<mailto:jan@x.pl>\n"_s;
        QTest::newRow("unsafe links keep text") << u"<p><a href=\"javascript:alert(1)\">kliknij</a> "
                                                   u"<a href=\"#sekcja\">kotwica</a></p>"_s
                                                << u"kliknij kotwica\n"_s;
        QTest::newRow("bold inside link")
                << u"<p><a href=\"https://x.org\"><b>B</b> t</a></p>"_s << u"[**B** t](https://x.org)\n"_s;
        // Brackets in a link label or an image alt would end the label (or make a wiki link).
        QTest::newRow("brackets in labels")
                << u"<p><a href=\"https://x.org\">a]b</a> <a href=\"https://x.org/(x)\">[x]</a> "
                   u"<img src=\"https://x.org/i.png\" alt=\"[[alt]]\"></p>"_s
                << u"[a\\]b](https://x.org) [\\[x\\]](https://x.org/%28x%29) ![\\[\\[alt\\]\\]](https://x.org/i.png)\n"_s;
        // Attributes are read in order: a name inside another attribute's quoted value is text.
        QTest::newRow("attribute inside a value") << u"<p><img alt=\"x src=evil.png\" src=\"https://x.org/good.png\"> "
                                                     u"<input value='type=checkbox checked' type=text>t</p>"_s
                                                  << u"![x src=evil.png](https://x.org/good.png) t\n"_s;
        QTest::newRow("quoted > in attributes")
                << u"<p><img alt=\"a > b\" src=\"https://x.org/c.png\"><input title='x>y' type=checkbox checked> z</p>"_s
                << u"![a > b](https://x.org/c.png)[x] z\n"_s;
        QTest::newRow("task-like text in list") << u"<ul><li>[ ] nie zadanie</li><li>[x] też nie</li></ul>"_s
                                                << u"- \\[ ] nie zadanie\n- \\[x] też nie\n"_s;
        QTest::newRow("images") << u"<p><img src=\"https://x.org/a.png\" alt=\"Obraz\"> "
                                   u"<img src=\"data:image/png;base64,AAAA\"> <img src=\"local dir/b.png\"></p>"_s
                                << u"![Obraz](https://x.org/a.png) ![](local%20dir/b.png)\n"_s;
        QTest::newRow("table") << u"<table><tr><th>Nazwa</th><th>Cena</th></tr>"
                                  u"<tr><td>a|b</td><td>5 <b>zł</b></td></tr><tr><td></td><td>x<br>y</td></tr></table>"_s
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
                   u"<p style=\"margin-left:40px\">wcięty, nie cytat</p>"_s
                << u"> A\n>\n>> B\n>\n> - c\n\nwcięty, nie cytat\n"_s;
        QTest::newRow("rule") << u"<p>a</p><hr><p>b</p>"_s << u"a\n\n---\n\nb\n"_s;
        QTest::newRow("headings") << u"<h1>Tytuł</h1><h2>Sekcja <b>x</b></h2><h6>mały</h6>"_s
                                  << u"# Tytuł\n\n## Sekcja x\n\n###### mały\n"_s;
        QTest::newRow("escaping") << u"<p>* nie lista</p><p># nie nagłówek</p><p>1. nie numer</p>"
                                     u"<p>a *b* [[x]] `c` ==d== %%e%% ((f)) &lt;tag&gt; ^id</p>"_s
                                  << u"\\* nie lista\n\n\\# nie nagłówek\n\n1\\. nie numer\n\n"
                                     u"a \\*b\\* \\[\\[x\\]\\] \\`c\\` \\==d\\== \\%%e\\%% \\((f)) \\<tag> \\^id\n"_s;
        QTest::newRow("entities and nbsp") << u"<p>a&nbsp;b &amp; c &lt; d</p>"_s << u"a b & c < d\n"_s;
        QTest::newRow("scripts and styles dropped")
                << u"<html><head><style>p{color:red}</style><script>alert('x')</script></head>"
                   u"<body><p>Treść</p><script>evil()</script><noscript>ns</noscript></body></html>"_s
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
        // Articles of the web (the Web Clipper, HTML imports): HTML5's sections are blocks, a
        // figure's caption a paragraph of its own, lazily loaded pictures show what the page loads,
        // code blocks name their language in the ways sites write it, the line break before
        // </code></pre> ends the last line; players are dropped unless embedsAsLinks.
        QTest::newRow("html5 sections") << u"<article><header>Nagłówek</header><section>Treść</section>"
                                           u"<footer>Stopka</footer></article><nav>Menu</nav>"_s
                                        << u"Nagłówek\nTreść\nStopka\nMenu\n"_s;
        QTest::newRow("figure and caption")
                << u"<figure><img src=\"https://x.org/a.png\" alt=\"Wykres\"><figcaption>Rys. 1</figcaption></figure>"_s
                << u"![Wykres](https://x.org/a.png)\n\n*Rys. 1*\n"_s;
        QTest::newRow("summary") << u"<details><summary>Więcej</summary><p>Ukryte</p></details>"_s
                                 << u"**Więcej**\n\nUkryte\n"_s;
        QTest::newRow("lazy picture")
                << u"<img src=\"data:image/gif;base64,R0lGOD\" data-src=\"https://x.org/żółw.jpg\" alt=\"Żółw\">"_s
                << u"![Żółw](https://x.org/żółw.jpg)\n"_s;
        QTest::newRow("srcset picture") << u"<img srcset=\"https://x.org/a.jpg 1x, https://x.org/b.jpg 2x\">"_s
                                        << u"![](https://x.org/b.jpg)\n"_s;
        QTest::newRow("src wins over srcset")
                << u"<img src=\"https://x.org/a.jpg\" srcset=\"https://x.org/b.jpg 2x\">"_s
                << u"![](https://x.org/a.jpg)\n"_s;
        QTest::newRow("github code") << u"<div class=\"highlight highlight-source-js\"><pre>let a = 1;\n</pre></div>"_s
                                     << u"```js\nlet a = 1;\n```\n"_s;
        QTest::newRow("language on pre") << u"<pre class=\"language-rust\"><code>fn x() {}</code></pre>"_s
                                         << u"```rust\nfn x() {}\n```\n"_s;
        QTest::newRow("data-lang") << u"<pre data-lang=\"sql\">SELECT 1;</pre>"_s << u"```sql\nSELECT 1;\n```\n"_s;
        QTest::newRow("human lang is no code language") << u"<pre lang=\"pl\">tekst</pre>"_s << u"```\ntekst\n```\n"_s;
        QTest::newRow("line break before the end tag")
                << u"<pre><code>a\n\nb\n</code></pre>"_s << u"```\na\n\nb\n```\n"_s;
        QTest::newRow("player dropped")
                << u"<p>a</p><iframe src=\"https://www.youtube.com/embed/AbCdEf12345\"></iframe>"_s << u"a\n"_s;
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

    // ---- math formulas of web pages (HtmlToMarkdownOptions::mathFormulas, docs/CLIPPER.md)
    void mathFormulas_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("markdown");
        const QString fraction = u"<mi>x</mi><mo>=</mo><mfrac><mrow><mo>−</mo><mi>b</mi></mrow><mrow><mn>2</mn><mi>a</mi></mrow></mfrac>"_s;
        // KaTeX: the annotation's TeX; the rendered copy is dropped.
        QTest::newRow("katex inline")
                << u"<p>Niech "_s + katex(u"x = \\frac{-b}{2a}"_s, fraction) + u" będzie pierwiastkiem równania.</p>"_s
                << u"Niech $x = \\frac{-b}{2a}$ będzie pierwiastkiem równania.\n"_s;
        QTest::newRow("katex display")
                << u"<p>Wzór Einsteina:</p><p>"_s + katex(u"E = mc^{2}"_s, u"<mi>E</mi>"_s, true) + u"</p><p>wiąże masę z energią.</p>"_s
                << u"Wzór Einsteina:\n\n$$E = mc^{2}$$\n\nwiąże masę z energią.\n"_s;
        QTest::newRow("katex display in a paragraph")
                << u"<p>Wynik: "_s + katex(u"\\sum_{i=1}^{n} i"_s, u"<mi>i</mi>"_s, true) + u" — tyle.</p>"_s
                << u"Wynik:\n\n$$\\sum_{i=1}^{n} i$$\n\n— tyle.\n"_s;
        QTest::newRow("katex escaped TeX")
                << u"<p>"_s + katex(u"a &lt; b \\land c &amp;&amp; d"_s, u"<mi>a</mi>"_s) + u"</p>"_s
                << u"$a < b \\land c && d$\n"_s;
        QTest::newRow("katex in a list and a heading")
                << u"<h2>Pole "_s + katex(u"\\pi r^2"_s, u"<mi>π</mi>"_s) + u"</h2><ul><li>obwód "_s
                           + katex(u"2\\pi r"_s, u"<mn>2</mn>"_s) + u"</li></ul>"_s
                << u"## Pole $\\pi r^2$\n\n- obwód $2\\pi r$\n"_s;
        QTest::newRow("katex bold and a link around")
                << u"<p><b>"_s + katex(u"x"_s, u"<mi>x</mi>"_s) + u"</b> i <a href=\"https://matma.example/\">"_s
                           + katex(u"y"_s, u"<mi>y</mi>"_s) + u"</a></p>"_s
                << u"**$x$** i [$y$](https://matma.example/)\n"_s;
        // MathJax 3: the assistive MathML (no TeX in it), converted; display="true" is a block.
        QTest::newRow("mathjax 3 inline")
                << u"<p>Kwadrat <mjx-container class=\"MathJax\" jax=\"CHTML\"><mjx-math class=\"MJX-TEX\"><mjx-msup><mjx-mi class=\"mjx-i\"><mjx-c class=\"mjx-c1D465\"></mjx-c></mjx-mi></mjx-msup></mjx-math>"
                   "<mjx-assistive-mml unselectable=\"on\" display=\"inline\"><math xmlns=\"http://www.w3.org/1998/Math/MathML\"><msup><mi>x</mi><mn>2</mn></msup></math></mjx-assistive-mml></mjx-container> rośnie.</p>"_s
                << u"Kwadrat $x^2$ rośnie.\n"_s;
        QTest::newRow("mathjax 3 display")
                << u"<mjx-container class=\"MathJax\" jax=\"SVG\" display=\"true\"><svg width=\"10\"><title>x</title><g><text>ZDUBLOWANE</text></g></svg>"
                   "<mjx-assistive-mml unselectable=\"on\" display=\"block\"><math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\"><munderover><mo>∑</mo><mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow><mi>n</mi></munderover><msub><mi>a</mi><mi>i</mi></msub></math></mjx-assistive-mml></mjx-container>"_s
                << u"$$\\sum_{i=1}^{n}a_i$$\n"_s;
        QTest::newRow("mathjax 3 data attribute")
                << u"<p>Dla <mjx-container data-tex=\"\\beta^2\"><mjx-math>ZDUBLOWANE</mjx-math></mjx-container> mamy</p>"_s
                << u"Dla $\\beta^2$ mamy\n"_s;
        QTest::newRow("mathjax 3 without a source stays as text")
                << u"<p>Dla <mjx-container><mjx-math>β2</mjx-math></mjx-container> mamy</p>"_s << u"Dla β2 mamy\n"_s;
        // MathJax 2: the script's TeX; the rendered spans are dropped, the display one a block.
        QTest::newRow("mathjax 2 inline")
                << u"<p>Suma <span class=\"MathJax_Preview\" style=\"color: inherit;\"></span><span class=\"MathJax\" id=\"MathJax-Element-1-Frame\" tabindex=\"0\"><nobr><span>ZDUBLOWANE</span></nobr>"
                   "<span class=\"MJX_Assistive_MathML\"><math><mi>α</mi></math></span></span><script type=\"math/tex\" id=\"MathJax-Element-1\">\\alpha + \\beta</script> jest.</p>"_s
                << u"Suma $\\alpha + \\beta$ jest.\n"_s;
        QTest::newRow("mathjax 2 display")
                << u"<p>Całka:</p><div class=\"MathJax_Display\"><span class=\"MathJax\" id=\"f\">ZDUBLOWANE</span></div>"
                   "<script type=\"math/tex; mode=display\" id=\"MathJax-Element-2\">\\int_0^1 x\\,dx = \\tfrac{1}{2}</script>"_s
                << u"Całka:\n\n$$\\int_0^1 x\\,dx = \\tfrac{1}{2}$$\n"_s;
        QTest::newRow("mathjax 2 mathml script")
                << u"<span class=\"MathJax\">ZDUBLOWANE</span><script type=\"math/mml\"><math><mfrac><mn>1</mn><mn>3</mn></mfrac></math></script>"_s
                << u"$\\frac{1}{3}$\n"_s;
        QTest::newRow("mathjax 2 script escapes nothing")
                << u"<p><script type=\"math/tex\">a < b &amp; c</script></p>"_s << u"$a < b &amp; c$\n"_s;
        QTest::newRow("a script of another type is dropped")
                << u"<p>x<script type=\"text/javascript\">var math = 1;</script>y</p>"_s << u"xy\n"_s;
        // The extension's copy of a page's math scripts (Readability removes scripts).
        QTest::newRow("math source element after MathJax 2's rendering")
                << u"<p>Suma <span class=\"MathJax_Preview\"></span><span class=\"MathJax\" id=\"f\"><span>ZDUBLOWANE</span></span>"
                   "<brulion-math-source type=\"math/tex\" id=\"f2\">\\alpha</brulion-math-source> jest.</p>"_s
                << u"Suma $\\alpha$ jest.\n"_s;
        QTest::newRow("math source element")
                << u"<p>Wzór <brulion-math-source type=\"math/tex\">a &lt; b</brulion-math-source> i "
                   "<brulion-math-source type=\"math/tex; mode=display\">c^2</brulion-math-source></p>"_s
                << u"Wzór $a < b$ i\n\n$$c^2$$\n"_s;
        // Wikipedia: the MathML with its annotation, or only the fallback picture's alt.
        QTest::newRow("wikipedia mathml")
                << u"<p>Pole koła wynosi <span class=\"mwe-math-element mwe-math-element-inline\"><span class=\"mwe-math-mathml-inline mwe-math-mathml-a11y\" style=\"display: none;\">"
                   "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" alttext=\"{\\displaystyle \\pi r^{2}}\"><semantics><mrow class=\"MJX-TeXAtom-ORD\"><mstyle displaystyle=\"true\" scriptlevel=\"0\"><mi>π</mi><msup><mi>r</mi><mrow><mn>2</mn></mrow></msup></mstyle></mrow>"
                   "<annotation encoding=\"application/x-tex\">{\\displaystyle \\pi r^{2}}</annotation></semantics></math></span>"
                   "<img src=\"https://wikimedia.example/svg/abc\" class=\"mwe-math-fallback-image-inline\" aria-hidden=\"true\" alt=\"{\\displaystyle \\pi r^{2}}\"></span>.</p>"_s
                << u"Pole koła wynosi $\\pi r^{2}$.\n"_s;
        QTest::newRow("wikipedia fallback picture only")
                << u"<p>Twierdzenie <span class=\"mwe-math-element\"><img src=\"https://wikimedia.example/svg/d\" class=\"mwe-math-fallback-image-inline\" alt=\"{\\displaystyle a^{2}+b^{2}=c^{2}}\"></span>.</p>"_s
                << u"Twierdzenie $a^{2}+b^{2}=c^{2}$.\n"_s;
        QTest::newRow("wikipedia block")
                << u"<dl><dd><span class=\"mwe-math-element mwe-math-element-block\"><img src=\"x.svg\" class=\"mwe-math-fallback-image-display\" alt=\"{\\displaystyle \\lambda =2}\"></span></dd></dl>"_s
                << u"$$\\lambda =2$$\n"_s;
        QTest::newRow("math with an alttext only")
                << u"<p>x <math alttext=\"{\\displaystyle \\lambda }\"></math> y</p>"_s << u"x $\\lambda$ y\n"_s;
        // Plain MathML (no TeX anywhere): converted.
        QTest::newRow("mathml quadratic")
                << u"<p>Pierwiastki: <math display=\"block\"><mi>x</mi><mo>=</mo><mfrac><mrow><mo>−</mo><mi>b</mi><mo>±</mo><msqrt><msup><mi>b</mi><mn>2</mn></msup><mo>−</mo><mn>4</mn><mi>a</mi><mi>c</mi></msqrt></mrow><mrow><mn>2</mn><mi>a</mi></mrow></mfrac></math></p>"_s
                << u"Pierwiastki:\n\n$$x=\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}$$\n"_s;
        QTest::newRow("mathml pi and Polish text")
                << u"<p>Dla <math><mi>n</mi><mo>∈</mo><mi>ℕ</mi><mtext> oraz </mtext><mi>x</mi><mo>≥</mo><mn>3,14</mn></math> działa.</p>"_s
                << u"Dla $n\\in\\mathbb{N}\\text{ oraz }x\\ge3{,}14$ działa.\n"_s;
        // MathML with namespace prefixes and self-closing elements.
        QTest::newRow("mathml with prefixes")
                << u"<p><m:math xmlns:m=\"http://www.w3.org/1998/Math/MathML\"><m:mi>a</m:mi><m:mo>+</m:mo><m:mi>b</m:mi></m:math></p>"_s
                << u"$a+b$\n"_s;
        // Pandoc / Sphinx.
        QTest::newRow("pandoc inline and display")
                << u"<p>Znamy <span class=\"math inline\">\\(e^{i\\pi} + 1 = 0\\)</span>.</p><p><span class=\"math display\">\\[\\int_0^\\infty e^{-x}dx = 1\\]</span></p>"_s
                << u"Znamy $e^{i\\pi} + 1 = 0$.\n\n$$\\int_0^\\infty e^{-x}dx = 1$$\n"_s;
        QTest::newRow("a math class without delimiters is no formula")
                << u"<div class=\"math\">Matematyka dla dzieci</div>"_s << u"Matematyka dla dzieci\n"_s;
        // The text around: touching letters get a space, dollars of the text never join a formula.
        QTest::newRow("a letter touches a formula")
                << u"<p>wektor"_s + katex(u"v"_s, u"<mi>v</mi>"_s) + u", "_s + katex(u"x"_s, u"<mi>x</mi>"_s) + u"ów i "_s
                           + katex(u"a"_s, u"<mi>a</mi>"_s) + katex(u"b"_s, u"<mi>b</mi>"_s) + u"</p>"_s
                << u"wektor $v$, $x$ ów i $a$ $b$\n"_s;
        QTest::newRow("prices beside a formula")
                << u"<p>Cena $10 i "_s + katex(u"x"_s, u"<mi>x</mi>"_s) + u", razem 15$</p>"_s
                << u"Cena \\$10 i $x$, razem 15$\n"_s;
        QTest::newRow("a stray dollar before a formula")
                << u"<p>zapłać 5$"_s + katex(u"x"_s, u"<mi>x</mi>"_s) + u"</p>"_s << u"zapłać 5$ $x$\n"_s;
        // What the editor would not read as one formula is a code span.
        QTest::newRow("tex with a backtick")
                << u"<p>"_s + katex(u"a`b"_s, u"<mi>a</mi>"_s) + u"</p>"_s << u"`` a`b ``\n"_s;
        QTest::newRow("tex ending in a backslash")
                << u"<p>"_s + katex(u"a\\"_s, u"<mi>a</mi>"_s) + u"</p>"_s << u"`a\\`\n"_s;
        // A table cell: no bars.
        QTest::newRow("formula in a table cell")
                << u"<table><tr><th>wartość</th><th>wzór</th></tr><tr><td>moduł</td><td>"_s
                           + katex(u"|x| + \\|v\\|"_s, u"<mi>x</mi>"_s) + u"</td></tr></table>"_s
                << u"| wartość | wzór |\n| --- | --- |\n| moduł | $\\vert x\\vert + \\Vert v\\Vert$ |\n"_s;
        // Code is code.
        QTest::newRow("math in code stays")
                << u"<pre><code>"_s + katex(u"x"_s, u"<mi>x</mi>"_s) + u"</code></pre>"_s
                << QString();
        QTest::newRow("dollars in a code block stay")
                << u"<article><pre><code>cena = $5 + $x$</code></pre></article>"_s
                << u"```\ncena = $5 + $x$\n```\n"_s;
        // Hostile input: our own marks and slots in a page, unclosed containers, a formula source
        // that is not TeX.
        QTest::newRow("slots in the page")
                << u"<p>a\uE0F40\uE0F5b\uE0F2c\uE0F3d</p>"_s << u"a0bcd\n"_s;
        QTest::newRow("an unclosed container still converts the math in it")
                << u"<p>x<span class=\"katex\"><span class=\"katex-mathml\"><math><annotation encoding=\"application/x-tex\">y</annotation></math></p><p>z</p>"_s
                << u"x $y$\n\nz\n"_s;
        QTest::newRow("an annotation of another encoding")
                << u"<p><math><semantics><mi>x</mi><annotation encoding=\"application/xhtml+xml\">&lt;b&gt;</annotation></semantics></math></p>"_s
                << u"$x$\n"_s;
        QTest::newRow("math in a comment") << u"<p>a<!-- <math><mi>x</mi></math> -->b</p>"_s << u"ab\n"_s;
    }
    void mathFormulas()
    {
        QFETCH(QString, html);
        QFETCH(QString, markdown);
        const QString out = withMath(html).markdown;
        if (html.startsWith(u"<pre><code>"_s)) {
            // The formula of a code block is its text: no slot, no mark, no "$".
            QVERIFY2(!out.contains(QChar(detail::MathSlotOpen)) && !out.contains(QChar(detail::FormulaOpen)),
                     qPrintable(out));
            QVERIFY(out.startsWith(u"```\n"_s));
            return;
        }
        QCOMPARE(out, markdown);
        QVERIFY(!out.contains(u"ZDUBLOWANE"_s));
    }

    void mathFormulasAreOffByDefault()
    {
        const QString html = katex(u"x^2"_s, u"<mi>x</mi>"_s);
        QVERIFY(!htmlToMarkdown(u"<p>"_s + html + u"</p>"_s).markdown.contains(u"$x^2$"_s));
        QCOMPARE(htmlToMarkdown(u"<p>a<math><mi>x</mi></math>b</p>"_s).markdown, u"ab\n"_s);
    }

    void mathmlToTex_data()
    {
        QTest::addColumn<QString>("mathml");
        QTest::addColumn<QString>("tex");
        const auto row = [](const char *name, const QString &body, const QString &tex) {
            QTest::newRow(name) << u"<math>"_s + body + u"</math>"_s << tex;
        };
        row("identifiers and numbers", u"<mi>x</mi><mo>+</mo><mn>12</mn>"_s, u"x+12"_s);
        row("a fraction", u"<mfrac><mn>1</mn><mn>2</mn></mfrac>"_s, u"\\frac{1}{2}"_s);
        row("a binomial", u"<mfrac linethickness=\"0\"><mi>n</mi><mi>k</mi></mfrac>"_s, u"\\binom{n}{k}"_s);
        row("a root with an index", u"<mroot><mi>x</mi><mn>3</mn></mroot>"_s, u"\\sqrt[3]{x}"_s);
        row("a power of a sum", u"<msup><mrow><mo>(</mo><mi>a</mi><mo>+</mo><mi>b</mi><mo>)</mo></mrow><mn>2</mn></msup>"_s,
            u"(a+b)^2"_s);
        row("a power of a sum without brackets", u"<msup><mrow><mi>a</mi><mo>+</mo><mi>b</mi></mrow><mn>2</mn></msup>"_s,
            u"{a+b}^2"_s);
        row("a power of a number", u"<msup><mn>12</mn><mn>2</mn></msup>"_s, u"{12}^2"_s);
        row("an index and an exponent", u"<msubsup><mi>x</mi><mi>i</mi><mn>10</mn></msubsup>"_s, u"x_i^{10}"_s);
        row("a script of a script", u"<msup><msub><mi>x</mi><mi>i</mi></msub><mn>2</mn></msup>"_s, u"{x_i}^2"_s);
        row("a sum", u"<munderover><mo>∑</mo><mrow><mi>k</mi><mo>=</mo><mn>0</mn></mrow><mi>n</mi></munderover><msup><mi>q</mi><mi>k</mi></msup>"_s,
            u"\\sum_{k=0}^{n}q^k"_s);
        row("an integral", u"<msubsup><mo>∫</mo><mn>0</mn><mn>1</mn></msubsup><mi>f</mi><mo>(</mo><mi>x</mi><mo>)</mo><mi>d</mi><mi>x</mi>"_s,
            u"\\int_{0}^{1}f(x)dx"_s);
        row("a limit", u"<munder><mi>lim</mi><mrow><mi>x</mi><mo>→</mo><mn>0</mn></mrow></munder><mfrac><mrow><mi>sin</mi><mo>⁡</mo><mi>x</mi></mrow><mi>x</mi></mfrac>"_s,
            u"\\lim_{x\\to0}\\frac{\\sin x}{x}"_s);
        row("accents", u"<mover><mi>x</mi><mo>¯</mo></mover><mover><mi>v</mi><mo>→</mo></mover><mover accent=\"true\"><mi>a</mi><mo>^</mo></mover>"_s,
            u"\\overline{x}\\vec{v}\\hat{a}"_s);
        row("an underline and an overset", u"<munder><mi>x</mi><mo>_</mo></munder><mover><mo>=</mo><mi>def</mi></mover>"_s,
            u"\\underline{x}\\overset{\\mathrm{def}}{=}"_s);
        row("Greek letters", u"<mi>α</mi><mi>β</mi><mi>Ω</mi><mi>φ</mi><mi>ϕ</mi><mi>ε</mi>"_s,
            u"\\alpha\\beta\\Omega\\varphi\\phi\\varepsilon"_s);
        row("a word as an identifier", u"<mi>abc</mi><mi mathvariant=\"normal\">d</mi><mi mathvariant=\"bold\">v</mi>"_s,
            u"\\mathrm{abc}\\mathrm{d}\\mathbf{v}"_s);
        row("mathematical alphanumerics", u"<mi>𝑥</mi><mi>𝔸</mi><mi>ℝ</mi>"_s, u"x\\mathbb{A}\\mathbb{R}"_s);
        row("text with Polish letters and specials", u"<mtext>dla każdego_x &amp; 50%</mtext>"_s,
            u"\\text{dla każdego\\_x \\& 50\\%}"_s);
        row("spaces", u"<mi>a</mi><mspace width=\"1em\"></mspace><mi>b</mi><mtext> </mtext><mi>c</mi>"_s,
            u"a\\quad b\\ c"_s);
        row("a decimal comma", u"<mn>0,5</mn><mo>+</mo><mn>1.5</mn>"_s, u"0{,}5+1.5"_s);
        row("a matrix", u"<mrow><mo>(</mo><mtable><mtr><mtd><mn>1</mn></mtd><mtd><mn>0</mn></mtd></mtr><mtr><mtd><mn>0</mn></mtd><mtd><mn>1</mn></mtd></mtr></mtable><mo>)</mo></mrow>"_s,
            u"\\begin{pmatrix} 1 & 0 \\\\ 0 & 1 \\end{pmatrix}"_s);
        row("cases", u"<mrow><mo>{</mo><mtable><mtr><mtd><mi>x</mi></mtd><mtd><mtext>dla </mtext><mi>x</mi><mo>&gt;</mo><mn>0</mn></mtd></mtr><mtr><mtd><mn>0</mn></mtd><mtd><mtext>w p.p.</mtext></mtd></mtr></mtable></mrow>"_s,
            u"\\begin{cases} x & \\text{dla }x>0 \\\\ 0 & \\text{w p.p.} \\end{cases}"_s);
        row("fences", u"<mfenced open=\"[\" close=\"]\"><mi>a</mi><mi>b</mi></mfenced>"_s, u"[a,b]"_s);
        row("a braced set", u"<mo>{</mo><mn>1</mn><mo>,</mo><mn>2</mn><mo>}</mo>"_s, u"\\{1,2\\}"_s);
        row("semantics without TeX", u"<semantics><mrow><mi>y</mi></mrow><annotation encoding=\"MathML-Content\">ignored</annotation></semantics>"_s, u"y"_s);
        row("an old square root", u"<mo>√</mo><mn>2</mn>"_s, u"\\sqrt{2}"_s);
        row("a control word before a letter", u"<mi>π</mi><mi>r</mi><mo>≤</mo><mi>x</mi>"_s, u"\\pi r\\le x"_s);
        row("an unknown element's text", u"<mglyph>ż</mglyph>"_s, u"\\text{ż}"_s);
        row("empty", u"<mrow></mrow>"_s, QString());
        QTest::newRow("no math element") << u"<p>x</p>"_s << QString();
    }
    void mathmlToTex()
    {
        QFETCH(QString, mathml);
        QFETCH(QString, tex);
        QCOMPARE(detail::mathmlToTex(mathml), tex);
    }

    void mathFormulasAreBounded()
    {
        // Deep MathML, a page of thousands of formulas and of unclosed containers: quick, bounded.
        brulion::test::CpuTimer timer;
        timer.start();
        const QString deep = u"<p><math>"_s + u"<mrow>"_s.repeated(5000) + u"<mi>x</mi>"_s + u"</mrow>"_s.repeated(5000)
                             + u"</math></p>"_s;
        QVERIFY(withMath(deep).markdown.startsWith(u"$"_s));
        QString many = u"<p>"_s;
        for (int i = 0; i < 20000; ++i)
            many += u"<span class=\"katex\"><math><mi>x</mi><annotation encoding=\"application/x-tex\">x_"_s
                    + QString::number(i) + u"</annotation></math></span> "_s;
        many += u"</p>"_s;
        const QString out = withMath(many).markdown;
        QVERIFY(out.contains(u"$x_9999$"_s));
        QVERIFY(!out.contains(QChar(detail::MathSlotOpen)));
        const QString unclosed = u"<span class=\"katex\"><math><mi>x</mi>"_s.repeated(50000);
        QVERIFY(!withMath(unclosed).markdown.contains(QChar(detail::MathSlotOpen)));
        QVERIFY2(timer.elapsed() < 20000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(timer.elapsed())));
    }

    // The clipboard's HTML of copied blocks (io/clipboardhtml.h): semantic, escaped, no links into
    // the notebook, pictures as links (never data: URLs), block references as their text.
    void clipboardHtmlOfBlocks()
    {
        io::ClipboardHtmlOptions options;
        options.attachmentUrl = [](const QString &target) {
            return target == u"zdjęcie.png"  ? u"file:///notatki/zdjęcie.png"_s
                   : target == u"inline.png" ? u"data:image/png;base64,AAAA"_s
                                             : QString();
        };
        options.blockText = [](const QString &id) { return id == u"abc123" ? u"Treść **bloku**"_s : QString(); };
        const QString html = io::clipboardHtml(
                u"# Zakupy\n\n- [ ] Kupić żółć i\u00a0źdźbło #dom\n\t- [x] mleko\n- [[Strona]] i ((abc123)) "
                u"((brak))\n\n"
                u"![[zdjęcie.png]] ![[inline.png]] ![x](https://example.com/a.png)\n\n<script>alert(1)</script>\n"_s,
                options);
        QVERIFY(html.startsWith(u"<meta charset=\"utf-8\">"_s + QString(io::ClipboardHtmlMarker)));
        QVERIFY2(html.contains(u"<h1"_s) && html.contains(u"Zakupy"_s), qPrintable(html));
        QVERIFY2(html.contains(u"<ul"_s) && html.contains(u"type=\"checkbox\""_s), qPrintable(html));
        QVERIFY2(html.contains(u"Kupić żółć i\u00a0źdźbło"_s) || html.contains(u"Kupić żółć i&nbsp;źdźbło"_s),
                 qPrintable(html));
        QVERIFY2(!html.contains(u"href=\"page"_s) && html.contains(u"Strona"_s), qPrintable(html));
        QVERIFY2(html.contains(u"Treść <strong>bloku</strong>"_s) && html.contains(u"((brak))"_s), qPrintable(html));
        QVERIFY2(html.contains(u"href=\"file:///notatki/zdjęcie.png\""_s) && !html.contains(u"<img"_s),
                 qPrintable(html));
        QVERIFY2(!html.contains(u"data:"_s) && html.contains(u"inline.png"_s), qPrintable(html));
        QVERIFY2(html.contains(u"href=\"https://example.com/a.png\""_s), qPrintable(html));
        QVERIFY2(!html.contains(u"<script"_s) && html.contains(u"&lt;script&gt;"_s), qPrintable(html));
        QCOMPARE(io::clipboardHtml(u"  \n"_s), QString());
    }

    // A copied ```mermaid block is its source in the HTML too (mail and office apps seldom show
    // inline SVG); the export draws it (tst_io_export htmlMermaid).
    void clipboardHtmlOfDiagram()
    {
        const QString html = io::clipboardHtml(u"```mermaid\ngraph TD\n  A --> B\n```\n"_s);
        QVERIFY2(html.contains(u"<pre><code class=\"language-mermaid\">graph TD\n  A --&gt; B</code></pre>"_s),
                 qPrintable(html));
        QVERIFY(!html.contains(u"<svg"_s));
    }

    // Dates, recurrence and priority stay in the HTML of blocks that are no tasks (a mail or an
    // office app pastes it: the Markdown flavour has them).
    void clipboardHtmlKeepsTokens_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("a list item's due date")
                << u"- Punkt 📅 2026-10-20"_s << QStringList{u"Punkt"_s, u"📅 2026-10-20"_s};
        QTest::newRow("scheduled and recurrence")
                << u"- Punkt ⏳ 2026-10-21 🔁 every week"_s << QStringList{u"⏳ 2026-10-21"_s, u"🔁 every week"_s};
        QTest::newRow("a paragraph") << u"Zadanie 📅 2026-10-20 🛫 2026-10-18"_s
                                     << QStringList{u"<p>Zadanie"_s, u"📅 2026-10-20"_s, u"🛫 2026-10-18"_s};
        QTest::newRow("a priority") << u"Akapit ⏫ ważny"_s << QStringList{u"Akapit"_s, u"ważny"_s, u"⏫"_s};
        QTest::newRow("a heading") << u"## Termin 📅 2026-11-01"_s << QStringList{u"Termin"_s, u"📅 2026-11-01"_s};
        QTest::newRow("a reminder's recurrence once")
                << u"- Leki ⏰ 2026-10-20 08:00 🔁 every day"_s << QStringList{u"⏰ 2026-10-20 08:00 🔁 every day"_s};
        QTest::newRow("a task as before")
                << u"- [ ] Zrobić ⏫ 📅 2026-10-20"_s << QStringList{u"Zrobić"_s, u"📅 2026-10-20"_s, u"⏫"_s};
        QTest::newRow("code is not a date")
                << u"- `📅 2026-10-20` w kodzie"_s << QStringList{u"<code>📅 2026-10-20</code>"_s};
    }
    void clipboardHtmlKeepsTokens()
    {
        QFETCH(QString, markdown);
        QFETCH(QStringList, expected);
        const QString html = io::clipboardHtml(markdown);
        for (const QString &part : expected)
            QVERIFY2(html.contains(part), qPrintable(part + u" not in "_s + html));
        QCOMPARE(html.count(u"📅"_s), markdown.count(u"📅"_s));
        QCOMPARE(html.count(u"🔁"_s), markdown.count(u"🔁"_s));
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
                                              u"<img src='data:image/png;base64,iVBO' alt=\"Wykres &amp; dane\"></p>"
                                              u"<p><a href=\"inna.html\">Inna</a> <a href=\"https://x.org\">x</a></p>"_s,
                                              o);
        QCOMPARE(seen.size(), 3);
        QCOMPARE(seen[0].src, u"/etc/passwd"_s);
        QCOMPARE(seen[1].src, u"file:///dev/zero"_s);
        QCOMPARE(seen[1].alt, u"zero"_s);
        QCOMPARE(seen[2].alt, u"Wykres & dane"_s);
        QCOMPARE(r.markdown, u"![[assets/x.png]]\n\n[[Inna]] [x](https://x.org)\n"_s);
    }

    void embedsAsLinks()
    {
        HtmlToMarkdownOptions o;
        o.embedsAsLinks = true;
        const QString html =
                u"<iframe src=\"https://www.youtube.com/embed/AbCdEf12345?rel=0\" title=\"Żółw je\"></iframe>"
                u"<iframe src=\"//player.vimeo.com/video/123456\"></iframe>"
                u"<video controls><source src=\"https://x.org/film.mp4\"></video>"
                u"<audio src=\"https://x.org/podkast.mp3\" title=\"Odcinek 1\"></audio>"
                u"<iframe src=\"javascript:alert(1)\"><p>ukryte</p></iframe><p>koniec</p>"_s;
        QCOMPARE(htmlToMarkdown(html, o).markdown,
                 u"[Żółw je](https://www.youtube.com/watch?v=AbCdEf12345)\n\n<https://vimeo.com/123456>\n\n"
                 u"[film.mp4](https://x.org/film.mp4)\n\n[Odcinek 1](https://x.org/podkast.mp3)\n\nkoniec\n"_s);
        QCOMPARE(htmlToMarkdown(html).markdown, u"koniec\n"_s);
        // A code block's language is no text of it.
        QCOMPARE(htmlToPlainText(u"<pre><code class=\"language-js\">let a;</code></pre>"_s).trimmed(), u"let a;"_s);
    }

    void neverLoadsResources()
    {
        // A stylesheet link, an import and background images must not be read (no hang on
        // /dev/zero, no file access).
        const HtmlMarkdown r = htmlToMarkdown(
                u"<link rel=\"stylesheet\" href=\"file:///dev/zero\"><style>@import url(/dev/zero);</style>"
                u"<table background=\"/dev/zero\"><tr><td style=\"background-image:url(/dev/zero)\">x</td></tr></table>"_s);
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
        brulion::test::CpuTimer timer;
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
                u"</ol><ol start=3 class=\"x\" style=\"-qt-list-number-prefix:'#:0'; margin-top:0px\"><li>c<ol><li>d</ol>e</ol>"_s);
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
        brulion::test::CpuTimer timer;
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
        // What the editor does not link is text: "www." and "ftp://" addresses, "http" right
        // after a letter, what follows a URL's closing quote, a raw HTML tag.
        QTest::newRow("no links but text") << u"www.x.org/$a$ i ftp://x/$b$ i zobaczhttps://x.org/$c$\n"_s
                                           << u"www.x.org/\\$a$ i ftp://x/\\$b$ i zobaczhttps://x.org/\\$c$\n"_s;
        QTest::newRow("URL ends at a quote") << u"\"https://x.org/a\"$b$ i <https://x.org/$c$>\n"_s
                                             << u"\"https://x.org/a\"\\$b$ i <https://x.org/$c$>\n"_s;
        QTest::newRow("raw tag is text") << u"Tekst <span title=\"$x$\">a</span>\n"_s
                                         << u"Tekst <span title=\"\\$x$\">a</span>\n"_s;
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

    // Random web text with dollars across styles, lines, paragraphs, list items, table cells,
    // code, links and URLs: the imported note shows every dollar of the page, in the editor and
    // in the export — no formula took any (SYNTAX §11).
    void randomTextShowsItsDollars()
    {
        // The pieces, separated by "|".
        static const QStringList pieces = QStringLiteral(
                "$|$|$|$$|a|x|ż|5|10| | | |,|.|-|(|)|\\|*|_|#|\"|<b>|</b>|<i>|</i>|<br>|</p><p>|<code>$x$ a</code>|"
                "<a href=\"https://x.org/\">|</a>| https://x.org/?p=| www.x.org/")
                        .split(u'|');
        QRandomGenerator rng(20261022);
        int formulas = 0; // pages whose note would show a formula without its escapes
        for (int n = 0; n < 300; ++n) {
            QString body;
            const int length = 3 + int(rng.bounded(25));
            for (int k = 0; k < length; ++k)
                body += pieces.at(rng.bounded(int(pieces.size())));
            const int shape = int(rng.bounded(3));
            QString html;
            if (shape == 0)
                html = u"<p>"_s + body + u"</p>"_s;
            else if (shape == 1)
                html = u"<ul><li>"_s + body + u"</li><li>"_s + body + u"</li></ul>"_s;
            else
                html = u"<table><tr><td>"_s + body + u"</td><td>"_s + body + u"</td></tr></table>"_s;
            QTextDocument page;
            page.setHtml(html);
            const qsizetype dollars = page.toPlainText().count(u'$');
            const QString note = htmlToMarkdown(html).markdown;
            for (const bool exported : {false, true}) {
                const QString shown = iotest::shownText(note, exported);
                QVERIFY2(shown.count(u'$') == dollars, qPrintable(u"%1\n%2\n%3"_s.arg(html, note, shown)));
            }
            if (iotest::shownText(QString(note).replace(u"\\$"_s, u"$"_s)).count(u'$') < dollars)
                ++formulas;
        }
        QVERIFY2(formulas >= 10, qPrintable(QString::number(formulas)));
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
        brulion::test::CpuTimer timer;
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
