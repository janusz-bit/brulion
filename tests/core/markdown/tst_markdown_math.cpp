// markdown/math.h: where a note's text holds a formula (findMathSpans: `$…$`, `$$…$$`, `\(…\)`,
// `\[…\]` — what only looks like one is not) and how a formula's LaTeX becomes Qt rich text
// (renderMath: Unicode symbols, <sup>/<sub>, fractions, radicals, matrices, \text; everything
// else escaped). Exact strings: the tests are the specification of the dialect (docs/SYNTAX.md §11).
#include "markdown/math.h"

#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

/// The spans of `text` as "from..to contentFrom..contentTo inline|display" items, joined.
QString spansOf(const QString &text)
{
    QStringList out;
    for (const MathSpan &s : findMathSpans(text))
        out.append(u"%1..%2 %3..%4 %5"_s.arg(s.from)
                           .arg(s.to)
                           .arg(s.contentFrom)
                           .arg(s.contentTo)
                           .arg(s.display ? u"display"_s : u"inline"_s));
    return out.join(u"; "_s);
}

QString math(const QString &latex)
{
    return renderMath(latex, false);
}

QString displayMath(const QString &latex)
{
    return renderMath(latex, true);
}

} // namespace

class TestMath : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void spans_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("inline dollars") << u"$x$"_s << u"0..3 1..2 inline"_s;
        QTest::newRow("inline parens") << u"\\(x\\)"_s << u"0..5 2..3 inline"_s;
        QTest::newRow("display dollars") << u"$$x$$"_s << u"0..5 2..3 display"_s;
        QTest::newRow("display brackets") << u"\\[x\\]"_s << u"0..5 2..3 display"_s;
        QTest::newRow("in a sentence") << u"Pole koła: $\\pi r^2$, prawda?"_s << u"11..20 12..19 inline"_s;
        QTest::newRow("two formulas") << u"$x$ i $y$"_s << u"0..3 1..2 inline; 6..9 7..8 inline"_s;
        QTest::newRow("display lines") << u"$$\na = b\n$$"_s << u"0..11 2..9 display"_s;
        QTest::newRow("after an escape") << u"\\$5 i $x$"_s << u"6..9 7..8 inline"_s;

        // Not formulas: nothing to render, the text stays as written.
        QTest::newRow("prices") << u"kosztuje $5 i $10"_s << QString();
        QTest::newRow("space after opening") << u"$ x $"_s << QString();
        QTest::newRow("digit after closing") << u"$x$5"_s << QString();
        QTest::newRow("unclosed") << u"cena $x"_s << QString();
        QTest::newRow("escaped") << u"\\$100"_s << QString();
        QTest::newRow("empty") << u"$$"_s << QString();
        QTest::newRow("empty content") << u"$ $"_s << QString();
        QTest::newRow("no delimiters") << u"zwykły tekst 2 * 3"_s << QString();
        QTest::newRow("backslash alone") << u"C:\\Users"_s << QString();
        // A code span is the caller's to mask (InlineRenderer masks before asking).
        QTest::newRow("code span not skipped here") << u"`$x$`"_s << u"1..4 2..3 inline"_s;
    }

    void spans()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(spansOf(input), expected);
    }

    void renderSymbols_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("variables italic") << u"x + y"_s << u"<i>x</i> + <i>y</i>"_s;
        QTest::newRow("letter run") << u"mc^2"_s << u"<i>mc</i><sup>2</sup>"_s;
        QTest::newRow("equation") << u"E = mc^2"_s << u"<i>E</i> = <i>mc</i><sup>2</sup>"_s;
        QTest::newRow("numbers") << u"2,5 * 10^{3}"_s << u"2,5 ∗ 10<sup>3</sup>"_s;
        QTest::newRow("minus") << u"a - b"_s << u"<i>a</i> − <i>b</i>"_s; // U+2212
        QTest::newRow("unary minus") << u"-x"_s << u"−<i>x</i>"_s;
        QTest::newRow("subscript and superscript") << u"x_i^2"_s << u"<i>x</i><sub><i>i</i></sub><sup>2</sup>"_s;
        QTest::newRow("braced scripts") << u"x^{n+1}_{k}"_s << u"<i>x</i><sub><i>k</i></sub><sup><i>n</i> + 1</sup>"_s;
        QTest::newRow("command script") << u"x^\\alpha"_s << u"<i>x</i><sup><i>α</i></sup>"_s;
        QTest::newRow("prime") << u"f'"_s << u"<i>f</i><sup>′</sup>"_s;
        QTest::newRow("greek") << u"\\alpha + \\beta = \\gamma"_s << u"<i>α</i> + <i>β</i> = <i>γ</i>"_s;
        QTest::newRow("uppercase greek upright") << u"\\Delta \\Omega"_s << u"ΔΩ"_s; // as in LaTeX: no space
        QTest::newRow("comparisons") << u"a \\leq b \\neq c \\approx d"_s
                                     << u"<i>a</i> ≤ <i>b</i> ≠ <i>c</i> ≈ <i>d</i>"_s;
        QTest::newRow("arrows") << u"A \\to B \\implies C"_s << u"<i>A</i> → <i>B</i> ⟹ <i>C</i>"_s;
        QTest::newRow("sets") << u"x \\in A \\cup B, A \\subset B"_s
                              << u"<i>x</i> ∈ <i>A</i> ∪ <i>B</i>, <i>A</i> ⊂ <i>B</i>"_s;
        QTest::newRow("sum") << u"\\sum_{i=1}^{n} i"_s << u"∑<sub><i>i</i> = 1</sub><sup><i>n</i></sup> <i>i</i>"_s;
        QTest::newRow("integral") << u"\\int_0^1 x \\, dx"_s << u"∫<sub>0</sub><sup>1</sup> <i>x</i>&#8201;<i>dx</i>"_s;
        QTest::newRow("infinity") << u"\\lim_{x \\to \\infty} \\frac{1}{x} = 0"_s
                                  << u"lim<sub><i>x</i> → ∞</sub> <sup>1</sup>&#8260;<sub><i>x</i></sub> = 0"_s;
        QTest::newRow("functions") << u"\\sin x + \\log_2 y"_s << u"sin <i>x</i> + log<sub>2</sub> <i>y</i>"_s;
        QTest::newRow("delimiters") << u"\\langle x \\rangle"_s << u"⟨<i>x</i>⟩"_s;
        QTest::newRow("left right") << u"\\left( \\frac{a}{b} \\right)"_s
                                    << u"(<sup><i>a</i></sup>&#8260;<sub><i>b</i></sub>)"_s;
        QTest::newRow("spaces") << u"a \\quad b"_s << u"<i>a</i>&#8195;<i>b</i>"_s;
    }

    void renderSymbols()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(math(input), expected);
    }

    void renderFractions_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("vulgar half") << u"\\frac{1}{2}"_s << u"½"_s;
        QTest::newRow("vulgar in text") << u"\\frac{1}{2} \\pi r^2"_s << u"½<i>π</i><i>r</i><sup>2</sup>"_s;
        QTest::newRow("letters") << u"\\frac{a}{b}"_s << u"<sup><i>a</i></sup>&#8260;<sub><i>b</i></sub>"_s;
        QTest::newRow("derivative") << u"\\frac{d}{dx}"_s << u"<sup><i>d</i></sup>&#8260;<sub><i>dx</i></sub>"_s;
        QTest::newRow("commands") << u"\\frac{\\alpha}{\\beta}"_s << u"<sup><i>α</i></sup>&#8260;<sub><i>β</i></sub>"_s;
        QTest::newRow("long numerator") << u"\\frac{x + 1}{y}"_s << u"(<i>x</i> + 1)&#8260;<i>y</i>"_s;
        QTest::newRow("nested fraction") << u"\\frac{1}{x^2}"_s << u"1&#8260;(<i>x</i><sup>2</sup>)"_s;
        QTest::newRow("dfrac") << u"\\dfrac{3}{4}"_s << u"¾"_s;
        QTest::newRow("binom") << u"\\binom{n}{k}"_s << u"(<sup><i>n</i></sup>&#8260;<sub><i>k</i></sub>)"_s;
    }

    void renderFractions()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(math(input), expected);
    }

    void renderRadicalsAndText_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("square root") << u"\\sqrt{x}"_s
                                     << u"√<span style=\"text-decoration:overline\"><i>x</i></span>"_s;
        QTest::newRow("cube root") << u"\\sqrt[3]{x}"_s
                                   << u"∛<span style=\"text-decoration:overline\"><i>x</i></span>"_s;
        QTest::newRow("nth root") << u"\\sqrt[n]{x}"_s
                                  << u"√<sup><i>n</i></sup><span style=\"text-decoration:overline\"><i>x</i></span>"_s;
        QTest::newRow("root of a fraction")
                << u"\\sqrt{\\frac{1}{2}}"_s << u"√<span style=\"text-decoration:overline\">½</span>"_s;
        QTest::newRow("text") << u"\\text{jeśli } x > 0"_s << u"jeśli <i>x</i> &gt; 0"_s;
        QTest::newRow("text escapes") << u"\\text{<b>albo</b>}"_s << u"&lt;b&gt;albo&lt;/b&gt;"_s;
        QTest::newRow("operatorname") << u"\\operatorname{argmax} f"_s << u"argmax <i>f</i>"_s;
        QTest::newRow("mathbf") << u"\\mathbf{v}"_s << u"<b>v</b>"_s; // upright bold, as in TeX
        QTest::newRow("mathbb") << u"\\mathbb{R}^n"_s << u"ℝ<sup><i>n</i></sup>"_s;
        QTest::newRow("overline") << u"\\overline{AB}"_s
                                  << u"<span style=\"text-decoration:overline\"><i>AB</i></span>"_s;
        QTest::newRow("vector") << u"\\vec{v}"_s << u"<i>v\u20d7</i>"_s;
        QTest::newRow("hat") << u"\\hat{x}"_s << u"<i>x\u0302</i>"_s;
        QTest::newRow("not equal") << u"\\not="_s << u"≠"_s;
    }

    void renderRadicalsAndText()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(math(input), expected);
    }

    void renderEnvironments_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("pmatrix") << u"\\begin{pmatrix} 1 & 2 \\\\ 3 & 4 \\end{pmatrix}"_s
                                 << u"( 1&nbsp;&nbsp;2; 3&nbsp;&nbsp;4 )"_s;
        QTest::newRow("bmatrix") << u"\\begin{bmatrix} a \\\\ b \\end{bmatrix}"_s << u"[ <i>a</i>; <i>b</i> ]"_s;
        QTest::newRow("cases") << u"\\begin{cases} x & \\text{gdy } y > 0 \\\\ 0 \\end{cases}"_s
                               << u"{ <i>x</i>, gdy <i>y</i> &gt; 0; 0 }"_s;
        QTest::newRow("aligned display") << u"\\begin{aligned} a &= b \\\\ c &= d \\end{aligned}"_s
                                         << u"<i>a</i> = <i>b</i><br/><i>c</i> = <i>d</i>"_s;
        QTest::newRow("array") << u"\\begin{array}{cc} 1 & 2 \\end{array}"_s << u"1&nbsp;&nbsp;2"_s;
    }

    void renderEnvironments()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const bool display = input.contains(u"aligned"_s);
        QCOMPARE(renderMath(input, display), expected);
    }

    void unknownStaysAsWritten()
    {
        // Nothing a note says may be lost: an unknown command, a stray brace and hostile
        // markup all come out as text, and only tags built by the renderer are emitted.
        QCOMPARE(math(u"\\foo{1}"_s), u"\\foo1"_s);
        QCOMPARE(math(u"a \\} b"_s), u"<i>a</i>}<i>b</i>"_s);
        QCOMPARE(math(u"a } b"_s), u"<i>a</i><i>b</i>"_s); // a stray brace vanishes, spaces as in LaTeX
        QCOMPARE(math(u"<script>alert(1)</script>"_s),
                 u"&lt; <i>script</i> &gt; <i>alert</i>(1) &lt; /<i>script</i> &gt;"_s);
        QCOMPARE(math(u"\\text{<img src=x onerror=y>}"_s), u"&lt;img src=x onerror=y&gt;"_s);
        QCOMPARE(math(u"100\\%"_s), u"100%"_s);
    }

    void displayKeepsLines()
    {
        // A display formula may hold lines of its own; an inline one joins them.
        QCOMPARE(displayMath(u"a = b \\\\ c = d"_s), u"<i>a</i> = <i>b</i><br/><i>c</i> = <i>d</i>"_s);
        QCOMPARE(math(u"a = b \\\\ c = d"_s), u"<i>a</i> = <i>b</i> <i>c</i> = <i>d</i>"_s);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    TestMath test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_markdown_math.moc"
