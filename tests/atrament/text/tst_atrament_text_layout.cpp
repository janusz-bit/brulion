// Atrament's paragraph layout: shaping, itemization with fallback, wrapping, line metrics and the
// editing queries (carets, hit tests, selections, ↑/↓).
#include "text/segmentation.h"
#include "text/textlayout.h"
#include "text/textsystem.h"

#include "textfixtures.h"

#include <QElapsedTimer>
#include <QtTest>

#include <cmath>

using namespace brulion::atrament;
using namespace Qt::StringLiterals;
using textfixtures::BundledFonts;
using textfixtures::qs;
using textfixtures::u16;

namespace {

const QString kPolish = u"Zażółć gęślą jaźń"_s;

FontRun run(int length, FaceId face, float size = 16, std::uint32_t style = 0)
{
    FontRun r;
    r.length = length;
    r.face = face;
    r.fontSize = size;
    r.styleIndex = style;
    return r;
}

TextLayout layout(const BundledFonts &b, const QString &text, std::optional<float> width = std::nullopt,
                  float size = 16, FaceId face = 0)
{
    const std::u16string t = u16(text);
    const FontRun runs[] = {run(int(t.size()), face == 0 ? b.interRegular : face, size)};
    return layoutParagraph(*b.fonts, t, runs, ParagraphStyle{width, 0});
}

float widthOf(const BundledFonts &b, const QString &text, float size = 16)
{
    return layout(b, text, std::nullopt, size).maxContentWidth();
}

QString lineText(const TextLayout &l, const TextLine &line)
{
    return qs(std::u16string_view(l.text()).substr(std::size_t(line.start), std::size_t(line.next - line.start)));
}

QStringList lineTexts(const TextLayout &l)
{
    QStringList out;
    for (const TextLine &line : l.lines())
        out.push_back(lineText(l, line));
    return out;
}

// Invariants every layout keeps.
void checkLayout(const TextLayout &l)
{
    const auto &lines = l.lines();
    QVERIFY(!lines.empty());
    QCOMPARE(lines.front().start, 0);
    float top = 0;
    std::uint32_t glyphs = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const TextLine &line = lines[i];
        QVERIFY(line.start <= line.end);
        QVERIFY(line.end <= line.next);
        if (i + 1 < lines.size())
            QCOMPARE(lines[i + 1].start, line.next);
        else
            QCOMPARE(line.next, l.length());
        QCOMPARE(line.top, top);
        top += line.height;
        QVERIFY(line.baseline >= line.top - 1e-3f);
        QCOMPARE(line.firstGlyph, glyphs);
        glyphs += line.glyphCount;
        QVERIFY(isGraphemeBoundary(l.text(), line.start));
        QVERIFY(isGraphemeBoundary(l.text(), line.end));
        for (std::uint32_t g = line.firstGlyph; g < line.firstGlyph + line.glyphCount; ++g) {
            const PositionedGlyph &glyph = l.glyphs()[g];
            QVERIFY(glyph.cluster >= line.start && glyph.cluster < std::max(line.next, line.start + 1));
        }
    }
    QCOMPARE(std::size_t(glyphs), l.glyphs().size());
    QCOMPARE(l.size().height, top);
    QString all;
    for (const TextLine &line : lines)
        all += lineText(l, line);
    QCOMPARE(all, qs(l.text()));
}

void compareLayouts(const TextLayout &a, const TextLayout &b)
{
    QCOMPARE(a.lines().size(), b.lines().size());
    for (std::size_t i = 0; i < a.lines().size(); ++i) {
        const TextLine &x = a.lines()[i];
        const TextLine &y = b.lines()[i];
        QCOMPARE(x.start, y.start);
        QCOMPARE(x.end, y.end);
        QCOMPARE(x.next, y.next);
        QCOMPARE(x.top, y.top);
        QCOMPARE(x.height, y.height);
        QCOMPARE(x.baseline, y.baseline);
        QCOMPARE(x.width, y.width);
        QCOMPARE(x.x, y.x);
        QCOMPARE(x.firstGlyph, y.firstGlyph);
        QCOMPARE(x.glyphCount, y.glyphCount);
    }
    QCOMPARE(a.glyphs().size(), b.glyphs().size());
    for (std::size_t i = 0; i < a.glyphs().size(); ++i) {
        const PositionedGlyph &x = a.glyphs()[i];
        const PositionedGlyph &y = b.glyphs()[i];
        QCOMPARE(x.glyph, y.glyph);
        QCOMPARE(x.face, y.face);
        QCOMPARE(x.cluster, y.cluster);
        QVERIFY(std::abs(x.position.x - y.position.x) < 1e-3f);
        QVERIFY(std::abs(x.position.y - y.position.y) < 1e-3f);
    }
    QCOMPARE(a.size(), b.size());
    QCOMPARE(a.minContentWidth(), b.minContentWidth());
    QCOMPARE(a.maxContentWidth(), b.maxContentWidth());
}

QString polishParagraph(int i)
{
    static const QStringList words = {u"zażółć"_s,    u"gęślą"_s,      u"jaźń"_s,   u"Źdźbło"_s,   u"pięćdziesiąt"_s,
                                      u"chrząszcz"_s, u"brzmi"_s,      u"w"_s,      u"trzcinie"_s, u"Łódź"_s,
                                      u"żółwiątko"_s, u"ćma"_s,        u"mówią,"_s, u"że"_s,       u"ŚWIĘTOKRZYSKIE"_s,
                                      u"ńaś"_s,       u"Gżegżółka."_s, u"ósmy"_s};
    QString out;
    for (int k = 0; out.size() < 200; ++k) {
        if (!out.isEmpty())
            out += u' ';
        out += words[(i * 7 + k * 5) % words.size()];
    }
    return out;
}

} // namespace

class TestAtramentTextLayout : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void shapingPolish();
    void decomposedComposes();
    void kerning();
    void features();
    void fallbackRuns();
    void ligatureCarets();
    void wrapPolish_data();
    void wrapPolish();
    void wrapLongWord();
    void wrapNarrowerThanAGrapheme();
    void mandatoryBreaks();
    void trailingNewline();
    void contentWidths();
    void carets();
    void hitTestsRoundTrip_data();
    void hitTestsRoundTrip();
    void hitTestsOutside();
    void selection();
    void selectionAcrossBreaks();
    void upDown();
    void affinityAtWrap();
    void rewrapMatches_data();
    void rewrapMatches();
    void aligned_data();
    void aligned();
    void emptyText();
    void severalFonts();
    void lineHeightOverride();
    void tabs();
    void malformed();
    void emptyCollection();
    void timing();

private:
    BundledFonts m_fonts;
};

void TestAtramentTextLayout::initTestCase()
{
    QVERIFY(m_fonts.allLoaded());
}

