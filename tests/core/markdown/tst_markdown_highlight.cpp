// Coloured highlights (markdown/highlight.h, docs/SYNTAX.md §11): reading `<mark>` tags and their
// colours (Highlightr's styles and classes, other CSS mapped to the palette), the renderer's spans
// (only palette colours, never the note's CSS), and the editor's "Highlight" edit (setHighlight).
#include "markdown/highlight.h"
#include "markdown/inlinerenderer.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(brulion::markdown::HighlightColor)

namespace {

QString render(const QString &text)
{
    RenderContext c;
    c.palette.highlightBackground = u"H"_s;
    c.palette.link = u"L"_s;
    c.palette.codeBackground = u"C"_s;
    return InlineRenderer(c).render(text);
}

QString span(HighlightColor color, const QString &html)
{
    const QString tint = color == HighlightColor::Default ? u"H"_s : highlightTint(color);
    return u"<span style=\"background-color:"_s + tint + u"\">"_s + html + u"</span>"_s;
}

} // namespace

class TestHighlight : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void names()
    {
        for (int i = 0; i < HighlightColorCount; ++i) {
            const auto color = HighlightColor(i);
            QCOMPARE(parseHighlightColorName(highlightColorName(color)), color);
            QCOMPARE(parseHighlightColorName(highlightColorName(color).toUpper()), color);
            // What Brulion writes reads back as the same colour.
            if (color != HighlightColor::Default) {
                QCOMPARE(highlightColorFromStyle(QString(u"background: "_s + highlightWrittenColor(color) + u';')),
                         color);
                QVERIFY(highlightTint(color).startsWith(u'#'));
            }
        }
        QCOMPARE(parseHighlightColorName(u"gray"), HighlightColor::Grey);
        QCOMPARE(parseHighlightColorName(u"czerwony"), std::nullopt);
        QCOMPARE(parseHighlightColorName(u""), std::nullopt);
        QCOMPARE(highlightWrittenColor(HighlightColor::Default), QString());
    }

    void styles_data()
    {
        QTest::addColumn<QString>("style");
        QTest::addColumn<HighlightColor>("color");
        // Highlightr's defaults (inline-css), exactly and without their alpha.
        QTest::newRow("pink") << u"background: #FFB8EBA6;"_s << HighlightColor::Pink;
        QTest::newRow("red") << u"background: #FF5582A6;"_s << HighlightColor::Red;
        QTest::newRow("orange") << u"background: #FFB86CA6;"_s << HighlightColor::Orange;
        QTest::newRow("yellow") << u"background: #FFF3A3A6;"_s << HighlightColor::Yellow;
        QTest::newRow("green") << u"background: #BBFABBA6;"_s << HighlightColor::Green;
        QTest::newRow("cyan") << u"background: #ABF7F7A6;"_s << HighlightColor::Cyan;
        QTest::newRow("blue") << u"background: #ADCCFFA6;"_s << HighlightColor::Blue;
        QTest::newRow("purple") << u"background: #D2B3FFA6;"_s << HighlightColor::Purple;
        QTest::newRow("grey") << u"background: #CACFD9A6;"_s << HighlightColor::Grey;
        QTest::newRow("lower case, no alpha") << u"background:#ff5582"_s << HighlightColor::Red;
        // Other colours by hue.
        QTest::newRow("background-color hex") << u"background-color: #00ff00"_s << HighlightColor::Green;
        QTest::newRow("short hex") << u"background-color:#f00"_s << HighlightColor::Red;
        QTest::newRow("short hex alpha") << u"background-color:#00f8"_s << HighlightColor::Blue;
        QTest::newRow("rgb") << u"background-color: rgb(255, 165, 0)"_s << HighlightColor::Orange;
        QTest::newRow("rgba") << u"background: rgba(128, 0, 255, 0.4)"_s << HighlightColor::Purple;
        QTest::newRow("rgb percent") << u"background: rgb(0% 100% 100%)"_s << HighlightColor::Cyan;
        QTest::newRow("named") << u"BACKGROUND-COLOR: Yellow"_s << HighlightColor::Yellow;
        QTest::newRow("named pink") << u"background: hotpink"_s << HighlightColor::Pink;
        QTest::newRow("grey hex") << u"background: #888888"_s << HighlightColor::Grey;
        QTest::newRow("black") << u"background: #000"_s << HighlightColor::Grey;
        QTest::newRow("gradient") << u"background: linear-gradient(to right, #FF5582A6 0%, transparent)"_s
                                  << HighlightColor::Red;
        QTest::newRow("last wins") << u"background: red; background-color: blue"_s << HighlightColor::Blue;
        QTest::newRow("other properties") << u"color: red; font-weight: bold"_s << HighlightColor::Default;
        // Malformed / hostile.
        QTest::newRow("empty") << QString() << HighlightColor::Default;
        QTest::newRow("no value") << u"background:"_s << HighlightColor::Default;
        QTest::newRow("bad hex length") << u"background: #12345"_s << HighlightColor::Default;
        QTest::newRow("url") << u"background: url(https://tracker.example/x.png)"_s << HighlightColor::Default;
        QTest::newRow("expression") << u"background: expression(alert(1))"_s << HighlightColor::Default;
        QTest::newRow("unclosed rgb") << u"background: rgb(1, 2"_s << HighlightColor::Default;
        QTest::newRow("unknown name") << u"background: zażółć"_s << HighlightColor::Default;
    }

    void styles()
    {
        QFETCH(QString, style);
        QFETCH(HighlightColor, color);
        QCOMPARE(highlightColorFromStyle(style), color);
    }

    void classes()
    {
        QCOMPARE(highlightColorFromClass(u"hltr-red"), HighlightColor::Red);
        QCOMPARE(highlightColorFromClass(u"inny hltr-Purple"), HighlightColor::Purple);
        QCOMPARE(highlightColorFromClass(u"hltr-gray"), HighlightColor::Grey);
        QCOMPARE(highlightColorFromClass(u"hltr-"), HighlightColor::Default);
        QCOMPARE(highlightColorFromClass(u"red"), HighlightColor::Default);
        QCOMPARE(highlightColorFromClass(u"hltr-czerwony"), HighlightColor::Default);
    }

    void tags_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("to"); // -1: no tag
        QTest::addColumn<bool>("closing");
        QTest::addColumn<HighlightColor>("color");
        QTest::newRow("bare") << u"<mark>x"_s << 6 << false << HighlightColor::Default;
        QTest::newRow("upper") << u"<MARK>x"_s << 6 << false << HighlightColor::Default;
        QTest::newRow("closing") << u"</mark>"_s << 7 << true << HighlightColor::Default;
        QTest::newRow("closing blank") << u"</mark  >"_s << 9 << true << HighlightColor::Default;
        QTest::newRow("highlightr") << u"<mark style=\"background: #FF5582A6;\">"_s << 37 << false
                                    << HighlightColor::Red;
        QTest::newRow("single quotes") << u"<mark style='background:#ADCCFF'>"_s << 33 << false << HighlightColor::Blue;
        QTest::newRow("unquoted class") << u"<mark class=hltr-green>"_s << 23 << false << HighlightColor::Green;
        QTest::newRow("style before class")
                << u"<mark class=\"hltr-red\" style=\"background:#ABF7F7\">"_s << 50 << false << HighlightColor::Cyan;
        QTest::newRow("bare attribute") << u"<mark hidden>"_s << 13 << false << HighlightColor::Default;
        QTest::newRow("blanks") << u"<mark  style = \"background:red\" >"_s << 33 << false << HighlightColor::Red;
        // Not mark tags.
        QTest::newRow("other tag") << u"<marker>"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("no blank") << u"<mark\"x\">"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("self closing") << u"<mark/>"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("unclosed") << u"<mark style=\"x"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("unclosed tag") << u"<mark"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("line break") << u"<mark\nstyle=\"background:red\">"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("quote line break") << u"<mark style=\"a\nb\">"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("closing with attribute") << u"</mark class=x>"_s << -1 << false << HighlightColor::Default;
        QTest::newRow("too long") << u"<mark title=\""_s + QString(600, u'a') + u"\">"_s << -1 << false
                                  << HighlightColor::Default;
        QTest::newRow("no less than") << u"mark>"_s << -1 << false << HighlightColor::Default;
    }

    void tags()
    {
        QFETCH(QString, text);
        QFETCH(int, to);
        QFETCH(bool, closing);
        QFETCH(HighlightColor, color);
        const auto tag = markTagAt(text, 0);
        if (to < 0) {
            QVERIFY(!tag);
            return;
        }
        QVERIFY(tag);
        QCOMPARE(tag->to, to);
        QCOMPARE(tag->closing, closing);
        QCOMPARE(tag->color, color);
    }

    void pairs()
    {
        const QString text = u"<mark>a <mark style=\"background:red\">b</mark> c</mark> </mark> <mark>x"_s;
        const QList<MarkPair> pairs = findMarkPairs(text);
        QCOMPARE(pairs.size(), 2);
        QCOMPARE(pairs[0].open.from, 0);
        QCOMPARE(pairs[0].close.from, text.indexOf(u"</mark> </mark>"));
        QCOMPARE(pairs[1].open.color, HighlightColor::Red);
        QCOMPARE(text.sliced(pairs[1].open.to, pairs[1].close.from - pairs[1].open.to), u"b"_s);
    }

    void render_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("equals") << u"==ważne=="_s << span(HighlightColor::Default, u"ważne"_s);
        QTest::newRow("bare mark") << u"<mark>ważne</mark>"_s << span(HighlightColor::Default, u"ważne"_s);
        QTest::newRow("highlightr red") << u"Zażółć <mark style=\"background: #FF5582A6;\">gęślą</mark> jaźń"_s
                                        << u"Zażółć "_s + span(HighlightColor::Red, u"gęślą"_s) + u" jaźń"_s;
        QTest::newRow("class") << u"<mark class=\"hltr-blue\">niebieski</mark>"_s
                               << span(HighlightColor::Blue, u"niebieski"_s);
        QTest::newRow("markdown inside") << u"<mark style=\"background:#BBFABB\">**pogrubione** i [[Dom]]</mark>"_s
                                         << span(HighlightColor::Green,
                                                 u"<b>pogrubione</b> i\u00a0<a href=\"page:Dom\" "
                                                 u"style=\"color:L;text-decoration:none\">Dom</a>"_s);
        QTest::newRow("nested") << u"<mark>a <mark style=\"background:red\">b</mark> c</mark>"_s
                                << span(HighlightColor::Default, u"a "_s + span(HighlightColor::Red, u"b"_s) + u" c"_s);
        QTest::newRow("equals inside mark") << u"<mark class=hltr-pink>==a==</mark>"_s
                                            << span(HighlightColor::Pink, span(HighlightColor::Default, u"a"_s));
        // Unpaired or not a highlight: text.
        QTest::newRow("unclosed") << u"<mark>otwarte"_s << u"&lt;mark&gt;otwarte"_s;
        QTest::newRow("lone closing") << u"tekst</mark>"_s << u"tekst&lt;/mark&gt;"_s;
        QTest::newRow("in code") << u"`<mark>x</mark>`"_s
                                 << u"<code style=\"background-color:C\">&lt;mark&gt;x&lt;/mark&gt;</code>"_s;
        QTest::newRow("in comment") << u"a %%<mark>x</mark>%% b"_s << u"a  b"_s;
        QTest::newRow("partner inside a link") << u"<mark>a [b</mark>](https://e.pl)"_s
                                               << u"&lt;mark&gt;a <a href=\"ext:https://e.pl\" "
                                                  u"style=\"color:L;text-decoration:none\">b&lt;/mark&gt;</a>"_s;
        QTest::newRow("marker tag") << u"<marker>x</marker>"_s << u"&lt;marker&gt;x&lt;/marker&gt;"_s;
        // Hostile CSS and HTML: never passed on, only the palette.
        QTest::newRow("css injection") << u"<mark style=\"background:red;position:fixed;top:0\">x</mark>"_s
                                       << span(HighlightColor::Red, u"x"_s);
        QTest::newRow("quote breakout") << u"<mark style='background:red\" onmouseover=\"alert(1)'>x</mark>"_s
                                        << span(HighlightColor::Red, u"x"_s);
        QTest::newRow("url background") << u"<mark style=\"background:url(https://t.example/p.gif)\">x</mark>"_s
                                        << span(HighlightColor::Default, u"x"_s);
        QTest::newRow("script inside") << u"<mark><script>alert(1)</script></mark>"_s
                                       << span(HighlightColor::Default, u"&lt;script&gt;alert(1)&lt;/script&gt;"_s);
        QTest::newRow("event handler") << u"<mark onclick=\"alert(1)\">x</mark>"_s
                                       << span(HighlightColor::Default, u"x"_s);
    }

    void render()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(::render(input), expected);
    }

    void renderedColoursArePaletteOnly()
    {
        // Whatever a note writes, the only colours in the output are the palette's.
        const QString html = ::render(u"<mark style=\"background: #123456; color: #fff\">a</mark>"_s
                                      u"<mark style=\"background: rgb(1,2,3)\">b</mark>"_s);
        QVERIFY(!html.contains(u"#123456"));
        QVERIFY(!html.contains(u"#fff"));
        QVERIFY(!html.contains(u"rgb"));
        QVERIFY(html.contains(highlightTint(HighlightColor::Blue)));
    }

    void manyTagsStayLinear()
    {
        QString text;
        for (int i = 0; i < 20000; ++i)
            text += u"<mark>a "_s;
        for (int i = 0; i < 20000; ++i)
            text += u"</mark "_s;
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(findMarkPairs(text).size(), 0); // "</mark " is no tag
        (void)::render(text);
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    }

    void findSpans()
    {
        const QString text = u"a ==b== `==c==` <mark class=hltr-red>d</mark> ==e"_s;
        const QList<HighlightSpan> spans = findHighlights(text);
        QCOMPARE(spans.size(), 2);
        QCOMPARE(spans[0].from, 2);
        QCOMPARE(spans[0].to, 7);
        QVERIFY(!spans[0].html);
        QVERIFY(spans[1].html);
        QCOMPARE(spans[1].color, HighlightColor::Red);
        QCOMPARE(text.sliced(spans[1].contentFrom, spans[1].contentTo - spans[1].contentFrom), u"d"_s);
    }

    void setHighlight_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<QString>("color"); // "" = remove
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("selected"); // the selection afterwards

        const QString red = u"<mark style=\"background: #FF5582A6;\">"_s;
        const QString blue = u"<mark style=\"background: #ADCCFFA6;\">"_s;
        QTest::newRow("wrap default") << u"Zażółć gęślą jaźń"_s << 7 << 12 << u"default"_s << u"Zażółć ==gęślą== jaźń"_s
                                      << u"gęślą"_s;
        QTest::newRow("wrap red") << u"Zażółć gęślą jaźń"_s << 7 << 12 << u"red"_s
                                  << u"Zażółć "_s + red + u"gęślą</mark> jaźń"_s << u"gęślą"_s;
        QTest::newRow("blanks trimmed") << u"a  bcd  e"_s << 1 << 8 << u"default"_s << u"a  ==bcd==  e"_s << u"bcd"_s;
        QTest::newRow("reversed selection") << u"abc def"_s << 7 << 4 << u"default"_s << u"abc ==def=="_s << u"def"_s;
        QTest::newRow("equals cannot pair")
                << u"a(b)c"_s << 1 << 4 << u"default"_s << u"a<mark>(b)</mark>c"_s << u"(b)"_s;
        QTest::newRow("recolour equals") << u"x ==ważne== y"_s << 4 << 9 << u"blue"_s
                                         << u"x "_s + blue + u"ważne</mark> y"_s << u"ważne"_s;
        QTest::newRow("recolour part") << u"x ==ważne== y"_s << 5 << 6 << u"red"_s
                                       << u"x "_s + red + u"ważne</mark> y"_s << u"ważne"_s;
        QTest::newRow("recolour whole with markers")
                << u"x ==ważne== y"_s << 2 << 11 << u"red"_s << u"x "_s + red + u"ważne</mark> y"_s << u"ważne"_s;
        QTest::newRow("mark to default") << u"x "_s + red + u"ważne</mark> y"_s << 40 << 42 << u"default"_s
                                         << u"x ==ważne== y"_s << u"ważne"_s;
        QTest::newRow("same colour") << u"x ==a== y"_s << 4 << 5 << u"default"_s << u"x ==a== y"_s << u"a"_s;
        QTest::newRow("remove equals") << u"x ==ważne== y"_s << 5 << 5 << QString() << u"x ważne y"_s << u"ważne"_s;
        QTest::newRow("remove mark") << u"x "_s + red + u"ważne</mark> y"_s << 41 << 41 << QString() << u"x ważne y"_s
                                     << u"ważne"_s;
        QTest::newRow("remove nothing") << u"x y"_s << 0 << 3 << QString() << u"x y"_s << u"x y"_s;
        QTest::newRow("inner unwrapped") << u"a ==b== c ==d== e"_s << 0 << 17 << u"red"_s << red + u"a b c d e</mark>"_s
                                         << u"a b c d e"_s;
        QTest::newRow("caret inserts") << u"ab"_s << 1 << 1 << u"green"_s
                                       << u"a<mark style=\"background: #BBFABBA6;\"></mark>b"_s << QString();
        QTest::newRow("cuts a highlight") << u"ab ==cd== ef"_s << 1 << 6 << u"red"_s << u"ab ==cd== ef"_s << u"b ==c"_s;
        QTest::newRow("several lines") << u"ab\ncd"_s << 1 << 4 << u"red"_s << u"ab\ncd"_s << u"b\nc"_s;
        QTest::newRow("caret in a marker")
                << u"x <mark>a</mark>"_s << 4 << 4 << u"red"_s << u"x <mark>a</mark>"_s << QString();
        QTest::newRow("code untouched") << u"`==a==` b"_s << 8 << 9 << u"default"_s << u"`==a==` ==b=="_s << u"b"_s;
    }

    void setHighlight()
    {
        QFETCH(QString, text);
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(QString, color);
        QFETCH(QString, expected);
        QFETCH(QString, selected);
        const std::optional<HighlightColor> c =
                color.isEmpty() ? std::nullopt : std::optional(*parseHighlightColorName(color));
        const HighlightEdit edit = markdown::setHighlight(text, from, to, c);
        QCOMPARE(edit.text, expected);
        QCOMPARE(edit.changed, text != expected);
        const QString row = QString::fromLatin1(QTest::currentDataTag());
        QCOMPARE(edit.refused, row == u"cuts a highlight" || row == u"several lines" || row == u"caret in a marker");
        QCOMPARE(edit.text.sliced(edit.from, edit.to - edit.from), selected);
    }

    void wrappedTextRendersHighlighted()
    {
        for (int i = 1; i < HighlightColorCount; ++i) {
            const auto color = HighlightColor(i);
            const HighlightEdit edit = markdown::setHighlight(u"jeden dwa trzy"_s, 6, 9, color);
            QCOMPARE(::render(edit.text), u"jeden "_s + span(color, u"dwa"_s) + u" trzy"_s);
        }
    }
};

QTEST_GUILESS_MAIN(TestHighlight)
#include "tst_markdown_highlight.moc"
