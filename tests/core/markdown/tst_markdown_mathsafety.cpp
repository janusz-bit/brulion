// markdown/math.h against hostile notes: whatever a note writes between `$`, renderMath() emits
// only the tags and attributes it builds itself (a synced or imported note must never get an
// <img>, <a> or <script> into the editor's rich text or the HTML export).
#include "markdown/inlinerenderer.h"
#include "markdown/math.h"

#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

/// Every tag renderMath() builds (attributes included); anything else starting with '<' is foreign.
const QStringList OwnTags{u"<i>"_s,
                          u"</i>"_s,
                          u"<b>"_s,
                          u"</b>"_s,
                          u"<sup>"_s,
                          u"</sup>"_s,
                          u"<sub>"_s,
                          u"</sub>"_s,
                          u"<br/>"_s,
                          u"<span style=\"text-decoration:overline\">"_s,
                          u"<span style=\"text-decoration:underline\">"_s,
                          u"<span style=\"font-family:monospace\">"_s,
                          u"<span style=\"font-family:sans-serif\">"_s,
                          u"<span style=\"font-variant:small-caps\">"_s,
                          u"</span>"_s};

/// The first problem of `html` as renderMath() output, empty when there is none: a '<' that does
/// not start one of the renderer's own tags, a bare '>', a '&' that does not start an entity, or
/// tags that do not nest.
QString foreignMarkup(const QString &html)
{
    QStringList open;
    for (qsizetype i = 0; i < html.size(); ++i) {
        const QChar c = html[i];
        if (c == u'<') {
            QString tag;
            for (const QString &t : OwnTags) {
                if (QStringView(html).sliced(i).startsWith(t)) {
                    tag = t;
                    break;
                }
            }
            if (tag.isEmpty())
                return u"foreign '<' at %1: %2"_s.arg(i).arg(html.mid(i, 60));
            if (tag.startsWith(u"</")) {
                const QString name = tag.sliced(2, tag.size() - 3);
                if (open.isEmpty() || open.last() != name)
                    return u"%1 at %2 closes nothing"_s.arg(tag).arg(i);
                open.removeLast();
            } else if (tag != u"<br/>") {
                open.append(tag.sliced(1).section(u' ', 0, 0).remove(u'>'));
            }
            i += tag.size() - 1;
            continue;
        }
        if (c == u'>')
            return u"bare '>' at %1: %2"_s.arg(i).arg(html.mid(qMax<qsizetype>(0, i - 40), 60));
        if (c == u'&') {
            const QStringView rest = QStringView(html).sliced(i);
            bool entity = rest.startsWith(u"&lt;") || rest.startsWith(u"&gt;") || rest.startsWith(u"&amp;")
                          || rest.startsWith(u"&quot;") || rest.startsWith(u"&nbsp;");
            if (!entity && rest.startsWith(u"&#")) {
                qsizetype j = 2;
                while (j < rest.size() && rest[j].isDigit())
                    ++j;
                entity = j > 2 && j < rest.size() && rest[j] == u';';
            }
            if (!entity)
                return u"bare '&' at %1: %2"_s.arg(i).arg(html.mid(i, 40));
        }
    }
    if (!open.isEmpty())
        return u"unclosed %1"_s.arg(open.join(u','));
    return {};
}

/// Commands followed by a delimiter character: \left, \right, \middle and the size commands.
const QStringList DelimiterCommands{u"\\left"_s,  u"\\right"_s, u"\\middle"_s, u"\\big"_s,   u"\\Big"_s,
                                    u"\\bigg"_s,  u"\\Bigg"_s,  u"\\bigl"_s,   u"\\bigr"_s,  u"\\bigm"_s,
                                    u"\\Bigl"_s,  u"\\Bigr"_s,  u"\\Bigm"_s,   u"\\biggl"_s, u"\\biggr"_s,
                                    u"\\biggm"_s, u"\\Biggl"_s, u"\\Biggr"_s,  u"\\Biggm"_s};

} // namespace