void TestAtramentTextLayout::shapingPolish()
{
    const TextLayout l = layout(m_fonts, u"Zażółć"_s);
    checkLayout(l);
    QCOMPARE(l.glyphs().size(), std::size_t(6));
    for (int i = 0; i < 6; ++i) {
        QCOMPARE(l.glyphs()[std::size_t(i)].cluster, i);
        QCOMPARE(l.glyphs()[std::size_t(i)].face, m_fonts.interRegular);
        QVERIFY(l.glyphs()[std::size_t(i)].glyph != 0); // no .notdef
        QCOMPARE(l.glyphs()[std::size_t(i)].fontSize, 16.0f);
    }
    QCOMPARE(l.lines().size(), std::size_t(1));
    const TextLine &line = l.lines()[0];
    QCOMPARE(line.ascent, 1984.0f / 2048 * 16);
    QCOMPARE(line.descent, 494.0f / 2048 * 16);
    QCOMPARE(line.baseline, line.ascent);
    QCOMPARE(line.height, line.ascent + line.descent);
    // Glyphs sit on the baseline, left to right.
    for (std::size_t i = 1; i < l.glyphs().size(); ++i)
        QVERIFY(l.glyphs()[i].position.x > l.glyphs()[i - 1].position.x);
    for (const PositionedGlyph &g : l.glyphs())
        QCOMPARE(g.position.y, line.baseline);
    QCOMPARE(l.glyphs()[0].position.x, 0.0f);
    QVERIFY(l.size().width > 30 && l.size().width < 70);
}

void TestAtramentTextLayout::decomposedComposes()
{
    const TextLayout composed = layout(m_fonts, u"ą"_s);
    const TextLayout decomposed = layout(m_fonts, u"a\u0328"_s);
    QCOMPARE(composed.glyphs().size(), std::size_t(1));
    QCOMPARE(decomposed.glyphs().size(), std::size_t(1)); // HarfBuzz composes it for Inter
    QCOMPARE(decomposed.glyphs()[0].glyph, composed.glyphs()[0].glyph);
    QCOMPARE(decomposed.glyphs()[0].cluster, 0);
    QCOMPARE(decomposed.size().width, composed.size().width);
    // The caret inside the cluster stays at its start; after it, at its end.
    QCOMPARE(decomposed.caretX(1), 0.0f);
    QCOMPARE(decomposed.caretX(2), decomposed.size().width);
}

void TestAtramentTextLayout::kerning()
{
    const float av = widthOf(m_fonts, u"AV"_s);
    const float a = widthOf(m_fonts, u"A"_s);
    const float v = widthOf(m_fonts, u"V"_s);
    QVERIFY2(av < a + v - 0.5f, qPrintable(u"%1 vs %2"_s.arg(av).arg(a + v)));
    // Kerning across a run boundary of the same face: the runs shape apart.
    const std::u16string text = u"AV";
    const FontRun split[] = {run(1, m_fonts.interRegular), run(1, m_fonts.interRegular)};
    const TextLayout apart = layoutParagraph(*m_fonts.fonts, text, split, {});
    QCOMPARE(apart.size().width, a + v);
}

void TestAtramentTextLayout::features()
{
    const std::u16string text = u"AV 10";
    FontRun r = run(int(text.size()), m_fonts.interRegular);
    const float kerned = layoutParagraph(*m_fonts.fonts, text, std::span(&r, 1), {}).size().width;
    r.features = u"-kern";
    const float unkerned = layoutParagraph(*m_fonts.fonts, text, std::span(&r, 1), {}).size().width;
    QVERIFY(unkerned > kerned + 0.5f);
    r.features = u"kern=0, tnum";
    const TextLayout tnum = layoutParagraph(*m_fonts.fonts, text, std::span(&r, 1), {});
    QVERIFY(tnum.size().width >= kerned); // kern=0 parsed
    r.features = u"garbage!!, ąę, =";
    const float garbage = layoutParagraph(*m_fonts.fonts, text, std::span(&r, 1), {}).size().width;
    QCOMPARE(garbage, kerned);
    r.language = u"pl";
    QCOMPARE(layoutParagraph(*m_fonts.fonts, text, std::span(&r, 1), {}).glyphs().size(), std::size_t(5));
}

void TestAtramentTextLayout::fallbackRuns()
{
    const QString text = u"Hej 👍🏽 ⨆ ż"_s;
    const TextLayout l = layout(m_fonts, text);
    checkLayout(l);
    for (const PositionedGlyph &g : l.glyphs()) {
        const char16_t c = l.text()[std::size_t(g.cluster)];
        if (c == 0xD83D) // the emoji and its modifier, from the emoji face
            QCOMPARE(g.face, m_fonts.emoji);
        else if (c == 0x2A06)
            QCOMPARE(g.face, m_fonts.math);
        else
            QCOMPARE(g.face, m_fonts.interRegular);
        QVERIFY(g.glyph != 0);
    }
    // A fallback never makes the line taller: Inter's metrics.
    QCOMPARE(l.lines()[0].height, (1984.0f + 494.0f) / 2048 * 16);
    // A whole ZWJ sequence goes to the face of its first character (🧑 ZWJ 💻).
    const TextLayout zwj = layout(m_fonts, u"a🧑\u200D💻b"_s);
    QCOMPARE(zwj.glyphs().size(), std::size_t(4)); // a, 🧑, 💻 (the ZWJ hidden or merged), b
    for (const PositionedGlyph &g : zwj.glyphs())
        QCOMPARE(g.face, g.cluster >= 1 && g.cluster < 6 ? m_fonts.emoji : m_fonts.interRegular);
    // Nobody has 👨: it stays in the run's face (.notdef), with its sequence.
    const TextLayout family = layout(m_fonts, u"👨\u200D👩"_s);
    for (const PositionedGlyph &g : family.glyphs())
        QCOMPARE(g.face, m_fonts.interRegular);
}

void TestAtramentTextLayout::ligatureCarets()
{
    // JetBrains Mono's "->" ligature (calt): carets inside it are spread over its advance.
    const std::u16string text = u"a->b";
    const FontRun r[] = {run(int(text.size()), m_fonts.monoRegular)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, text, r, {});
    checkLayout(l);
    for (int i = 1; i <= int(text.size()); ++i)
        QVERIFY(l.caretX(i) > l.caretX(i - 1));
    // A monospaced font: every character as wide.
    const float cell = l.caretX(1);
    for (int i = 1; i <= int(text.size()); ++i)
        QVERIFY(std::abs(l.caretX(i) - cell * float(i)) < 0.01f);
}

