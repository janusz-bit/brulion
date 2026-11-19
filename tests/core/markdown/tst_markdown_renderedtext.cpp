// RenderedText (markdown/renderedtext.h): the renderer's HTML as text with style runs — the text Qt
// shows (entities, line breaks, collapsed white space), the runs' looks, the exact way back to the
// HTML, HTML that is not well formed, and the source map of renderRuns / renderBlockRuns.
#include "markdown/inlinerenderer.h"
#include "markdown/renderedtext.h"

#include "testtiming.h"

#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

RenderContext context()
{
    RenderContext c;
    c.today = QDate(2026, 10, 14);
    Palette p;
    p.link = u"L"_s;
    p.unresolved = u"U"_s;
    p.muted = u"M"_s;
    p.codeBackground = u"C"_s;
    p.highlightBackground = u"H"_s;
    p.tag = {u"TF"_s, u"TB"_s};
    p.due = {u"DF"_s, u"DB"_s};
    p.overdue = {u"OF"_s, u"OB"_s};
    p.reminder = {u"RF"_s, u"RB"_s};
    p.neutral = {u"NF"_s, u"NB"_s};
    p.done = {u"XF"_s, u"XB"_s};
    c.palette = p;
    c.pageIcon = [](const QString &target) {
        return target == u"Dom"_s ? PageIcon{PageIcon::Kind::App, u"home"_s} : PageIcon{};
    };
    return c;
}

QString shown(const QString &text)
{
    QString out = text;
    return out.replace(QChar(0xA0), u'~');
}

} // namespace