class TestMathSafety : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void injection_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected"); // inline rendering; empty: only checked for foreign markup
        QTest::newRow("left right image") << u"\\left<\\text{img src=https://example.invalid/p.png}\\right>"_s
                                          << u"⟨img src=https://example.invalid/p.png⟩"_s;
        QTest::newRow("left right onerror")
                << u"\\left<\\text{img src=x onerror=alert(1)}\\right>"_s << u"⟨img src=x onerror=alert(1)⟩"_s;
        QTest::newRow("bigl bigr anchor")
                << u"\\bigl<\\text{a href=task:toggle}\\bigr>KLIK"_s << u"⟨a href=task:toggle⟩<i>KLIK</i>"_s;
        QTest::newRow("Bigl lt") << u"\\Bigl<"_s << u"⟨"_s;
        QTest::newRow("Big gt") << u"\\Big>"_s << u"⟩"_s;
        QTest::newRow("middle quote") << u"\\middle\""_s << u"&quot;"_s;
        QTest::newRow("middle gt") << u"\\middle>"_s << u"⟩"_s;
        QTest::newRow("big ampersand") << u"\\big&"_s << u"&amp;"_s;
        QTest::newRow("inner product") << u"\\left< a, b \\right>"_s << u"⟨<i>a</i>, <i>b</i>⟩"_s;
        QTest::newRow("script element") << u"\\mathrm{\\left<script\\right>alert(1)\\left</script\\right>}"_s
                                        << QString();
        QTest::newRow("closing tag") << u"\\left<\\text{/a}\\right>"_s << u"⟨/a⟩"_s;
        QTest::newRow("entity") << u"\\left&\\text{lt;img}"_s << u"&amp;lt;img"_s;
        QTest::newRow("text") << u"\\text{<b>x</b>}"_s << u"&lt;b&gt;x&lt;/b&gt;"_s;
        QTest::newRow("textbf lt") << u"\\textbf{<}"_s << u"<b>&lt;</b>"_s;
        QTest::newRow("texttt quote") << u"\\texttt{\" onmouseover=\"x}"_s
                                      << u"<span style=\"font-family:monospace\">&quot; onmouseover=&quot;x</span>"_s;
        QTest::newRow("operatorname entity") << u"\\operatorname{&amp;}"_s << u"&amp;amp;"_s;
        QTest::newRow("unknown command") << u"\\foo<bar>"_s << QString();
        QTest::newRow("raw characters") << u"<>&\"' &lt; &#60;"_s << QString();
        QTest::newRow("escaped characters") << u"\\< \\> \\& \\\" \\'"_s << QString();
        QTest::newRow("not lt") << u"\\not<"_s << u"≮"_s;
        QTest::newRow("hat of gt") << u"\\hat{>}"_s << QString();
        QTest::newRow("matrix") << u"\\begin{matrix} \\& & < \\\\ > \\end{matrix}"_s << QString();
        QTest::newRow("environment name") << u"\\begin{<b>} x \\end{<b>}"_s << QString();
        QTest::newRow("root index") << u"\\sqrt[<]{&}"_s << QString();
        QTest::newRow("colour") << u"\\color{red\" onload=\"x}{y} \\textcolor{<b>}{z}"_s << QString();
        QTest::newRow("href") << u"\\href{javascript:alert(1)}{x} \\url{<x>}"_s << QString();
    }

    void injection()
    {
        // Only tags built by the renderer come out: the delimiter after \left, \right, \middle or
        // \big… is escaped (or drawn as ⟨ ⟩) like every other character of a note.
        QFETCH(QString, input);
        QFETCH(QString, expected);
        for (const bool display : {false, true}) {
            const QString html = renderMath(input, display);
            QVERIFY2(foreignMarkup(html).isEmpty(), qPrintable(foreignMarkup(html) + u" in "_s + html));
            QVERIFY2(!html.contains(u"<img"_s) && !html.contains(u"<a "_s) && !html.contains(u"<script"_s),
                     qPrintable(html));
        }
        if (!expected.isNull())
            QCOMPARE(renderMath(input, false), expected);
    }

    void everyDelimiterCharacterIsEscaped()
    {
        // Every printable ASCII character (also after a backslash) as the delimiter of every
        // delimiter command, alone, before more text and around tag-like text.
        int checked = 0;
        for (const QString &command : DelimiterCommands) {
            for (char16_t u = 0x21; u < 0x7f; ++u) {
                const QString c(QChar{u});
                const QStringList inputs{command + c, command + u'\\' + c, command + u' ' + c + u"img src=x"_s,
                                         command + c + u"\\text{img src=x}"_s + command + c};
                for (const QString &input : inputs) {
                    for (const bool display : {false, true}) {
                        const QString html = renderMath(input, display);
                        QVERIFY2(foreignMarkup(html).isEmpty(),
                                 qPrintable(input + u" -> "_s + html + u": "_s + foreignMarkup(html)));
                        ++checked;
                    }
                }
            }
        }
        QCOMPARE(checked, DelimiterCommands.size() * 94 * 4 * 2);
    }

    void randomFormulasEmitOnlyOwnMarkup()
    {
        // A deterministic fuzz over the constructs that take arguments, delimiters and raw text.
        // Space-separated tokens, then the ones that are spaces or need a QChar themselves.
        static const QStringList tokens = [] {
            QStringList t = u"{ } { } ^ _ \\frac \\sqrt [ ] \\left \\right \\middle \\big \\Bigl \\biggr \\bigcup "
                            u"\\bigstar < > & ' \\text{ \\operatorname{ \\mathrm{ \\mathbb{ \\textbf{ \\texttt{ "
                            u"\\textsc{ \\mathbf{ \\begin{matrix} \\end{matrix} \\begin{cases} \\end{cases} "
                            u"\\begin{array}{c \\\\ \\ \\{ \\} \\& \\< \\| \\. x ą Ż 1 2 + - = \\not \\hat \\vec "
                            u"\\overline \\alpha \\foo $ ~ , ; \\overset \\genfrac \\binom \\color{ \\textcolor{ "
                            u"\\tag{ \\' \\, \\limits 😀 𝔸 \\sqrt[ \\cfrac[ \\left\\{ \\right\\} \\left. \\left< "
                            u"\\right>"_s.split(u' ', Qt::SkipEmptyParts);
            t << u"\""_s << u" "_s << u"\n"_s << u"img src=x"_s << u"a href=y"_s << QString(QChar(0xD800))
              << QString(QChar(0xDC00)) << QString(QChar(0x0328));
            return t;
        }();
        QRandomGenerator rng(20261021);
        for (int n = 0; n < 20000; ++n) {
            QString input;
            const int length = 1 + int(rng.bounded(30));
            for (int k = 0; k < length; ++k)
                input += tokens[int(rng.bounded(qsizetype(tokens.size())))];
            const bool display = rng.bounded(2) == 1;
            const QString html = renderMath(input, display);
            QVERIFY2(foreignMarkup(html).isEmpty(),
                     qPrintable(input + u" -> "_s + html + u": "_s + foreignMarkup(html)));
        }
    }

    void injectionThroughTheEditor()
    {
        // The same formula in a note's text, as the editor renders it: no element of the note's
        // own, so nothing is loaded and no link is made.
        const QString html = InlineRenderer().render(
                u"Wzór: $\\left<\\text{img src=https://example.invalid/ślad.png}\\right>$ koniec"_s);
        QVERIFY2(!html.contains(u"<img"_s), qPrintable(html));
        QVERIFY2(html.contains(u"⟨img src=https://example.invalid/ślad.png⟩"_s), qPrintable(html));
    }
};

QTEST_GUILESS_MAIN(TestMathSafety)

#include "tst_markdown_mathsafety.moc"