void TestAtramentTextLayout::wrapPolish_data()
{
    QTest::addColumn<float>("width");
    QTest::addColumn<QStringList>("lines");
    const float all = widthOf(m_fonts, kPolish);
    const float two = widthOf(m_fonts, u"Zażółć gęślą"_s);
    const float one = widthOf(m_fonts, u"Zażółć"_s);
    QTest::newRow("unlimited") << 1000.0f << QStringList{kPolish};
    QTest::newRow("exactly") << all << QStringList{kPolish};
    QTest::newRow("a bit less") << all - 0.1f << QStringList{u"Zażółć gęślą "_s, u"jaźń"_s};
    QTest::newRow("two words") << two + 1 << QStringList{u"Zażółć gęślą "_s, u"jaźń"_s};
    QTest::newRow("one word") << one + 1 << QStringList{u"Zażółć "_s, u"gęślą "_s, u"jaźń"_s};
    QTest::newRow("narrower than a word") << one - 1 << QStringList{u"Zażół"_s, u"ć "_s, u"gęślą "_s, u"jaźń"_s};
}

void TestAtramentTextLayout::wrapPolish()
{
    QFETCH(float, width);
    QFETCH(QStringList, lines);
    const TextLayout l = layout(m_fonts, kPolish, width);
    checkLayout(l);
    QCOMPARE(lineTexts(l), lines);
    QCOMPARE(l.wrapWidth(), std::optional<float>(width));
    for (const TextLine &line : l.lines()) {
        QVERIFY2(line.width <= width + 1e-3f, qPrintable(u"%1 > %2"_s.arg(line.width).arg(width)));
        QVERIFY(l.text()[std::size_t(line.start)] != u' ');
        QCOMPARE(line.end, line.next); // soft wraps: the spaces hang on the line
    }
    QVERIFY(l.size().width <= width + 1e-3f);
    // Glyphs of a line start at its left edge.
    for (const TextLine &line : l.lines())
        QCOMPARE(l.glyphs()[line.firstGlyph].position.x, 0.0f);
}

void TestAtramentTextLayout::wrapLongWord()
{
    const QString word = u"Konstantynopolitańczykówna\u0328ę"_s; // with a decomposed ą near the end
    const TextLayout l = layout(m_fonts, u"Ala "_s + word + u" ma"_s, 60.0f);
    checkLayout(l);
    QVERIFY(l.lines().size() >= 4);
    QCOMPARE(lineText(l, l.lines()[0]), u"Ala "_s);
    for (const TextLine &line : l.lines()) {
        QVERIFY(line.width <= 60.0f + 1e-3f);
        QVERIFY(line.end > line.start);
    }
    // The combining ogonek never starts a line.
    for (const TextLine &line : l.lines())
        QVERIFY(l.text()[std::size_t(line.start)] != u'\u0328');
    QVERIFY(lineText(l, l.lines().back()).endsWith(u" ma"_s)); // the word's tail fits with "ma"
    // min-content is the whole word: the widest unbreakable piece.
    QCOMPARE(l.minContentWidth(), widthOf(m_fonts, word));
}

void TestAtramentTextLayout::wrapNarrowerThanAGrapheme()
{
    const TextLayout l = layout(m_fonts, u"ab c👍"_s, 1.0f);
    checkLayout(l);
    QCOMPARE(lineTexts(l), (QStringList{u"a"_s, u"b "_s, u"c"_s, u"👍"_s}));
    const TextLayout zero = layout(m_fonts, u"ab"_s, 0.0f);
    QCOMPARE(lineTexts(zero), (QStringList{u"a"_s, u"b"_s}));
    const TextLayout negative = layout(m_fonts, u"ab"_s, -5.0f);
    QCOMPARE(lineTexts(negative), (QStringList{u"a"_s, u"b"_s}));
    QCOMPARE(negative.wrapWidth(), std::optional<float>(0.0f));
    const TextLayout nan = layout(m_fonts, u"a b"_s, std::nanf(""));
    QCOMPARE(nan.lines().size(), std::size_t(1));
    QVERIFY(!nan.wrapWidth());
}

void TestAtramentTextLayout::mandatoryBreaks()
{
    const TextLayout l = layout(m_fonts, u"ab\nc\r\nd e"_s, 1000.0f);
    checkLayout(l);
    const auto &lines = l.lines();
    QCOMPARE(lines.size(), std::size_t(4));
    QCOMPARE((std::array{lines[0].start, lines[0].end, lines[0].next}), (std::array{0, 2, 3}));
    QCOMPARE((std::array{lines[1].start, lines[1].end, lines[1].next}), (std::array{3, 4, 6}));
    QCOMPARE((std::array{lines[2].start, lines[2].end, lines[2].next}), (std::array{6, 7, 8}));
    QCOMPARE((std::array{lines[3].start, lines[3].end, lines[3].next}), (std::array{8, 9, 9}));
    // The break characters draw nothing.
    for (const PositionedGlyph &g : l.glyphs()) {
        const char16_t c = l.text()[std::size_t(g.cluster)];
        QVERIFY(c != u'\n' && c != u'\r' && c != u' ');
    }
    QCOMPARE(l.glyphs().size(), std::size_t(5));
    QCOMPARE(lines[0].width, widthOf(m_fonts, u"ab"_s));
    // A mandatory break also ends a line that would fit more.
    const TextLayout unwrapped = layout(m_fonts, u"a\nb"_s);
    QCOMPARE(unwrapped.lines().size(), std::size_t(2));
    QCOMPARE(unwrapped.maxContentWidth(), std::max(widthOf(m_fonts, u"a"_s), widthOf(m_fonts, u"b"_s)));
    // Spaces before a break hang too.
    const TextLayout spaces = layout(m_fonts, u"ab   \ncd"_s);
    QCOMPARE(spaces.lines()[0].width, widthOf(m_fonts, u"ab"_s));
    QCOMPARE(spaces.lines()[0].end, 5);
}

void TestAtramentTextLayout::trailingNewline()
{
    const TextLayout l = layout(m_fonts, u"abc\n"_s);
    checkLayout(l);
    QCOMPARE(l.lines().size(), std::size_t(2));
    const TextLine &last = l.lines()[1];
    QCOMPARE(last.start, 4);
    QCOMPARE(last.end, 4);
    QCOMPARE(last.next, 4);
    QCOMPARE(last.height, l.lines()[0].height);
    QCOMPARE(last.top, l.lines()[0].height);
    QCOMPARE(last.glyphCount, 0u);
    QCOMPARE(l.lineIndex(4), 1);
    QCOMPARE(l.lineIndex(3), 0);
    QCOMPARE(l.caretRect(4).top(), last.top);
    QCOMPARE(l.caretX(4), 0.0f);
    QCOMPARE(l.offsetAt({100, last.top + 2}), 4);
    const TextLayout two = layout(m_fonts, u"\n"_s);
    checkLayout(two);
    QCOMPARE(two.lines().size(), std::size_t(2));
    QVERIFY(two.lines()[0].height > 0);
    QCOMPARE(two.size().width, 0.0f);
    const TextLayout crlf = layout(m_fonts, u"x\r\n"_s);
    QCOMPARE(crlf.lines().size(), std::size_t(2));
    QCOMPARE(crlf.lines()[0].end, 1);
    QCOMPARE(crlf.lines()[0].next, 3);
}