class TestRenderedText : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void text_data()
    {
        QTest::addColumn<QString>("html");
        QTest::addColumn<QString>("text"); // U+00A0 as ~
        QTest::newRow("plain") << u"Zażółć gęślą jaźń"_s << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("entities") << u"a &amp; b &lt;c&gt; &quot;d&quot;"_s << u"a & b <c> \"d\""_s;
        QTest::newRow("nbsp entity and literal") << u"w&nbsp;domu i dalej"_s << u"w~domu i~dalej"_s;
        QTest::newRow("line break") << u"a<br/>b"_s << u"a\nb"_s;
        QTest::newRow("spaces collapse") << u"a   b"_s << u"a b"_s;
        QTest::newRow("leading spaces dropped") << u"  a"_s << u"a"_s;
        QTest::newRow("trailing space kept") << u"a  "_s << u"a "_s;
        QTest::newRow("after a line break") << u"a<br/>  b"_s << u"a\nb"_s;
        QTest::newRow("across tags") << u"a <b> b</b>"_s << u"a b"_s;
        QTest::newRow("em space collapses") << u"a&#8195; b"_s << u"a b"_s;
        QTest::newRow("tab and newline") << u"a\tb\nc"_s << u"a b c"_s;
        QTest::newRow("emoji") << u"📅 jutro"_s << u"📅 jutro"_s;
        QTest::newRow("numeric entity") << u"&#8260;&#x1F4C5;"_s << u"⁄📅"_s;
        QTest::newRow("chip") << u"<span style=\"background-color:B;color:F\">&nbsp;⏰&nbsp;jutro&nbsp;</span>"_s
                              << u"~⏰~jutro~"_s;
    }
    void text()
    {
        QFETCH(QString, html);
        QFETCH(QString, text);
        const RenderedText t = renderedTextFromHtml(html);
        QCOMPARE(shown(t.text), text);
        QVERIFY(t.valid);
        QCOMPARE(runsToHtml(t), html);
    }

    void runs()
    {
        const InlineRenderer r(context());
        const RenderedText t = r.renderRuns(u"**gruby** i `kod` #tag [[Dom]] ==żółty=="_s);
        QVERIFY(t.valid);
        QVERIFY(t.drawable);
        QCOMPARE(shown(t.text), u"gruby i~kod ~#tag~ ~~~~Dom żółty"_s);
        const auto at = [&](QStringView piece) {
            const RenderedRun *run = t.runAt(t.text.indexOf(piece));
            return run ? *run : RenderedRun{};
        };
        QCOMPARE(at(u"gruby").style, quint16(RenderedRun::Bold));
        QCOMPARE(at(u"gruby").role, RenderedRole::Text);
        const RenderedRun code = at(u"kod");
        QCOMPARE(code.style, quint16(RenderedRun::Code));
        QCOMPARE(code.background, RenderedBackground::Box);
        QCOMPARE(code.backgroundColor, u"C"_s);
        QCOMPARE(code.role, RenderedRole::Code);
        const RenderedRun tag = at(u"#tag");
        QCOMPARE(tag.background, RenderedBackground::Pill);
        QCOMPARE(tag.foreground, u"TF"_s);
        QCOMPARE(tag.href, u"tag:tag"_s);
        QCOMPARE(tag.role, RenderedRole::Tag);
        // The tag's pill is one run with its padding.
        QCOMPARE(tag.from, t.text.indexOf(u"#tag") - 1);
        QCOMPARE(tag.to, t.text.indexOf(u"#tag") + 5);
        const RenderedRun link = at(u"Dom");
        QCOMPARE(link.href, u"page:Dom"_s);
        QCOMPARE(link.foreground, u"L"_s);
        QCOMPARE(link.role, RenderedRole::Link);
        QCOMPARE(link.background, RenderedBackground::None);
        const RenderedRun mark = at(u"żółty");
        QCOMPARE(mark.background, RenderedBackground::Box);
        QCOMPARE(mark.role, RenderedRole::Highlight);
        // The page's app icon: three no-break spaces in front of the link.
        QCOMPARE(t.objects.size(), 1);
        QCOMPARE(t.objects[0].kind, RenderedObject::Kind::Icon);
        QCOMPARE(t.objects[0].name, u"home"_s);
        QCOMPARE(t.objects[0].color, u"L"_s);
        QCOMPARE(t.objects[0].to - t.objects[0].from, 3);
        QCOMPARE(t.runAt(t.objects[0].from)->icon, u"home"_s);
        QCOMPARE(t.runAt(t.objects[0].from)->role, RenderedRole::Icon);
    }

    void nestingOrderKept()
    {
        // ***a***: <i><b>…</b></i>, and the order of empty elements.
        for (const QString &html : {u"<i><b>a</b></i>"_s, u"<b><i>a</i></b>"_s, u"<b></b><i></i>x"_s,
                                    u"<b><i></i></b>x"_s, u"a<b>b<i>c</i></b><s>d</s>"_s}) {
            const RenderedText t = renderedTextFromHtml(html);
            QVERIFY(t.valid);
            QCOMPARE(runsToHtml(t), html);
        }
        const RenderedText t = renderedTextFromHtml(u"<i><b>a</b></i>"_s);
        QCOMPARE(t.runs.size(), 1);
        QCOMPARE(t.runs[0].style, quint16(RenderedRun::Bold | RenderedRun::Italic));
    }

    void notWellFormed()
    {
        // A <mark> pair across emphasis: the renderer writes crossing tags.
        const InlineRenderer r(context());
        const QString html = r.render(u"<mark>a **b</mark> c**"_s);
        QCOMPARE(html, u"<span style=\"background-color:H\">a <b>b</span> c</b>"_s);
        const RenderedText t = r.renderRuns(u"<mark>a **b</mark> c**"_s);
        QVERIFY(!t.valid);
        QVERIFY(!t.drawable);
        QCOMPARE(t.text, u"a b c"_s);
        // Unknown tags and attributes, a bare '<'.
        QVERIFY(!renderedTextFromHtml(u"<img src=\"x\">"_s).valid);
        QVERIFY(!renderedTextFromHtml(u"<a onclick=\"x\">a</a>"_s).valid);
        QVERIFY(!renderedTextFromHtml(u"a < b"_s).valid);
        QVERIFY(!renderedTextFromHtml(u"<b>a"_s).valid);
        QVERIFY(!renderedTextFromHtml(u"a</b>"_s).valid);
    }

    void notDrawable()
    {
        const InlineRenderer r(context());
        QVERIFY(!r.renderBlockRuns(u"---"_s, BlockKind::Rule).drawable);
        QVERIFY(!r.renderBlockRuns(u"```\nkod\n```"_s, BlockKind::Code).drawable);
        // A formula with fonts of its own; a plain one is drawable.
        QVERIFY(!r.renderRuns(u"$\\mathtt{x}$"_s).drawable);
        QVERIFY(r.renderRuns(u"$x^2$"_s).drawable);
        // Exactly the HTML back, also for those.
        QCOMPARE(runsToHtml(r.renderBlockRuns(u"```\nkod <a>\n```"_s, BlockKind::Code)),
                 r.renderBlock(u"```\nkod <a>\n```"_s, BlockKind::Code));
    }

    void otherRenderings()
    {
        // The task chips, the hovered link, the find bar's highlights round-trip too.
        const InlineRenderer r(context());
        const QString chips = taskChipsHtml(2, 5, 3900, QDateTime(), context().palette);
        QCOMPARE(runsToHtml(renderedTextFromHtml(chips)), chips);
        const QString html = r.render(u"[[Dom]] i [[Inna|inna strona]] #a/b"_s);
        const QString hovered = InlineRenderer::withHoveredLink(html, u"page:Dom");
        const RenderedText h = renderedTextFromHtml(hovered);
        QVERIFY(h.valid);
        QCOMPARE(runsToHtml(h), hovered);
        QVERIFY(h.runAt(h.text.indexOf(u"Dom"))->style & RenderedRun::Underline);
        const QString found = InlineRenderer::withFindHighlight(html, u"strona", false, false, false, u"#ff0");
        const RenderedText f = renderedTextFromHtml(found);
        QCOMPARE(runsToHtml(f), found);
        QCOMPARE(f.runAt(f.text.indexOf(u"strona"))->backgroundColor, u"#ff0"_s);
        QCOMPARE(f.runAt(f.text.indexOf(u"strona"))->background, RenderedBackground::Pill); // in a link
    }

    void sourceMap()
    {
        const InlineRenderer r(context());
        const QString text = u"Ala **ma** [[Kot|kota]] & psa"_s;
        const RenderedText t = r.renderRuns(text);
        QCOMPARE(t.text, u"Ala ma kota & psa"_s);
        // Verbatim text character by character.
        QCOMPARE(t.sourceAt(0), 0);
        QCOMPARE(t.sourceAt(2), 2);
        QCOMPARE(t.sourceAt(t.text.indexOf(u"ma")), text.indexOf(u"ma"));
        QCOMPARE(t.sourceAt(t.text.indexOf(u"psa") + 1), text.indexOf(u"psa") + 1);
        QCOMPARE(t.sourceAt(t.text.indexOf(u"&")), text.indexOf(u"&"));
        // A link as a whole: its start.
        QCOMPARE(t.sourceAt(t.text.indexOf(u"kota") + 2), text.indexOf(u"[["));
        QCOMPARE(t.sourceAt(t.text.size()), text.size());
        QCOMPARE(t.displayAt(text.indexOf(u"psa")), t.text.indexOf(u"psa"));
        QCOMPARE(t.displayAt(text.indexOf(u"Kot")), t.text.indexOf(u"kota"));
        // The display range of a source range: the bold word, the whole link.
        QCOMPARE(t.displayRange(text.indexOf(u"ma"), text.indexOf(u"ma") + 2),
                 std::make_pair(t.text.indexOf(u"ma"), t.text.indexOf(u"ma") + 2));
        const qsizetype link = text.indexOf(u"[[");
        QCOMPARE(t.displayRange(link + 3, link + 4),
                 std::make_pair(t.text.indexOf(u"kota"), t.text.indexOf(u"kota") + 4));
    }

    void sourceMapOfBlocks_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("word"); // a word shown verbatim
        QTest::newRow("heading") << u"## Tytuł rozdziału"_s << int(BlockKind::Heading) << u"rozdziału"_s;
        QTest::newRow("setext") << u"Tytuł\nnowy\n==="_s << int(BlockKind::Heading) << u"nowy"_s;
        QTest::newRow("quote") << u"> pierwsza\n> druga linia"_s << int(BlockKind::Quote) << u"linia"_s;
        QTest::newRow("quote list") << u"> - punkt pierwszy"_s << int(BlockKind::Quote) << u"pierwszy"_s;
        QTest::newRow("callout") << u"> [!note] Uwaga\n> treść notatki"_s << int(BlockKind::Callout) << u"notatki"_s;
        QTest::newRow("callout title") << u"> [!note] Ważna uwaga\n> treść"_s << int(BlockKind::Callout) << u"uwaga"_s;
        QTest::newRow("task") << u"[ ] zadanie na jutro"_s << int(BlockKind::ListItem) << u"jutro"_s;
        QTest::newRow("crlf") << u"pierwsza\r\ndruga"_s << int(BlockKind::Paragraph) << u"druga"_s;
        QTest::newRow("html") << u"<div>\nx & y</div>"_s << int(BlockKind::Html) << u"y</div>"_s;
    }
    void sourceMapOfBlocks()
    {
        QFETCH(QString, body);
        QFETCH(int, kind);
        QFETCH(QString, word);
        const InlineRenderer r(context());
        const RenderedText t = r.renderBlockRuns(body, BlockKind(kind));
        QCOMPARE(runsToHtml(t), r.renderBlock(body, BlockKind(kind)));
        const qsizetype display = t.text.indexOf(word);
        QVERIFY2(display >= 0, qPrintable(t.text));
        for (qsizetype k = 0; k < word.size(); ++k)
            QCOMPARE(t.sourceAt(display + k), body.indexOf(word) + k);
        QCOMPARE(t.displayAt(body.indexOf(word)), display);
    }

    void hostileInputStaysLinear()
    {
        // Thousands of elements: reading and writing back stay fast.
        QString html;
        for (int k = 0; k < 3000; ++k)
            html += u"<b>a<i>b</i></b> "_s;
        brulion::test::CpuTimer timer;
        timer.start();
        const RenderedText t = renderedTextFromHtml(html);
        QCOMPARE(runsToHtml(t), html);
        QVERIFY(timer.elapsed() < 2000 * BRULION_TEST_SLOWDOWN);
    }
};

QTEST_GUILESS_MAIN(TestRenderedText)
#include "tst_markdown_renderedtext.moc"
