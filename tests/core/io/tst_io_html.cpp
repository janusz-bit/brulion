// HTML -> Markdown conversion (QTextDocument parsing, own Markdown writer) and Markdown escaping.
#include "iotest.h"

#include "io/htmlmarkdown.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

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
    }

    void escape()
    {
        QFETCH(QString, text);
        QFETCH(bool, lineStart);
        QFETCH(QString, expected);
        QCOMPARE(escapeMarkdownText(text, lineStart), expected);
    }
};

QTEST_MAIN(TestIoHtml)
#include "tst_io_html.moc"