void TestAtramentTextLayout::contentWidths()
{
    const TextLayout l = layout(m_fonts, kPolish);
    float widest = 0;
    for (const QString &word : kPolish.split(u' '))
        widest = std::max(widest, widthOf(m_fonts, word));
    QCOMPARE(l.minContentWidth(), widest);
    QCOMPARE(l.maxContentWidth(), l.size().width);
    QCOMPARE(l.maxContentWidth(), widthOf(m_fonts, kPolish));
    // At its min-content width it wraps at every space, never inside a word.
    const TextLayout narrow = layout(m_fonts, kPolish, l.minContentWidth());
    QCOMPARE(lineTexts(narrow), (QStringList{u"Zażółć "_s, u"gęślą "_s, u"jaźń"_s}));
    QCOMPARE(narrow.minContentWidth(), l.minContentWidth());
    // At its max-content width it does not wrap.
    QCOMPARE(layout(m_fonts, kPolish, l.maxContentWidth()).lines().size(), std::size_t(1));
    // Trailing spaces count for neither.
    QCOMPARE(layout(m_fonts, u"ab   "_s).maxContentWidth(), widthOf(m_fonts, u"ab"_s));
    QCOMPARE(layout(m_fonts, u"ab   "_s).minContentWidth(), widthOf(m_fonts, u"ab"_s));
    // Lines between mandatory breaks: the widest.
    QCOMPARE(layout(m_fonts, u"ab\nabcd\nabc"_s).maxContentWidth(), widthOf(m_fonts, u"abcd"_s));
}

void TestAtramentTextLayout::carets()
{
    const TextLayout l = layout(m_fonts, u"Zażółć gęślą jaźń 👍🏽 e\u0301 🇵🇱 koniec"_s, 90.0f);
    checkLayout(l);
    QVERIFY(l.lines().size() > 2);
    for (std::size_t li = 0; li < l.lines().size(); ++li) {
        const TextLine &line = l.lines()[li];
        float previous = -1;
        for (int o = line.start; o <= line.end; ++o) {
            const CaretAffinity affinity = o == line.end ? CaretAffinity::Upstream : CaretAffinity::Downstream;
            const float x = l.caretX(o, affinity);
            QVERIFY2(x >= previous, qPrintable(u"line %1 offset %2"_s.arg(li).arg(o)));
            previous = x;
            const Rect r = l.caretRect(o, affinity);
            QCOMPARE(r.width(), 0.0f);
            QCOMPARE(r.left(), x);
            QCOMPARE(r.top(), line.top);
            QCOMPARE(r.height(), line.height);
        }
        QCOMPARE(l.caretX(line.start), 0.0f);
    }
    // Inside a surrogate pair or a cluster: the cluster's start.
    const TextLayout emoji = layout(m_fonts, u"a👍🏽b"_s);
    QCOMPARE(emoji.caretX(2), emoji.caretX(1));
    QCOMPARE(emoji.caretX(4), emoji.caretX(1));
    QVERIFY(emoji.caretX(5) > emoji.caretX(1));
    // Out of range: clamped.
    QCOMPARE(emoji.caretX(-3), 0.0f);
    QCOMPARE(emoji.caretX(1000), emoji.size().width);
}

void TestAtramentTextLayout::hitTestsRoundTrip_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<float>("width");
    QTest::newRow("Polish wrapped") << kPolish << 60.0f;
    QTest::newRow("mixed") << u"Zażółć gęślą jaźń 👍🏽 e\u0301 🇵🇱 ⨆ koniec, ĄĆĘŁŃÓŚŹŻ."_s << 90.0f;
    QTest::newRow("mandatory") << u"raz\ndwa\r\n\ntrzy cztery pięć"_s << 50.0f;
    QTest::newRow("unwrapped") << kPolish << -1.0f;
    QTest::newRow("long word") << u"Konstantynopolitańczykówna"_s << 40.0f;
}

void TestAtramentTextLayout::hitTestsRoundTrip()
{
    QFETCH(QString, text);
    QFETCH(float, width);
    const TextLayout l = layout(m_fonts, text, width < 0 ? std::nullopt : std::optional<float>(width));
    checkLayout(l);
    const std::u16string &t = l.text();
    for (int i = 0; i <= int(t.size()); i = nextGraphemeBoundary(t, i)) {
        for (const CaretAffinity affinity : {CaretAffinity::Downstream, CaretAffinity::Upstream}) {
            const Rect r = l.caretRect(i, affinity);
            CaretAffinity got = CaretAffinity::Downstream;
            const int hit = l.offsetAt(r.center(), &got);
            QVERIFY2(hit == i, qPrintable(u"offset %1 → %2"_s.arg(i).arg(hit)));
            QCOMPARE(l.lineIndex(hit, got), l.lineIndex(i, affinity));
        }
        if (i == int(t.size()))
            break;
    }
}

void TestAtramentTextLayout::hitTestsOutside()
{
    const TextLayout l = layout(m_fonts, kPolish, 60.0f);
    const auto &lines = l.lines();
    QCOMPARE(l.offsetAt({-10, -10}), 0);
    QCOMPARE(l.offsetAt({1000, -10}, nullptr), lines[0].end);
    QCOMPARE(l.offsetAt({-10, 1000}), lines.back().start);
    QCOMPARE(l.offsetAt({1000, 1000}), l.length());
    CaretAffinity affinity = CaretAffinity::Downstream;
    QCOMPARE(l.offsetAt({1000, lines[0].top + 1}, &affinity), lines[0].end);
    QCOMPARE(affinity, CaretAffinity::Upstream);
    QCOMPARE(l.offsetAt({-5, lines[1].top + 1}, &affinity), lines[1].start);
    QCOMPARE(affinity, CaretAffinity::Downstream);
    // Between two characters: the nearer boundary.
    const float x1 = l.caretX(1);
    const float x2 = l.caretX(2);
    QCOMPARE(l.offsetAt({x1 + (x2 - x1) * 0.4f, 1}), 1);
    QCOMPARE(l.offsetAt({x1 + (x2 - x1) * 0.6f, 1}), 2);
}

void TestAtramentTextLayout::selection()
{
    const TextLayout l = layout(m_fonts, kPolish, 60.0f);
    const auto &lines = l.lines();
    QCOMPARE(lines.size(), std::size_t(3));
    // Within one line.
    std::vector<Rect> rects = l.selectionRects({1, 4});
    QCOMPARE(rects.size(), std::size_t(1));
    QCOMPARE(rects[0].left(), l.caretX(1));
    QCOMPARE(rects[0].right(), l.caretX(4));
    QCOMPARE(rects[0].top(), lines[0].top);
    QCOMPARE(rects[0].height(), lines[0].height);
    // Reversed ranges are the same.
    QCOMPARE(l.selectionRects({4, 1}), rects);
    // Everything: one rectangle per line, over its hanging spaces.
    rects = l.selectionRects({0, l.length()});
    QCOMPARE(rects.size(), lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        QCOMPARE(rects[i].left(), 0.0f);
        QCOMPARE(rects[i].top(), lines[i].top);
        QCOMPARE(rects[i].height(), lines[i].height);
        if (i + 1 < lines.size())
            QVERIFY(rects[i].right() > lines[i].width); // the hanging space
        else
            QCOMPARE(rects[i].right(), lines[i].width);
    }
    // Ending exactly at a wrap point: the next line is untouched.
    QCOMPARE(l.selectionRects({0, lines[1].start}).size(), std::size_t(1));
    // Starting at a wrap point: only the next line.
    rects = l.selectionRects({lines[1].start, lines[1].start + 2});
    QCOMPARE(rects.size(), std::size_t(1));
    QCOMPARE(rects[0].top(), lines[1].top);
    // Empty and out of range.
    QVERIFY(l.selectionRects({3, 3}).empty());
    QCOMPARE(l.selectionRects({-100, 100}).size(), lines.size());
}

void TestAtramentTextLayout::selectionAcrossBreaks()
{
    const TextLayout l = layout(m_fonts, u"ab\n\ncd"_s);
    const auto &lines = l.lines();
    QCOMPARE(lines.size(), std::size_t(3));
    const float space = widthOf(m_fonts, u"a b"_s) - widthOf(m_fonts, u"ab"_s);
    const std::vector<Rect> rects = l.selectionRects({1, 6});
    QCOMPARE(rects.size(), std::size_t(3));
    QCOMPARE(rects[0].left(), l.caretX(1));
    QVERIFY(std::abs(rects[0].right() - (lines[0].width + space)) < 0.5f); // the break: a space
    QCOMPARE(rects[1].left(), 0.0f);                                       // the empty line: just its break
    QVERIFY(std::abs(rects[1].right() - space) < 0.5f);
    QCOMPARE(rects[2].right(), lines[2].width);
    // Only the line break selected.
    const std::vector<Rect> newline = l.selectionRects({2, 3});
    QCOMPARE(newline.size(), std::size_t(1));
    QCOMPARE(newline[0].left(), lines[0].width);
    QVERIFY(newline[0].width() > 0);
}

void TestAtramentTextLayout::upDown()
{
    const TextLayout l = layout(m_fonts, kPolish, 60.0f);
    const auto &lines = l.lines();
    QCOMPARE(l.offsetAbove(3, l.caretX(3)), -1);
    QCOMPARE(l.offsetBelow(lines.back().start + 1, 0), -1);
    QCOMPARE(l.offsetBelow(0, 0), lines[1].start);
    QCOMPARE(l.offsetAbove(lines[1].start, 0), 0);
    // The nearest boundary at the same x on the next line.
    const float x = l.caretX(lines[1].start + 2);
    QCOMPARE(l.offsetBelow(lines[1].start + 2, x), l.offsetAt({x, lines[2].top + 1}));
    QCOMPARE(l.offsetAbove(lines[2].start + 2, l.caretX(lines[2].start + 2)),
             l.offsetAt({l.caretX(lines[2].start + 2), lines[1].top + 1}));
    // Far right: the last position before the wrap (the caret stays on that line).
    const int below = l.offsetBelow(0, 1000);
    QVERIFY(below < lines[1].next);
    QCOMPARE(l.lineIndex(below), 1);
    const int bottom = l.offsetBelow(lines[1].start, 1000);
    QCOMPARE(bottom, l.length()); // the last line has no wrap at its end
    QCOMPARE(l.offsetAbove(l.length(), 1000), below);
}

void TestAtramentTextLayout::affinityAtWrap()
{
    const TextLayout l = layout(m_fonts, kPolish, 60.0f);
    const int wrap = l.lines()[1].start;
    QCOMPARE(l.lineIndex(wrap), 1);
    QCOMPARE(l.lineIndex(wrap, CaretAffinity::Downstream), 1);
    QCOMPARE(l.lineIndex(wrap, CaretAffinity::Upstream), 0);
    QCOMPARE(l.caretX(wrap, CaretAffinity::Downstream), 0.0f);
    QVERIFY(l.caretX(wrap, CaretAffinity::Upstream) > l.lines()[0].width); // after the hanging space
    QCOMPARE(l.caretRect(wrap, CaretAffinity::Upstream).top(), l.lines()[0].top);
    QCOMPARE(l.caretRect(wrap, CaretAffinity::Downstream).top(), l.lines()[1].top);
    // Not at a wrap point: the affinity changes nothing.
    QCOMPARE(l.lineIndex(3, CaretAffinity::Upstream), 0);
    QCOMPARE(l.lineIndex(0, CaretAffinity::Upstream), 0);
    // After a mandatory break: always the next line.
    const TextLayout hard = layout(m_fonts, u"ab\ncd"_s);
    QCOMPARE(hard.lineIndex(3, CaretAffinity::Upstream), 1);
    QCOMPARE(hard.lineIndex(2, CaretAffinity::Upstream), 0);
    QCOMPARE(hard.lineIndex(2, CaretAffinity::Downstream), 0);
}

void TestAtramentTextLayout::rewrapMatches_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<float>("first");
    QTest::addColumn<float>("second");
    QTest::newRow("Polish") << kPolish << 1000.0f << 60.0f;
    QTest::newRow("narrow to wide") << kPolish << 20.0f << 100.0f;
    QTest::newRow("mixed") << u"Zażółć 👍🏽 ⨆ gęślą\njaźń\n"_s << 80.0f << 30.0f;
    QTest::newRow("unwrapped") << polishParagraph(3) << -1.0f << 150.0f;
    QTest::newRow("to unwrapped") << polishParagraph(5) << 150.0f << -1.0f;
    QTest::newRow("empty") << QString() << 10.0f << 20.0f;
}

void TestAtramentTextLayout::rewrapMatches()
{
    QFETCH(QString, text);
    QFETCH(float, first);
    QFETCH(float, second);
    const std::u16string t = u16(text);
    const FontRun runs[] = {run(int(t.size()) / 2, m_fonts.interRegular, 16, 1),
                            run(int(t.size()), m_fonts.literataItalic, 20, 2)};
    auto styleOf = [](float w) { return ParagraphStyle{w < 0 ? std::nullopt : std::optional<float>(w), 0}; };
    const TextLayout a = layoutParagraph(*m_fonts.fonts, t, runs, styleOf(first));
    const TextLayout rewrapped = rewrapParagraph(*m_fonts.fonts, a, runs, styleOf(second));
    const TextLayout direct = layoutParagraph(*m_fonts.fonts, t, runs, styleOf(second));
    checkLayout(rewrapped);
    compareLayouts(rewrapped, direct);
    // Back again.
    compareLayouts(rewrapParagraph(*m_fonts.fonts, rewrapped, runs, styleOf(first)), a);
    // A default-constructed layout is shaped from scratch.
    compareLayouts(rewrapParagraph(*m_fonts.fonts, TextLayout{}, runs, styleOf(second)),
                   layoutParagraph(*m_fonts.fonts, u"", runs, styleOf(second)));
}

void TestAtramentTextLayout::aligned_data()
{
    QTest::addColumn<int>("align");
    QTest::addColumn<float>("wrap"); // < 0: none
    QTest::addColumn<float>("box");  // < 0: none
    QTest::newRow("right, wrapped") << int(TextAlign::Right) << 90.0f << -1.0f;
    QTest::newRow("centre, wrapped") << int(TextAlign::Center) << 90.0f << -1.0f;
    QTest::newRow("right, in a wider box") << int(TextAlign::Right) << 90.0f << 140.0f;
    QTest::newRow("centre, unwrapped in a box") << int(TextAlign::Center) << -1.0f << 400.0f;
    QTest::newRow("right, unwrapped: the widest line") << int(TextAlign::Right) << -1.0f << -1.0f;
    QTest::newRow("right, a box narrower than a line") << int(TextAlign::Right) << -1.0f << 20.0f;
}

void TestAtramentTextLayout::aligned()
{
    // Lines aligned in the paragraph's box: everything the layout gives moves with its line.
    QFETCH(int, align);
    QFETCH(float, wrap);
    QFETCH(float, box);
    using namespace std::string_literals;
    auto near = [](const Rect &a, const Rect &b) {
        return std::abs(a.left() - b.left()) < 1e-3f && std::abs(a.right() - b.right()) < 1e-3f
               && std::abs(a.top() - b.top()) < 1e-3f && std::abs(a.bottom() - b.bottom()) < 1e-3f;
    };
    const std::u16string t = u"Zażółć gęślą jaźń\nŁódź i \u00a0\u00a0 ćma 👍🏽 koniec"s;
    const int object = int(t.find(u'\u00a0'));
    FontRun boxed = run(2, m_fonts.interRegular);
    boxed.inlineBox = InlineBox{18, 12, 3};
    const FontRun runs[] = {run(object, m_fonts.interRegular, 16, 1), boxed,
                            run(int(t.size()) - object - 2, m_fonts.literataItalic, 18, 2)};
    ParagraphStyle plain;
    if (wrap >= 0)
        plain.wrapWidth = wrap;
    ParagraphStyle style = plain;
    style.align = TextAlign(align);
    if (box >= 0)
        style.alignWidth = box;
    const TextLayout left = layoutParagraph(*m_fonts.fonts, t, runs, plain);
    const TextLayout l = layoutParagraph(*m_fonts.fonts, t, runs, style);
    checkLayout(l);
    QCOMPARE(l.size(), left.size());
    QCOMPARE(l.align(), TextAlign(align));
    const float boxWidth = box >= 0 ? box : wrap >= 0 ? wrap : left.size().width;
    QCOMPARE(l.boxWidth(), boxWidth);
    QVERIFY(l.lines().size() >= 2);
    QCOMPARE(l.lines().size(), left.lines().size());
    for (std::size_t i = 0; i < l.lines().size(); ++i) {
        const TextLine &a = l.lines()[i];
        const TextLine &b = left.lines()[i];
        QCOMPARE(a.start, b.start);
        QCOMPARE(a.end, b.end);
        QCOMPARE(a.width, b.width);
        QCOMPARE(b.x, 0.0f);
        const float room = std::max(0.0f, boxWidth - a.width);
        QVERIFY(std::abs(a.x - (TextAlign(align) == TextAlign::Center ? room / 2 : room)) < 1e-3f);
        // Its glyphs, its carets.
        for (std::uint32_t g = a.firstGlyph; g < a.firstGlyph + a.glyphCount; ++g)
            QVERIFY(std::abs(l.glyphs()[g].position.x - (left.glyphs()[g].position.x + a.x)) < 1e-3f);
        QVERIFY(std::abs(l.caretX(a.start) - a.x) < 1e-3f);
        QVERIFY(std::abs(l.caretX(a.end, CaretAffinity::Upstream) - (left.caretX(b.end, CaretAffinity::Upstream) + a.x)) < 1e-3f);
    }
    for (int i = 0; i <= int(t.size()); i = l.nextCaretStop(i)) {
        for (const CaretAffinity affinity : {CaretAffinity::Downstream, CaretAffinity::Upstream}) {
            const float dx = l.lines()[std::size_t(l.lineIndex(i, affinity))].x;
            QVERIFY(std::abs(l.caretX(i, affinity) - (left.caretX(i, affinity) + dx)) < 1e-3f);
            // A hit test where the caret is drawn finds it.
            const Rect r = l.caretRect(i, affinity);
            CaretAffinity got = CaretAffinity::Downstream;
            QVERIFY2(l.offsetAt(r.center(), &got) == i, qPrintable(u"offset %1"_s.arg(i)));
            QCOMPARE(l.lineIndex(i, got), l.lineIndex(i, affinity));
        }
        if (i == int(t.size()))
            break;
    }
    // Selection and glyph rectangles, the inline box.
    const std::vector<Rect> sel = l.selectionRects({0, l.length()});
    const std::vector<Rect> selLeft = left.selectionRects({0, l.length()});
    QCOMPARE(sel.size(), selLeft.size());
    for (std::size_t i = 0; i < sel.size(); ++i)
        QVERIFY(near(sel[i], selLeft[i].translated({l.lines()[i].x, 0})));
    const std::vector<LineRect> glyphs = l.glyphRects({2, l.length() - 2});
    const std::vector<LineRect> glyphsLeft = left.glyphRects({2, l.length() - 2});
    QCOMPARE(glyphs.size(), glyphsLeft.size());
    for (std::size_t i = 0; i < glyphs.size(); ++i)
        QVERIFY(near(glyphs[i].rect, glyphsLeft[i].rect.translated({l.lines()[std::size_t(glyphs[i].line)].x, 0})));
    QCOMPARE(l.inlineBoxes().size(), std::size_t(1));
    const PlacedInlineBox &placed = l.inlineBoxes().front();
    QVERIFY(near(placed.bounds, left.inlineBoxes().front().bounds.translated({l.lines()[std::size_t(placed.line)].x, 0})));
    QVERIFY(std::abs(placed.bounds.left() - l.caretX(object)) < 1e-3f);
    // ↑/↓ by the drawn x: from the last line's start to the line above, at the same place.
    const std::size_t last = l.lines().size() - 1;
    const int start = l.lines()[last].start;
    const float x = l.caretX(start);
    const int above = l.offsetAbove(start, x);
    const TextLine &up = l.lines()[last - 1];
    QVERIFY(above >= up.start && above <= up.end);
    // (A wrapped line's end is the next line's start: ↑ stops before it.)
    const int stop = up.end == up.next ? l.previousCaretStop(up.end) : up.end;
    for (int o = up.start; o <= stop; o = l.nextCaretStop(o)) {
        QVERIFY(std::abs(l.caretX(o) - x) >= std::abs(l.caretX(above) - x) - 1e-3f);
        if (o == stop)
            break;
    }
    // Rewrapped to another alignment and back: as laid out directly.
    compareLayouts(rewrapParagraph(*m_fonts.fonts, l, runs, plain), left);
    compareLayouts(rewrapParagraph(*m_fonts.fonts, left, runs, style), l);
    // The text system keeps them apart.
    TextSystem system(m_fonts.fonts);
    const auto cachedLeft = system.layout(t, runs, plain);
    const auto cached = system.layout(t, runs, style);
    QVERIFY(cachedLeft != cached);
    QCOMPARE(cached->lines().front().x, l.lines().front().x);
    QCOMPARE(system.layout(t, runs, style), cached);
}

void TestAtramentTextLayout::emptyText()
{
    const FontRun r[] = {run(0, m_fonts.interRegular, 20)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, u"", r, {});
    checkLayout(l);
    QCOMPARE(l.lines().size(), std::size_t(1));
    const TextLine &line = l.lines()[0];
    QCOMPARE(line.start, 0);
    QCOMPARE(line.end, 0);
    QCOMPARE(line.next, 0);
    QCOMPARE(line.height, (1984.0f + 494.0f) / 2048 * 20);
    QCOMPARE(l.size(), (Size{0, line.height}));
    QVERIFY(l.glyphs().empty());
    QCOMPARE(l.caretRect(0).height(), line.height);
    QCOMPARE(l.offsetAt({50, 50}), 0);
    QVERIFY(l.selectionRects({0, 5}).empty());
    QCOMPARE(l.offsetAbove(0, 0), -1);
    QCOMPARE(l.offsetBelow(0, 0), -1);
    QCOMPARE(l.minContentWidth(), 0.0f);
    QCOMPARE(l.maxContentWidth(), 0.0f);
    // Without runs: the collection's first face at the default size.
    const TextLayout noRuns = layoutParagraph(*m_fonts.fonts, u"", {}, {});
    QCOMPARE(noRuns.lines()[0].height, (1984.0f + 494.0f) / 2048 * 14);
    // A default TextLayout answers too.
    const TextLayout none;
    QCOMPARE(none.caretX(3), 0.0f);
    QCOMPARE(none.offsetAt({1, 1}), 0);
    QCOMPARE(none.lineIndex(2), 0);
    QVERIFY(none.selectionRects({0, 1}).empty());
    QCOMPARE(none.offsetBelow(0, 0), -1);
}

void TestAtramentTextLayout::severalFonts()
{
    const std::u16string text = u"mały DUŻY tekst";
    const FontRun runs[] = {run(5, m_fonts.interRegular, 12, 7), run(5, m_fonts.interBold, 24, 8),
                            run(5, m_fonts.literataRegular, 16, 9)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, text, runs, {});
    checkLayout(l);
    QCOMPARE(l.lines().size(), std::size_t(1));
    const TextLine &line = l.lines()[0];
    const FontMetrics inter = m_fonts.fonts->metrics(m_fonts.interBold);
    const FontMetrics literata = m_fonts.fonts->metrics(m_fonts.literataRegular);
    const float ascent = std::max(inter.ascent * 24, literata.ascent * 16);
    const float descent = std::max(inter.descent * 24, literata.descent * 16);
    QCOMPARE(line.ascent, ascent);
    QCOMPARE(line.descent, descent);
    const float tallest = std::max((inter.ascent + inter.descent + inter.lineGap) * 24,
                                   (literata.ascent + literata.descent + literata.lineGap) * 16);
    QCOMPARE(line.height, std::max(tallest, ascent + descent));
    // One baseline for all; sizes and style indices from the runs.
    for (const PositionedGlyph &g : l.glyphs()) {
        QCOMPARE(g.position.y, line.baseline);
        const std::uint32_t expected = g.cluster < 5 ? 7 : g.cluster < 10 ? 8 : 9;
        QCOMPARE(g.styleIndex, expected);
        QCOMPARE(g.fontSize, g.cluster < 5 ? 12.0f : g.cluster < 10 ? 24.0f : 16.0f);
        QCOMPARE(g.face, g.cluster < 5    ? m_fonts.interRegular
                         : g.cluster < 10 ? m_fonts.interBold
                                          : m_fonts.literataRegular);
    }
    // Wrapped: each line as tall as its own runs.
    const TextLayout wrapped = layoutParagraph(*m_fonts.fonts, text, runs, ParagraphStyle{l.minContentWidth(), 0});
    checkLayout(wrapped);
    QCOMPARE(wrapped.lines().size(), std::size_t(3));
    QCOMPARE(wrapped.lines()[0].height, (1984.0f + 494.0f) / 2048 * 12);
    QCOMPARE(wrapped.lines()[1].height, (inter.ascent + inter.descent) * 24);
}

void TestAtramentTextLayout::lineHeightOverride()
{
    const std::u16string text = u"Zażółć\ngęślą";
    const FontRun r[] = {run(int(text.size()), m_fonts.interRegular, 16)};
    const TextLayout l = layoutParagraph(*m_fonts.fonts, text, r, ParagraphStyle{std::nullopt, 40});
    checkLayout(l);
    const float ascent = 1984.0f / 2048 * 16;
    const float descent = 494.0f / 2048 * 16;
    for (std::size_t i = 0; i < l.lines().size(); ++i) {
        const TextLine &line = l.lines()[i];
        QCOMPARE(line.height, 40.0f);
        QCOMPARE(line.top, 40.0f * float(i));
        QCOMPARE(line.baseline, line.top + (40 - (ascent + descent)) / 2 + ascent);
    }
    QCOMPARE(l.size().height, 80.0f);
    // Smaller than the font: the half-leading is negative, the line exactly that tall.
    const TextLayout tight = layoutParagraph(*m_fonts.fonts, text, r, ParagraphStyle{std::nullopt, 10});
    QCOMPARE(tight.lines()[0].height, 10.0f);
    QCOMPARE(tight.lines()[0].baseline, (10 - (ascent + descent)) / 2 + ascent);
    QCOMPARE(tight.caretRect(0).height(), 10.0f);
}

void TestAtramentTextLayout::tabs()
{
    const float space = widthOf(m_fonts, u"a b"_s) - widthOf(m_fonts, u"ab"_s);
    const TextLayout l = layout(m_fonts, u"a\tb"_s);
    QVERIFY(std::abs((l.caretX(2) - l.caretX(1)) - 4 * space) < 0.01f);
    // A tab hangs at a wrap like a space and is a break opportunity.
    const TextLayout wrapped = layout(m_fonts, u"abc\tdef"_s, widthOf(m_fonts, u"abc"_s) + 1);
    QCOMPARE(lineTexts(wrapped), (QStringList{u"abc\t"_s, u"def"_s}));
}

void TestAtramentTextLayout::malformed()
{
    const std::u16string surrogates = u"a\xD800"
                                      u"b\xDC00\xD83D";
    const TextLayout lone = layout(m_fonts, qs(surrogates), 5.0f);
    checkLayout(lone);
    QVERIFY(lone.glyphs().size() >= 5);
    const TextLayout replacement = layout(m_fonts, u"a�b"_s);
    checkLayout(replacement);
    QCOMPARE(replacement.glyphs().size(), std::size_t(3));

    // A zero font size: nothing has a size, nothing crashes.
    const TextLayout zero = layout(m_fonts, kPolish, 100.0f, 0.0f);
    checkLayout(zero);
    QCOMPARE(zero.size(), (Size{0, 0}));
    QCOMPARE(zero.offsetAt({5, 5}), 0);
    const TextLayout nan = layout(m_fonts, kPolish, 100.0f, std::nanf(""));
    checkLayout(nan);
    QCOMPARE(nan.size().width, 0.0f);
    const TextLayout negative = layout(m_fonts, kPolish, 100.0f, -12.0f);
    QCOMPARE(negative.size().width, 0.0f);

    const std::u16string text = u16(kPolish);
    // Runs longer than the text: clipped.
    const FontRun longer[] = {run(1000, m_fonts.interRegular), run(5, m_fonts.literataBold)};
    const TextLayout l1 = layoutParagraph(*m_fonts.fonts, text, longer, {});
    checkLayout(l1);
    compareLayouts(l1, layout(m_fonts, kPolish));
    // Runs shorter than the text: the rest in the first face, at the last run's size.
    const FontRun shorter[] = {run(3, m_fonts.literataBold, 20, 4)};
    const TextLayout l2 = layoutParagraph(*m_fonts.fonts, text, shorter, {});
    checkLayout(l2);
    for (const PositionedGlyph &g : l2.glyphs()) {
        QCOMPARE(g.face, g.cluster < 3 ? m_fonts.literataBold : m_fonts.interRegular);
        QCOMPARE(g.fontSize, 20.0f);
        QCOMPARE(g.styleIndex, 4u);
    }
    // kNoFace and faces that do not exist: the first face.
    const FontRun noFace[] = {run(int(text.size()), kNoFace)};
    compareLayouts(layoutParagraph(*m_fonts.fonts, text, noFace, {}), layout(m_fonts, kPolish));
    const FontRun bogus[] = {run(int(text.size()), 4242)};
    compareLayouts(layoutParagraph(*m_fonts.fonts, text, bogus, {}), layout(m_fonts, kPolish));
    // Negative and zero lengths are skipped.
    const FontRun odd[] = {run(-4, m_fonts.literataBold), run(0, m_fonts.monoBold), run(int(text.size()), 0)};
    compareLayouts(layoutParagraph(*m_fonts.fonts, text, odd, {}), layout(m_fonts, kPolish));
    // A run boundary inside a surrogate pair: each half alone, no crash.
    const std::u16string emoji = u"a👍b";
    const FontRun split[] = {run(2, m_fonts.interRegular), run(2, m_fonts.emoji)};
    checkLayout(layoutParagraph(*m_fonts.fonts, emoji, split, {}));
    // Characters no face has: the run's .notdef, one glyph each.
    const TextLayout missing = layout(m_fonts, u"\U0010FFFD͸"_s);
    checkLayout(missing);
    QCOMPARE(missing.glyphs().size(), std::size_t(2));
    QCOMPARE(missing.glyphs()[0].glyph, 0u);
    // Controls draw nothing.
    QVERIFY(layout(m_fonts, u"\x01\x7F"_s).glyphs().empty());
}

void TestAtramentTextLayout::emptyCollection()
{
    FontCollection none;
    const std::u16string text = u16(kPolish);
    const FontRun r[] = {run(int(text.size()), 0)};
    const TextLayout l = layoutParagraph(none, text, r, ParagraphStyle{30.0f, 0});
    checkLayout(l);
    QVERIFY(l.glyphs().empty());
    QCOMPARE(l.size(), (Size{0, 0}));
    QCOMPARE(l.offsetAt({10, 10}), 0);
    const TextLayout empty = layoutParagraph(none, u"", {}, {});
    QCOMPARE(empty.lines().size(), std::size_t(1));
}

void TestAtramentTextLayout::timing()
{
    std::vector<std::u16string> paragraphs;
    std::size_t characters = 0;
    for (int i = 0; i < 1000; ++i) {
        paragraphs.push_back(u16(polishParagraph(i)));
        characters += paragraphs.back().size();
    }
    std::vector<TextLayout> layouts;
    layouts.reserve(paragraphs.size());
    QElapsedTimer timer;
    timer.start();
    for (const std::u16string &p : paragraphs) {
        const FontRun r[] = {run(int(p.size()), m_fonts.interRegular, 15)};
        layouts.push_back(layoutParagraph(*m_fonts.fonts, p, r, ParagraphStyle{320.0f, 0}));
    }
    const qint64 layoutNs = timer.nsecsElapsed();
    timer.restart();
    std::size_t lines = 0;
    for (std::size_t i = 0; i < layouts.size(); ++i) {
        const FontRun r[] = {run(int(paragraphs[i].size()), m_fonts.interRegular, 15)};
        lines += rewrapParagraph(*m_fonts.fonts, layouts[i], r, ParagraphStyle{250.0f, 0}).lines().size();
    }
    const qint64 rewrapNs = timer.nsecsElapsed();
    qDebug("1000 paragraphs, %zu characters: layout (shaping + wrapping) %.1f ms, rewrap %.1f ms (%zu lines)",
           characters, double(layoutNs) / 1e6, double(rewrapNs) / 1e6, lines);
    for (const TextLayout &l : layouts)
        QVERIFY(l.lines().size() >= 4);
}

QTEST_GUILESS_MAIN(TestAtramentTextLayout)
#include "tst_atrament_text_layout.moc"
