// Atrament's layout: the flexbox algorithm against hand-computed CSS results (what a browser gives
// for the same HTML and CSS with `box-sizing: border-box`), measured leaves, caching, malformed
// input and speed.
#include "layout/layout.h"

#include "testtiming.h"

#include <QtTest>

#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace brulion::atrament;

namespace {

// A style in one expression: S().row().w(300).gap(8).
struct S {
    LayoutStyle s;

    S &row() { return dir(FlexDirection::Row); }
    S &col() { return dir(FlexDirection::Column); }
    S &dir(FlexDirection d)
    {
        s.flexDirection = d;
        return *this;
    }
    S &wrap()
    {
        s.flexWrap = FlexWrap::Wrap;
        return *this;
    }
    S &w(float v) { return w(Length::px(v)); }
    S &w(Length l)
    {
        s.width = l;
        return *this;
    }
    S &h(float v) { return h(Length::px(v)); }
    S &h(Length l)
    {
        s.height = l;
        return *this;
    }
    S &size(float width, float height) { return w(width).h(height); }
    S &minW(Length l)
    {
        s.minWidth = l;
        return *this;
    }
    S &maxW(Length l)
    {
        s.maxWidth = l;
        return *this;
    }
    S &minH(Length l)
    {
        s.minHeight = l;
        return *this;
    }
    S &maxH(Length l)
    {
        s.maxHeight = l;
        return *this;
    }
    S &grow(float v)
    {
        s.flexGrow = v;
        return *this;
    }
    S &shrink(float v)
    {
        s.flexShrink = v;
        return *this;
    }
    S &basis(Length l)
    {
        s.flexBasis = l;
        return *this;
    }
    S &basis(float v) { return basis(Length::px(v)); }
    S &justify(JustifyContent j)
    {
        s.justifyContent = j;
        return *this;
    }
    S &items(AlignItems a)
    {
        s.alignItems = a;
        return *this;
    }
    S &self(AlignItems a)
    {
        s.alignSelf = a;
        return *this;
    }
    S &content(AlignContent a)
    {
        s.alignContent = a;
        return *this;
    }
    S &margin(LengthEdges m)
    {
        s.margin = m;
        return *this;
    }
    S &margin(float top, float right, float bottom, float left)
    {
        return margin({Length::px(top), Length::px(right), Length::px(bottom), Length::px(left)});
    }
    S &pad(float v) { return pad(Edges::all(v)); }
    S &pad(Edges e)
    {
        s.padding = e;
        return *this;
    }
    S &border(float v)
    {
        s.border = Edges::all(v);
        return *this;
    }
    S &gap(float rowGap, float columnGap)
    {
        s.rowGap = rowGap;
        s.columnGap = columnGap;
        return *this;
    }
    S &absolute(LengthEdges inset)
    {
        s.position = PositionType::Absolute;
        s.inset = inset;
        return *this;
    }
    S &absolute() { return absolute(LengthEdges::autoEdges()); }
    S &overflow(Overflow o)
    {
        s.overflowX = o;
        s.overflowY = o;
        return *this;
    }
    S &none()
    {
        s.display = Display::None;
        return *this;
    }
    S &ratio(float r)
    {
        s.aspectRatio = r;
        return *this;
    }
    operator const LayoutStyle &() const { return s; }
};

constexpr Length px(float v)
{
    return Length::px(v);
}

constexpr Length pct(float v)
{
    return Length::percent(v);
}

constexpr Length autoL = Length::autoLength();

NodeId node(LayoutEngine &e, const LayoutStyle &style, std::initializer_list<NodeId> children)
{
    return e.newNode(style, std::span<const NodeId>(children.begin(), children.size()));
}

struct MeasureCall {
    std::optional<float> knownWidth;
    std::optional<float> knownHeight;
    AvailableSpace availableWidth;
    AvailableSpace availableHeight;
};

// A fake paragraph as CSS lays out a block of text: words of the given widths with a space of 10
// between them, 20 px a line; as wide as its longest word for min-content, its one line for
// max-content, and in a definite space fit-content (min(max-content, max(min-content, space))),
// wrapped greedily at that width. In the browser: inline-blocks in a block with font-size 0 and
// word-spacing 10px.
struct Paragraph {
    std::vector<float> words;
    float space = 10;
    float lineHeight = 20;
    std::vector<MeasureCall> *log = nullptr;

    Size operator()(std::optional<float> knownWidth, std::optional<float> knownHeight, AvailableSpace availableWidth,
                    AvailableSpace availableHeight) const
    {
        if (log)
            log->push_back({knownWidth, knownHeight, availableWidth, availableHeight});
        float minContent = 0;
        float maxContent = 0;
        for (const float word : words) {
            minContent = std::max(minContent, word);
            maxContent += (maxContent > 0 ? space : 0.0f) + word;
        }
        float width = maxContent;
        if (knownWidth)
            width = *knownWidth;
        else if (availableWidth.kind == AvailableSpace::Kind::Definite)
            width = std::min(maxContent, std::max(minContent, availableWidth.value));
        else if (availableWidth.kind == AvailableSpace::Kind::MinContent)
            width = minContent;
        float line = -1;
        int lines = 0;
        for (const float word : words) {
            if (line >= 0 && line + space + word <= width + 1e-3f) {
                line += space + word;
            } else {
                line = word;
                ++lines;
            }
        }
        return {width, knownHeight ? *knownHeight : float(lines) * lineHeight};
    }
};

// `count` words of `width` px.
MeasureFunction words(int count, float width, std::vector<MeasureCall> *log = nullptr)
{
    Paragraph p;
    p.words.assign(std::size_t(count), width);
    p.log = log;
    return p;
}

MeasureFunction words(std::vector<float> widths)
{
    Paragraph p;
    p.words = std::move(widths);
    return p;
}

// A scene in the notation the browser harness reads too: a node is `(css | children…)`, a leaf
// `(css)`, a text `[word widths | css]`; css is the subset of CSS layout.h supports.
class SceneParser {
public:
    SceneParser(LayoutEngine &engine, std::string spec) : e(engine), text(std::move(spec)) { }

    NodeId parse()
    {
        const NodeId root = node();
        skipSpaces();
        if (at < text.size())
            fail("trailing text");
        return root;
    }

    std::vector<NodeId> preorder; // the nodes in the order the browser lists them
    std::string error;

private:
    void fail(const std::string &why)
    {
        if (error.empty())
            error = why + " at " + std::to_string(at);
    }
    void skipSpaces()
    {
        while (at < text.size() && text[at] == ' ')
            ++at;
    }
    std::string until(const char *stops)
    {
        std::string out;
        while (at < text.size() && !std::strchr(stops, text[at]))
            out += text[at++];
        return out;
    }

    NodeId node()
    {
        skipSpaces();
        if (at >= text.size()) {
            fail("unexpected end");
            return kNoNode;
        }
        const char open = text[at++];
        const std::size_t slot = preorder.size();
        preorder.push_back(kNoNode);
        NodeId id = kNoNode;
        if (open == '[') {
            std::vector<float> widths;
            std::istringstream in(until("|]"));
            for (float w; in >> w;)
                widths.push_back(w);
            ++at; // |
            const LayoutStyle s = style(until("]"));
            ++at;
            id = e.newLeaf(s, words(widths));
        } else if (open == '(') {
            const LayoutStyle s = style(until("|)"));
            std::vector<NodeId> children;
            if (at < text.size() && text[at] == '|') {
                ++at;
                for (skipSpaces(); at < text.size() && text[at] != ')'; skipSpaces())
                    children.push_back(node());
            }
            ++at;
            id = e.newNode(s, children);
        } else {
            fail("expected ( or [");
        }
        preorder[slot] = id;
        return id;
    }

    static Length length(const std::string &v)
    {
        if (v == "auto")
            return Length::autoLength();
        const float number = std::stof(v);
        return v.back() == '%' ? Length::percent(number) : Length::px(number);
    }
    static std::vector<std::string> split(const std::string &v)
    {
        std::vector<std::string> parts;
        std::istringstream in(v);
        for (std::string part; in >> part;)
            parts.push_back(part);
        return parts;
    }
    // CSS's one to four values: top, right, bottom, left.
    static LengthEdges edges(const std::string &v)
    {
        std::vector<Length> l;
        for (const std::string &part : split(v))
            l.push_back(length(part));
        while (l.size() < 4)
            l.push_back(l.size() == 3 ? l[1] : l.size() == 2 ? l[0] : l[0]);
        return {l[0], l[1], l[2], l[3]};
    }
    static Edges pxEdges(const std::string &v)
    {
        const LengthEdges l = edges(v);
        return {l.top.value, l.right.value, l.bottom.value, l.left.value};
    }
    static AlignItems align(const std::string &v)
    {
        if (v == "flex-start" || v == "start")
            return AlignItems::FlexStart;
        if (v == "flex-end" || v == "end")
            return AlignItems::FlexEnd;
        if (v == "center")
            return AlignItems::Center;
        if (v == "baseline")
            return AlignItems::Baseline;
        if (v == "auto")
            return AlignItems::Auto;
        return AlignItems::Stretch;
    }

    LayoutStyle style(const std::string &css)
    {
        LayoutStyle s;
        std::istringstream in(css);
        for (std::string declaration; std::getline(in, declaration, ';');) {
            const auto colon = declaration.find(':');
            if (colon == std::string::npos) {
                if (declaration.find_first_not_of(' ') != std::string::npos)
                    fail("bad declaration " + declaration);
                continue;
            }
            const std::string name = split(declaration.substr(0, colon)).at(0);
            std::string v = declaration.substr(colon + 1);
            v.erase(0, v.find_first_not_of(' '));
            v.erase(v.find_last_not_of(' ') + 1);
            if (name == "display")
                s.display = v == "none" ? Display::None : Display::Flex;
            else if (name == "position")
                s.position = v == "absolute" ? PositionType::Absolute : PositionType::Relative;
            else if (name == "flex-direction")
                s.flexDirection = v == "column"           ? FlexDirection::Column
                                  : v == "row-reverse"    ? FlexDirection::RowReverse
                                  : v == "column-reverse" ? FlexDirection::ColumnReverse
                                                          : FlexDirection::Row;
            else if (name == "flex-wrap")
                s.flexWrap = v == "wrap" ? FlexWrap::Wrap : FlexWrap::NoWrap;
            else if (name == "justify-content")
                s.justifyContent = v == "flex-end"        ? JustifyContent::FlexEnd
                                   : v == "center"        ? JustifyContent::Center
                                   : v == "space-between" ? JustifyContent::SpaceBetween
                                   : v == "space-around"  ? JustifyContent::SpaceAround
                                   : v == "space-evenly"  ? JustifyContent::SpaceEvenly
                                                          : JustifyContent::FlexStart;
            else if (name == "align-items")
                s.alignItems = align(v);
            else if (name == "align-self")
                s.alignSelf = align(v);
            else if (name == "align-content")
                s.alignContent = v == "flex-start"      ? AlignContent::FlexStart
                                 : v == "flex-end"      ? AlignContent::FlexEnd
                                 : v == "center"        ? AlignContent::Center
                                 : v == "space-between" ? AlignContent::SpaceBetween
                                 : v == "space-around"  ? AlignContent::SpaceAround
                                                        : AlignContent::Stretch;
            else if (name == "flex-grow")
                s.flexGrow = std::stof(v);
            else if (name == "flex-shrink")
                s.flexShrink = std::stof(v);
            else if (name == "flex-basis")
                s.flexBasis = length(v);
            else if (name == "width")
                s.width = length(v);
            else if (name == "height")
                s.height = length(v);
            else if (name == "min-width")
                s.minWidth = length(v);
            else if (name == "min-height")
                s.minHeight = length(v);
            else if (name == "max-width")
                s.maxWidth = length(v);
            else if (name == "max-height")
                s.maxHeight = length(v);
            else if (name == "aspect-ratio")
                s.aspectRatio = std::stof(v);
            else if (name == "margin")
                s.margin = edges(v);
            else if (name == "margin-top")
                s.margin.top = length(v);
            else if (name == "margin-right")
                s.margin.right = length(v);
            else if (name == "margin-bottom")
                s.margin.bottom = length(v);
            else if (name == "margin-left")
                s.margin.left = length(v);
            else if (name == "padding")
                s.padding = pxEdges(v);
            else if (name == "padding-top")
                s.padding.top = length(v).value;
            else if (name == "padding-right")
                s.padding.right = length(v).value;
            else if (name == "padding-bottom")
                s.padding.bottom = length(v).value;
            else if (name == "padding-left")
                s.padding.left = length(v).value;
            else if (name == "border-width")
                s.border = pxEdges(v);
            else if (name == "row-gap")
                s.rowGap = length(v).value;
            else if (name == "column-gap")
                s.columnGap = length(v).value;
            else if (name == "top")
                s.inset.top = length(v);
            else if (name == "right")
                s.inset.right = length(v);
            else if (name == "bottom")
                s.inset.bottom = length(v);
            else if (name == "left")
                s.inset.left = length(v);
            else if (name == "overflow")
                s.overflowX = s.overflowY = v == "visible"  ? Overflow::Visible
                                            : v == "hidden" ? Overflow::Hidden
                                                            : Overflow::Scroll;
            else
                fail("unknown property " + name);
        }
        return s;
    }

    LayoutEngine &e;
    std::string text;
    std::size_t at = 0;
};

// Within 0.01 px (0.02 for the browser, which rounds to 1/64 px).
QString mismatch(Rect actual, Rect expected, float tolerance = 0.01f)
{
    const auto same = [tolerance](float a, float b) { return std::abs(a - b) <= tolerance; };
    if (same(actual.left(), expected.left()) && same(actual.top(), expected.top())
        && same(actual.width(), expected.width()) && same(actual.height(), expected.height()))
        return {};
    return QStringLiteral("got (%1, %2, %3 × %4), expected (%5, %6, %7 × %8)")
                                  .arg(actual.left())
                                  .arg(actual.top())
                                  .arg(actual.width())
                                  .arg(actual.height())
                                  .arg(expected.left())
                                  .arg(expected.top())
                                  .arg(expected.width())
                                  .arg(expected.height());
}

bool finite(Rect r)
{
    return std::isfinite(r.left()) && std::isfinite(r.top()) && std::isfinite(r.width()) && std::isfinite(r.height())
           && r.width() >= 0 && r.height() >= 0;
}

} // namespace

// The border box of `n` relative to its parent.
#define CHECK_RECT(n, x, y, width, height)                                                                             \
    do {                                                                                                               \
        const QString m_ = mismatch(e.layout(n).rect, Rect::fromXYWH(x, y, width, height));                            \
        QVERIFY2(m_.isEmpty(), qPrintable(QStringLiteral(#n ": %1").arg(m_)));                                          \
    } while (false)

class TestAtramentLayout : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void emptyAndInvalid();
    void fixedSizes();
    void grow_data();
    void grow();
    void shrink_data();
    void shrink();
    void automaticMinimumOfText();
    void scrollContainerDoesNotPushParent();
    void columnHeightFollowsWrappedText();
    void iconAndWrappingText();
    void nestedTenDeep();
    void containerSizedByContent();
    void fitContentRowWraps();
    void justifyContent_data();
    void justifyContent();
    void autoMargins();
    void alignItems_data();
    void alignItems();
    void reverseDirections();
    void gaps();
    void wrapAndAlignContent_data();
    void wrapAndAlignContent();
    void alignContentOverflow();
    void columnWrap();
    void percentages();
    void aspectRatio();
    void paddingAndBorder();
    void displayNone();
    void absoluteChildren();
    void measureArguments();
    void contentSize();
    void absoluteCoordinates();
    void malformedInput();
    void cachePerCompute();
    void clearAndRebuild();
    void browserScenes_data();
    void browserScenes();
    void tenThousandNodes();
};

void TestAtramentLayout::emptyAndInvalid()
{
    LayoutEngine e;
    e.compute(0, Size{100, 100}); // no nodes at all
    QCOMPARE(e.nodeCount(), std::size_t(0));
    QCOMPARE(e.layout(7).rect, Rect{});
    QCOMPARE(e.absolute(7), Rect{});
    QCOMPARE(e.parent(7), kNoNode);
    QCOMPARE(e.measureCalls(), std::size_t(0));

    // An empty auto-sized container takes no space; at 100 % it fills the window.
    const NodeId empty = e.newNode({}, {});
    const NodeId full = e.newNode(S().w(pct(100)).h(pct(100)), {});
    e.compute(empty, Size{800, 600});
    CHECK_RECT(empty, 0, 0, 0, 0);
    e.compute(full, Size{800, 600});
    CHECK_RECT(full, 0, 0, 800, 600);

    // Children that do not exist yet, or have a parent already, are left out.
    const NodeId a = e.newNode(S().size(10, 10), {});
    const NodeId first = node(e, S().size(100, 100), {a, 999, a});
    const NodeId second = node(e, S().size(100, 100), {a});
    QCOMPARE(e.parent(a), first);
    e.compute(second, Size{800, 600});
    CHECK_RECT(second, 0, 0, 100, 100);
}

void TestAtramentLayout::fixedSizes()
{
    LayoutEngine e;
    const NodeId a = e.newLeaf(S().size(50, 20));
    const NodeId b = e.newLeaf(S().size(70, 30));
    const NodeId row = node(e, S().row().size(400, 100), {a, b});
    e.compute(row, Size{800, 600});
    CHECK_RECT(row, 0, 0, 400, 100);
    CHECK_RECT(a, 0, 0, 50, 20);
    CHECK_RECT(b, 50, 0, 70, 30);

    // A column stretches items without a width to its own.
    e.clear();
    const NodeId c = e.newLeaf(S().h(20));
    const NodeId d = e.newLeaf(S().h(30));
    const NodeId column = node(e, S().col().size(400, 300), {c, d});
    QCOMPARE(column, NodeId(2)); // ids start again from 0
    e.compute(column, Size{800, 600});
    CHECK_RECT(c, 0, 0, 400, 20);
    CHECK_RECT(d, 0, 20, 400, 30);
    QCOMPARE(e.parent(c), column);
    QCOMPARE(e.parent(column), kNoNode);
}

// Items: basis (px), grow, min and max (-1: auto); a row 600 wide.
void TestAtramentLayout::grow_data()
{
    QTest::addColumn<QList<float>>("bases");
    QTest::addColumn<QList<float>>("grows");
    QTest::addColumn<QList<float>>("mins");
    QTest::addColumn<QList<float>>("maxs");
    QTest::addColumn<float>("gap");
    QTest::addColumn<QList<float>>("widths");
    QTest::addColumn<QList<float>>("xs");

    QTest::newRow("equal from zero") << QList<float>{0, 0, 0} << QList<float>{1, 1, 1} << QList<float>{-1, -1, -1}
                                     << QList<float>{-1, -1, -1} << 0.0f << QList<float>{200, 200, 200}
                                     << QList<float>{0, 200, 400};
    QTest::newRow("weighted") << QList<float>{100, 100, 100} << QList<float>{1, 2, 3} << QList<float>{-1, -1, -1}
                              << QList<float>{-1, -1, -1} << 0.0f << QList<float>{150, 200, 250}
                              << QList<float>{0, 150, 350};
    // 200 each first; the first exceeds its max and freezes at 100, the others share the rest.
    QTest::newRow("max freezes") << QList<float>{0, 0, 0} << QList<float>{1, 1, 1} << QList<float>{-1, -1, -1}
                                 << QList<float>{100, -1, -1} << 0.0f << QList<float>{100, 250, 250}
                                 << QList<float>{0, 100, 350};
    // 300 each first; the first is below its min of 400 and freezes there.
    QTest::newRow("min freezes") << QList<float>{0, 0} << QList<float>{1, 1} << QList<float>{400, -1}
                                 << QList<float>{-1, -1} << 0.0f << QList<float>{400, 200} << QList<float>{0, 400};
    // Factors summing to 0.5 take half of the free space (9.7.4b).
    QTest::newRow("factors below one") << QList<float>{100, 100} << QList<float>{0.25f, 0.25f} << QList<float>{-1, -1}
                                       << QList<float>{-1, -1} << 0.0f << QList<float>{200, 200}
                                       << QList<float>{0, 200};
    QTest::newRow("gaps") << QList<float>{0, 0, 0} << QList<float>{1, 1, 1} << QList<float>{-1, -1, -1}
                          << QList<float>{-1, -1, -1} << 30.0f << QList<float>{180, 180, 180}
                          << QList<float>{0, 210, 420};
    QTest::newRow("no grow") << QList<float>{100, 50} << QList<float>{0, 0} << QList<float>{-1, -1}
                             << QList<float>{-1, -1} << 0.0f << QList<float>{100, 50} << QList<float>{0, 100};
}

void TestAtramentLayout::grow()
{
    QFETCH(QList<float>, bases);
    QFETCH(QList<float>, grows);
    QFETCH(QList<float>, mins);
    QFETCH(QList<float>, maxs);
    QFETCH(float, gap);
    QFETCH(QList<float>, widths);
    QFETCH(QList<float>, xs);

    LayoutEngine e;
    std::vector<NodeId> items;
    for (qsizetype i = 0; i < bases.size(); ++i) {
        S s;
        s.basis(bases[i]).grow(grows[i]).h(20);
        if (mins[i] >= 0)
            s.minW(px(mins[i]));
        if (maxs[i] >= 0)
            s.maxW(px(maxs[i]));
        items.push_back(e.newLeaf(s));
    }
    const NodeId row = e.newNode(S().row().size(600, 20).gap(0, gap), items);
    e.compute(row, Size{800, 600});
    for (qsizetype i = 0; i < bases.size(); ++i)
        CHECK_RECT(items[std::size_t(i)], xs[i], 0, widths[i], 20);
}

// Items: basis, shrink, min (-1: auto), horizontal padding; a row 300 wide (or `width`).
void TestAtramentLayout::shrink_data()
{
    QTest::addColumn<float>("width");
    QTest::addColumn<QList<float>>("bases");
    QTest::addColumn<QList<float>>("shrinks");
    QTest::addColumn<QList<float>>("mins");
    QTest::addColumn<QList<float>>("paddings");
    QTest::addColumn<QList<float>>("widths");

    // 300 too much, taken in proportion to the base sizes: 100 and 200.
    QTest::newRow("scaled by base") << 300.0f << QList<float>{200, 400} << QList<float>{1, 1} << QList<float>{-1, -1}
                                    << QList<float>{0, 0} << QList<float>{100, 200};
    // Scaled shrink factors 400 and 200 for 100 too much.
    QTest::newRow("factors") << 300.0f << QList<float>{200, 200} << QList<float>{2, 1} << QList<float>{-1, -1}
                             << QList<float>{0, 0} << QList<float>{133.333f, 166.667f};
    // 100 each would put the first below its min of 150: it freezes there, the second takes the rest.
    QTest::newRow("min freezes") << 200.0f << QList<float>{200, 200} << QList<float>{1, 1} << QList<float>{150, -1}
                                 << QList<float>{0, 0} << QList<float>{150, 50};
    QTest::newRow("shrink 0") << 300.0f << QList<float>{200, 200} << QList<float>{0, 1} << QList<float>{-1, -1}
                              << QList<float>{0, 0} << QList<float>{200, 100};
    // The shrink factor scales the inner (content-box) base size: 160 and 400 of 560.
    QTest::newRow("inner base size") << 300.0f << QList<float>{200, 400} << QList<float>{1, 1} << QList<float>{-1, -1}
                                     << QList<float>{20, 0} << QList<float>{114.286f, 185.714f};
    QTest::newRow("all frozen overflow") << 300.0f << QList<float>{200, 200} << QList<float>{0, 0}
                                         << QList<float>{-1, -1} << QList<float>{0, 0} << QList<float>{200, 200};
    // Padding is a floor: a border box never gets smaller than its padding and border (32.5 →
    // 40), nor a content box below 0 (-12.5 → 0); both freeze.
    QTest::newRow("padding floor") << 20.0f << QList<float>{100, 100} << QList<float>{1, 1} << QList<float>{-1, -1}
                                   << QList<float>{20, 0} << QList<float>{40, 0};
}

void TestAtramentLayout::shrink()
{
    QFETCH(float, width);
    QFETCH(QList<float>, bases);
    QFETCH(QList<float>, shrinks);
    QFETCH(QList<float>, mins);
    QFETCH(QList<float>, paddings);
    QFETCH(QList<float>, widths);

    LayoutEngine e;
    std::vector<NodeId> items;
    for (qsizetype i = 0; i < bases.size(); ++i) {
        S s;
        s.basis(bases[i]).shrink(shrinks[i]).h(20).pad(Edges::symmetric(0, paddings[i]));
        if (mins[i] >= 0)
            s.minW(px(mins[i]));
        items.push_back(e.newLeaf(s));
    }
    const NodeId row = e.newNode(S().row().size(width, 20), items);
    e.compute(row, Size{800, 600});
    float x = 0;
    for (qsizetype i = 0; i < bases.size(); ++i) {
        CHECK_RECT(items[std::size_t(i)], x, 0, widths[i], 20);
        x += widths[i];
    }
}

// CSS 4.5: a flex item's automatic minimum size is its min-content size — a text shrinks only to
// its longest word — and 0 for a scroll container.
void TestAtramentLayout::automaticMinimumOfText()
{
    LayoutEngine e;
    // Words 60, 40, 80: max-content 200, min-content 80.
    NodeId text = e.newLeaf({}, words({60, 40, 80}));
    NodeId row = node(e, S().row().w(50), {text});
    e.compute(row, Size{800, 600});
    CHECK_RECT(text, 0, 0, 80, 60); // one word a line at 80
    CHECK_RECT(row, 0, 0, 50, 60);

    // Overflow hidden: no automatic minimum, it shrinks to the row (each word on a line of its own).
    e.clear();
    text = e.newLeaf(S().overflow(Overflow::Hidden), words({60, 40, 80}));
    row = node(e, S().row().w(50), {text});
    e.compute(row, Size{800, 600});
    CHECK_RECT(text, 0, 0, 50, 60);

    // An explicit min-width: 0 too.
    e.clear();
    text = e.newLeaf(S().minW(px(0)), words({60, 40, 80}));
    row = node(e, S().row().w(50), {text});
    e.compute(row, Size{800, 600});
    CHECK_RECT(text, 0, 0, 50, 60);

    // Beside an icon that does not shrink: 150 left, "60 40" and "80" on two lines.
    e.clear();
    const NodeId icon = e.newLeaf(S().size(50, 20).shrink(0));
    text = e.newLeaf({}, words({60, 40, 80}));
    row = node(e, S().row().w(200), {icon, text});
    e.compute(row, Size{800, 600});
    CHECK_RECT(icon, 0, 0, 50, 20);
    CHECK_RECT(text, 50, 0, 150, 40);
    CHECK_RECT(row, 0, 0, 200, 40);
}

void TestAtramentLayout::scrollContainerDoesNotPushParent()
{
    // A header and a scrolled list of 20 rows of 50 in a column 300 high: the list takes what
    // the header leaves (its automatic minimum is 0), its content stays 1000 high.
    const auto build = [](LayoutEngine &e, Overflow overflow, const LayoutStyle &rootStyle, NodeId &header,
                          NodeId &list, std::vector<NodeId> &rows) {
        header = e.newLeaf(S().h(50).shrink(0));
        rows.clear();
        for (int i = 0; i < 20; ++i)
            rows.push_back(e.newLeaf(S().h(50).shrink(0)));
        list = e.newNode(S().col().grow(1).overflow(overflow), rows);
        return node(e, rootStyle, {header, list});
    };
    LayoutEngine e;
    NodeId header = kNoNode;
    NodeId list = kNoNode;
    std::vector<NodeId> rows;
    NodeId root = build(e, Overflow::Scroll, S().col().size(200, 300), header, list, rows);
    e.compute(root, Size{800, 600});
    CHECK_RECT(header, 0, 0, 200, 50);
    CHECK_RECT(list, 0, 50, 200, 250);
    CHECK_RECT(rows.back(), 0, 950, 200, 50);
    QCOMPARE(e.layout(list).contentSize, (Size{200, 1000}));

    // The same under a max-height instead of a height.
    e.clear();
    root = build(e, Overflow::Hidden, S().col().w(200).maxH(px(300)), header, list, rows);
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 200, 300);
    CHECK_RECT(list, 0, 50, 200, 250);

    // Without overflow the list's automatic minimum is its content: it overflows the column.
    e.clear();
    root = build(e, Overflow::Visible, S().col().size(200, 300), header, list, rows);
    e.compute(root, Size{800, 600});
    CHECK_RECT(list, 0, 50, 200, 1000);
}

void TestAtramentLayout::columnHeightFollowsWrappedText()
{
    // A column 200 wide with padding 10: a text of ten 50 px words stretched to 180 wraps three
    // words a line (170), four lines; a second text of three words fits one line.
    LayoutEngine e;
    const NodeId first = e.newLeaf({}, words(10, 50));
    const NodeId second = e.newLeaf(S().margin(5, 0, 0, 0), words(3, 50));
    const NodeId column = node(e, S().col().w(200).pad(10), {first, second});
    e.compute(column, Size{800, 600});
    CHECK_RECT(first, 10, 10, 180, 80);
    CHECK_RECT(second, 10, 95, 180, 20);
    CHECK_RECT(column, 0, 0, 200, 125);
    QVERIFY2(e.measureCalls() <= 4, qPrintable(QString::number(e.measureCalls())));
}

void TestAtramentLayout::iconAndWrappingText()
{
    // A row 300 wide: an icon of 24 that does not shrink, a gap of 8, a text of ten 40 px words
    // (max-content 490) that takes the rest, 268: five words a line, two lines; items centred.
    LayoutEngine e;
    const NodeId icon = e.newLeaf(S().size(24, 24).shrink(0));
    const NodeId text = e.newLeaf(S().grow(1), words(10, 40));
    const NodeId row = node(e, S().row().w(300).gap(0, 8).items(AlignItems::Center), {icon, text});
    e.compute(row, Size{800, 600});
    CHECK_RECT(row, 0, 0, 300, 40);
    CHECK_RECT(icon, 0, 8, 24, 24);
    CHECK_RECT(text, 32, 0, 268, 40);
    QVERIFY2(e.measureCalls() <= 3, qPrintable(QString::number(e.measureCalls())));

    // The same text in a narrower row: 68 wide, one word a line.
    e.clear();
    const NodeId icon2 = e.newLeaf(S().size(24, 24).shrink(0));
    const NodeId text2 = e.newLeaf(S().grow(1), words(10, 40));
    const NodeId row2 = node(e, S().row().w(100).gap(0, 8), {icon2, text2});
    e.compute(row2, Size{800, 600});
    CHECK_RECT(text2, 32, 0, 68, 200);
    CHECK_RECT(icon2, 0, 0, 24, 24);
}

void TestAtramentLayout::nestedTenDeep()
{
    // Rows and columns nested ten deep, each with padding 1, around a text of five 30 px words
    // (max-content 190). Each row sizes its column child to the content's width, each column
    // stretches its row child; heights grow by 2 a level from the text's 20 up to the first row
    // under the root, whose column child is stretched to the root's 298.
    LayoutEngine e;
    const NodeId text = e.newLeaf({}, words(5, 30));
    NodeId inner = text;
    std::vector<NodeId> levels; // innermost first
    for (int level = 9; level >= 1; --level) {
        inner = node(e, S().dir(level % 2 ? FlexDirection::Column : FlexDirection::Row).pad(1), {inner});
        levels.push_back(inner);
    }
    const NodeId root = node(e, S().row().size(400, 300).pad(1), {inner});
    e.compute(root, Size{800, 600});

    QCOMPARE(e.absolute(text), Rect::fromXYWH(10, 10, 190, 20));
    // levels[0] is L9 (a column in a row), levels[8] is L1 (a column under the root row).
    const float widths[] = {192, 194, 196, 198, 200, 202, 204, 206, 208};
    const float heights[] = {22, 24, 26, 28, 30, 32, 34, 36, 298};
    for (std::size_t i = 0; i < levels.size(); ++i)
        CHECK_RECT(levels[i], 1, 1, widths[i], heights[i]);
    const std::size_t calls = e.measureCalls();
    qInfo("measure calls, ten levels deep: %zu", calls);
    QVERIFY2(calls <= 12, qPrintable(QString::number(calls)));

    // Ten more levels: still a handful of calls, not twice as many per level.
    e.clear();
    const NodeId text2 = e.newLeaf({}, words(5, 30));
    inner = text2;
    for (int level = 19; level >= 1; --level)
        inner = node(e, S().dir(level % 2 ? FlexDirection::Column : FlexDirection::Row).pad(1), {inner});
    const NodeId root2 = node(e, S().row().size(400, 300).pad(1), {inner});
    e.compute(root2, Size{800, 600});
    QCOMPARE(e.absolute(text2), Rect::fromXYWH(20, 20, 190, 20));
    qInfo("measure calls, twenty levels deep: %zu", e.measureCalls());
    QVERIFY2(e.measureCalls() <= 12, qPrintable(QString::number(e.measureCalls())));
}

void TestAtramentLayout::containerSizedByContent()
{
    // An auto-sized row: as wide as its items (fit-content of the window), as high as the highest.
    LayoutEngine e;
    NodeId a = e.newLeaf(S().size(50, 20));
    NodeId b = e.newLeaf(S().size(70, 30));
    NodeId root = node(e, S().row(), {a, b});
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 120, 30);
    CHECK_RECT(b, 50, 0, 70, 30);

    // Its min-width and max-width: at most 100, the empty items shrink by their base sizes.
    e.clear();
    a = e.newLeaf(S().size(50, 20));
    b = e.newLeaf(S().size(70, 30));
    root = node(e, S().row().minW(px(200)), {a, b});
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 200, 30);
    e.clear();
    a = e.newLeaf(S().size(50, 20));
    b = e.newLeaf(S().size(70, 30));
    root = node(e, S().row().maxW(px(100)).minH(px(50)), {a, b});
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 100, 50);
    CHECK_RECT(a, 0, 0, 41.667f, 20);
    CHECK_RECT(b, 41.667f, 0, 58.333f, 30);

    // A column: the sum of the heights, the widest width.
    e.clear();
    a = e.newLeaf(S().size(50, 20));
    b = e.newLeaf(S().size(70, 30));
    root = node(e, S().col(), {a, b});
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 70, 50);

    // A column around a text: the text's width, at most the window's.
    e.clear();
    NodeId text = e.newLeaf({}, words(3, 50));
    root = node(e, S().col().pad(5), {text});
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 180, 30);
    CHECK_RECT(text, 5, 5, 170, 20);
    e.clear();
    text = e.newLeaf({}, words(10, 50));
    root = node(e, S().col(), {text});
    e.compute(root, Size{300, 600});
    CHECK_RECT(root, 0, 0, 300, 40); // fit-content: the window's width, five words a line
}

void TestAtramentLayout::fitContentRowWraps()
{
    // A row in a column 300 wide that does not stretch it: fit-content, so its long text wraps at
    // 300 (five 50 px words a line); a short one keeps its width and can be centred.
    LayoutEngine e;
    NodeId text = e.newLeaf({}, words(10, 50));
    NodeId row = node(e, S().row().self(AlignItems::FlexStart), {text});
    NodeId column = node(e, S().col().w(300), {row});
    e.compute(column, Size{800, 600});
    CHECK_RECT(row, 0, 0, 300, 40);
    CHECK_RECT(text, 0, 0, 300, 40);
    CHECK_RECT(column, 0, 0, 300, 40);

    e.clear();
    text = e.newLeaf({}, words(3, 50));
    row = node(e, S().row().self(AlignItems::Center), {text});
    column = node(e, S().col().w(300), {row});
    e.compute(column, Size{800, 600});
    CHECK_RECT(row, 65, 0, 170, 20);
}

void TestAtramentLayout::justifyContent_data()
{
    QTest::addColumn<JustifyContent>("justify");
    QTest::addColumn<float>("itemWidth");
    QTest::addColumn<float>("firstX");
    QTest::addColumn<float>("secondX");

    // Two items of 50 in 300: 200 free.
    QTest::newRow("flex-start") << JustifyContent::FlexStart << 50.0f << 0.0f << 50.0f;
    QTest::newRow("flex-end") << JustifyContent::FlexEnd << 50.0f << 200.0f << 250.0f;
    QTest::newRow("center") << JustifyContent::Center << 50.0f << 100.0f << 150.0f;
    QTest::newRow("space-between") << JustifyContent::SpaceBetween << 50.0f << 0.0f << 250.0f;
    QTest::newRow("space-around") << JustifyContent::SpaceAround << 50.0f << 50.0f << 200.0f;
    QTest::newRow("space-evenly") << JustifyContent::SpaceEvenly << 50.0f << 66.667f << 183.333f;
    // Two of 200 that do not shrink: 100 too much; center overflows both ways, the space-* values
    // fall back to flex-start (space-around and space-evenly to safe center, as in browsers).
    QTest::newRow("overflow flex-start") << JustifyContent::FlexStart << 200.0f << 0.0f << 200.0f;
    QTest::newRow("overflow flex-end") << JustifyContent::FlexEnd << 200.0f << -100.0f << 100.0f;
    QTest::newRow("overflow center") << JustifyContent::Center << 200.0f << -50.0f << 150.0f;
    QTest::newRow("overflow space-between") << JustifyContent::SpaceBetween << 200.0f << 0.0f << 200.0f;
    QTest::newRow("overflow space-around") << JustifyContent::SpaceAround << 200.0f << 0.0f << 200.0f;
    QTest::newRow("overflow space-evenly") << JustifyContent::SpaceEvenly << 200.0f << 0.0f << 200.0f;
}

void TestAtramentLayout::justifyContent()
{
    QFETCH(JustifyContent, justify);
    QFETCH(float, itemWidth);
    QFETCH(float, firstX);
    QFETCH(float, secondX);
    LayoutEngine e;
    const NodeId a = e.newLeaf(S().size(itemWidth, 50).shrink(0));
    const NodeId b = e.newLeaf(S().size(itemWidth, 50).shrink(0));
    const NodeId row = node(e, S().row().size(300, 50).justify(justify), {a, b});
    e.compute(row, Size{800, 600});
    CHECK_RECT(a, firstX, 0, itemWidth, 50);
    CHECK_RECT(b, secondX, 0, itemWidth, 50);
}

void TestAtramentLayout::autoMargins()
{
    LayoutEngine e;
    const auto twoItems = [&e](LengthEdges marginA, JustifyContent justify, float width, NodeId &a, NodeId &b) {
        e.clear();
        a = e.newLeaf(S().size(width, 50).shrink(0).margin(marginA));
        b = e.newLeaf(S().size(width, 50).shrink(0));
        const NodeId row = node(e, S().row().size(300, 100).justify(justify), {a, b});
        e.compute(row, Size{800, 600});
    };
    NodeId a = kNoNode;
    NodeId b = kNoNode;
    // margin-right: auto pushes the next item to the end.
    twoItems({px(0), autoL, px(0), px(0)}, JustifyContent::FlexStart, 50, a, b);
    CHECK_RECT(a, 0, 0, 50, 50);
    CHECK_RECT(b, 250, 0, 50, 50);
    // Both sides auto share the 200: 100 each.
    twoItems({px(0), autoL, px(0), autoL}, JustifyContent::FlexStart, 50, a, b);
    CHECK_RECT(a, 100, 0, 50, 50);
    CHECK_RECT(b, 250, 0, 50, 50);
    // Auto margins take the free space before justify-content.
    twoItems({px(0), px(0), px(0), autoL}, JustifyContent::Center, 50, a, b);
    CHECK_RECT(a, 200, 0, 50, 50);
    // In the cross axis: auto top pushes down, both centre (and keep it from stretching).
    twoItems({autoL, px(0), px(0), px(0)}, JustifyContent::FlexStart, 50, a, b);
    CHECK_RECT(a, 0, 50, 50, 50);
    twoItems({autoL, px(0), autoL, px(0)}, JustifyContent::FlexStart, 50, a, b);
    CHECK_RECT(a, 0, 25, 50, 50);
    // No free space: the auto margins are 0.
    twoItems({px(0), px(0), px(0), autoL}, JustifyContent::FlexStart, 200, a, b);
    CHECK_RECT(a, 0, 0, 200, 50);
    CHECK_RECT(b, 200, 0, 200, 50);

    // An item without a height and auto cross margins is not stretched: centred at its content.
    e.clear();
    const NodeId c = e.newLeaf(S().w(50).margin({autoL, px(0), autoL, px(0)}), words(1, 30));
    const NodeId row = node(e, S().row().size(300, 100), {c});
    e.compute(row, Size{800, 600});
    CHECK_RECT(c, 0, 40, 50, 20);
}

void TestAtramentLayout::alignItems_data()
{
    QTest::addColumn<AlignItems>("items");
    QTest::addColumn<AlignItems>("self");
    QTest::addColumn<float>("height");    // -1: auto
    QTest::addColumn<float>("maxHeight"); // -1: none
    QTest::addColumn<float>("marginTop");
    QTest::addColumn<float>("marginBottom");
    QTest::addColumn<Rect>("expected");

    // One item 50 wide in a row 300 × 100.
    const auto a = AlignItems::Auto;
    QTest::newRow("stretch") << AlignItems::Stretch << a << -1.0f << -1.0f << 0.0f << 0.0f
                             << Rect::fromXYWH(0, 0, 50, 100);
    QTest::newRow("stretch keeps a height")
            << AlignItems::Stretch << a << 40.0f << -1.0f << 0.0f << 0.0f << Rect::fromXYWH(0, 0, 50, 40);
    QTest::newRow("stretch to max-height")
            << AlignItems::Stretch << a << -1.0f << 70.0f << 0.0f << 0.0f << Rect::fromXYWH(0, 0, 50, 70);
    QTest::newRow("stretch within margins")
            << AlignItems::Stretch << a << -1.0f << -1.0f << 10.0f << 5.0f << Rect::fromXYWH(0, 10, 50, 85);
    QTest::newRow("flex-start") << AlignItems::FlexStart << a << 40.0f << -1.0f << 0.0f << 0.0f
                                << Rect::fromXYWH(0, 0, 50, 40);
    QTest::newRow("flex-start, content height")
            << AlignItems::FlexStart << a << -1.0f << -1.0f << 0.0f << 0.0f << Rect::fromXYWH(0, 0, 50, 20);
    QTest::newRow("flex-end") << AlignItems::FlexEnd << a << 40.0f << -1.0f << 0.0f << 0.0f
                              << Rect::fromXYWH(0, 60, 50, 40);
    QTest::newRow("flex-end with margin")
            << AlignItems::FlexEnd << a << 40.0f << -1.0f << 0.0f << 10.0f << Rect::fromXYWH(0, 50, 50, 40);
    QTest::newRow("center") << AlignItems::Center << a << 40.0f << -1.0f << 0.0f << 0.0f
                            << Rect::fromXYWH(0, 30, 50, 40);
    QTest::newRow("baseline as flex-start")
            << AlignItems::Baseline << a << 40.0f << -1.0f << 0.0f << 0.0f << Rect::fromXYWH(0, 0, 50, 40);
    QTest::newRow("align-self wins") << AlignItems::Center << AlignItems::FlexEnd << 40.0f << -1.0f << 0.0f << 0.0f
                                     << Rect::fromXYWH(0, 60, 50, 40);
    QTest::newRow("align-self stretch") << AlignItems::Center << AlignItems::Stretch << -1.0f << -1.0f << 0.0f << 0.0f
                                        << Rect::fromXYWH(0, 0, 50, 100);
}

void TestAtramentLayout::alignItems()
{
    QFETCH(AlignItems, items);
    QFETCH(AlignItems, self);
    QFETCH(float, height);
    QFETCH(float, maxHeight);
    QFETCH(float, marginTop);
    QFETCH(float, marginBottom);
    QFETCH(Rect, expected);
    LayoutEngine e;
    S s;
    s.w(50).self(self).margin(marginTop, 0, marginBottom, 0);
    if (height >= 0)
        s.h(height);
    if (maxHeight >= 0)
        s.maxH(px(maxHeight));
    const NodeId item = e.newLeaf(s, words(1, 30)); // content: 30 × 20
    const NodeId row = node(e, S().row().size(300, 100).items(items), {item});
    e.compute(row, Size{800, 600});
    const QString m = mismatch(e.layout(item).rect, expected);
    QVERIFY2(m.isEmpty(), qPrintable(m));
}

void TestAtramentLayout::reverseDirections()
{
    LayoutEngine e;
    // row-reverse: the first item at the right, its right margin towards the edge.
    NodeId a = e.newLeaf(S().size(50, 50).margin(0, 10, 0, 0));
    NodeId b = e.newLeaf(S().size(70, 50));
    NodeId row = node(e, S().dir(FlexDirection::RowReverse).size(300, 50), {a, b});
    e.compute(row, Size{800, 600});
    CHECK_RECT(a, 240, 0, 50, 50);
    CHECK_RECT(b, 170, 0, 70, 50);

    // justify-content: flex-end packs them at the left (the main end).
    e.clear();
    a = e.newLeaf(S().size(50, 50).margin(0, 10, 0, 0));
    b = e.newLeaf(S().size(70, 50));
    row = node(e, S().dir(FlexDirection::RowReverse).size(300, 50).justify(JustifyContent::FlexEnd), {a, b});
    e.compute(row, Size{800, 600});
    CHECK_RECT(b, 0, 0, 70, 50);
    CHECK_RECT(a, 70, 0, 50, 50);

    // column-reverse inside padding.
    e.clear();
    a = e.newLeaf(S().h(30));
    b = e.newLeaf(S().h(40));
    const NodeId column = node(e, S().dir(FlexDirection::ColumnReverse).size(50, 200).pad(10), {a, b});
    e.compute(column, Size{800, 600});
    CHECK_RECT(a, 10, 160, 30, 30);
    CHECK_RECT(b, 10, 120, 30, 40);
}

void TestAtramentLayout::gaps()
{
    LayoutEngine e;
    NodeId items[5];
    for (NodeId &item : items)
        item = e.newLeaf(S().size(50, 20));
    NodeId row = node(e, S().row().w(300).gap(0, 10), {items[0], items[1], items[2]});
    e.compute(row, Size{800, 600});
    CHECK_RECT(items[0], 0, 0, 50, 20);
    CHECK_RECT(items[1], 60, 0, 50, 20);
    CHECK_RECT(items[2], 120, 0, 50, 20);
    CHECK_RECT(row, 0, 0, 300, 20);

    e.clear();
    for (NodeId &item : items)
        item = e.newLeaf(S().size(50, 20));
    const NodeId column = node(e, S().col().gap(5, 0), {items[0], items[1], items[2]});
    e.compute(column, Size{800, 600});
    CHECK_RECT(items[2], 0, 50, 50, 20);
    CHECK_RECT(column, 0, 0, 50, 70);

    // Wrapping: three fit 170 exactly with two gaps of 10, the rest go to a line 5 lower.
    e.clear();
    for (NodeId &item : items)
        item = e.newLeaf(S().size(50, 20));
    row = e.newNode(S().row().wrap().w(170).gap(5, 10), items);
    e.compute(row, Size{800, 600});
    CHECK_RECT(items[2], 120, 0, 50, 20);
    CHECK_RECT(items[3], 0, 25, 50, 20);
    CHECK_RECT(items[4], 60, 25, 50, 20);
    CHECK_RECT(row, 0, 0, 170, 45);
}

void TestAtramentLayout::wrapAndAlignContent_data()
{
    QTest::addColumn<AlignContent>("alignContent");
    QTest::addColumn<QList<float>>("lineYs");
    QTest::addColumn<float>("lastHeight");

    // Items 80 wide in a wrapping row 200 × 200: lines of two, two, one; heights 30, 50 | 20, 40 |
    // auto (an empty item: 0). Lines 50, 40 and 0: 110 free.
    QTest::newRow("flex-start") << AlignContent::FlexStart << QList<float>{0, 50, 90} << 0.0f;
    QTest::newRow("flex-end") << AlignContent::FlexEnd << QList<float>{110, 160, 200} << 0.0f;
    QTest::newRow("center") << AlignContent::Center << QList<float>{55, 105, 145} << 0.0f;
    QTest::newRow("space-between") << AlignContent::SpaceBetween << QList<float>{0, 105, 200} << 0.0f;
    QTest::newRow("space-around") << AlignContent::SpaceAround << QList<float>{18.333f, 105, 181.667f} << 0.0f;
    // Each line 36.667 taller; the auto-height item stretches to its line.
    QTest::newRow("stretch") << AlignContent::Stretch << QList<float>{0, 86.667f, 163.333f} << 36.667f;
}

void TestAtramentLayout::wrapAndAlignContent()
{
    QFETCH(AlignContent, alignContent);
    QFETCH(QList<float>, lineYs);
    QFETCH(float, lastHeight);
    LayoutEngine e;
    const float heights[] = {30, 50, 20, 40};
    std::vector<NodeId> items;
    for (const float height : heights)
        items.push_back(e.newLeaf(S().size(80, height)));
    items.push_back(e.newLeaf(S().w(80)));
    const NodeId row = e.newNode(S().row().wrap().size(200, 200).content(alignContent), items);
    e.compute(row, Size{800, 600});
    CHECK_RECT(items[0], 0, lineYs[0], 80, 30);
    CHECK_RECT(items[1], 80, lineYs[0], 80, 50);
    CHECK_RECT(items[2], 0, lineYs[1], 80, 20);
    CHECK_RECT(items[3], 80, lineYs[1], 80, 40);
    CHECK_RECT(items[4], 0, lineYs[2], 80, lastHeight);
}

void TestAtramentLayout::alignContentOverflow()
{
    // Two lines of 40 in a wrapping row 50 high: 30 too much. center overflows both ways,
    // space-around falls back to safe center (flex-start), stretch and space-between to flex-start.
    const std::pair<AlignContent, float> cases[] = {{AlignContent::FlexStart, 0},   {AlignContent::FlexEnd, -30},
                                                    {AlignContent::Center, -15},    {AlignContent::SpaceBetween, 0},
                                                    {AlignContent::SpaceAround, 0}, {AlignContent::Stretch, 0}};
    for (const auto &[alignContent, firstY] : cases) {
        LayoutEngine e;
        std::vector<NodeId> items;
        for (int i = 0; i < 4; ++i)
            items.push_back(e.newLeaf(S().size(80, 40)));
        const NodeId row = e.newNode(S().row().wrap().size(200, 50).content(alignContent), items);
        e.compute(row, Size{800, 600});
        CHECK_RECT(items[0], 0, firstY, 80, 40);
        CHECK_RECT(items[2], 0, firstY + 40, 80, 40);
    }
}

void TestAtramentLayout::columnWrap()
{
    // A column 100 high wraps 40 px items two a line; its width is two lines of 50.
    LayoutEngine e;
    std::vector<NodeId> items;
    for (int i = 0; i < 4; ++i)
        items.push_back(e.newLeaf(S().size(50, 40)));
    NodeId column = e.newNode(S().col().wrap().h(100), items);
    e.compute(column, Size{800, 600});
    CHECK_RECT(items[1], 0, 40, 50, 40);
    CHECK_RECT(items[2], 50, 0, 50, 40);
    CHECK_RECT(items[3], 50, 40, 50, 40);
    CHECK_RECT(column, 0, 0, 100, 100);

    // With only a max-height it breaks there and is as high as its longest line.
    e.clear();
    items.clear();
    for (int i = 0; i < 4; ++i)
        items.push_back(e.newLeaf(S().size(50, 40)));
    column = e.newNode(S().col().wrap().maxH(px(100)), items);
    e.compute(column, Size{800, 600});
    CHECK_RECT(items[2], 50, 0, 50, 40);
    CHECK_RECT(column, 0, 0, 100, 80);

    // With an auto height and no max it is one line: an auto block size is the content's.
    e.clear();
    items.clear();
    for (int i = 0; i < 4; ++i)
        items.push_back(e.newLeaf(S().size(50, 40)));
    column = e.newNode(S().col().wrap(), items);
    e.compute(column, Size{800, 100});
    CHECK_RECT(items[3], 0, 120, 50, 40);
    CHECK_RECT(column, 0, 0, 50, 160);
}

void TestAtramentLayout::percentages()
{
    LayoutEngine e;
    // In a row 400 × 200: 50 % × 25 %, a flex-basis of 25 % with a height of 100 %, and margins of
    // 10 % and 5 % — both of the width (CSS).
    const NodeId a = e.newLeaf(S().w(pct(50)).h(pct(25)));
    const NodeId b = e.newLeaf(S().basis(pct(25)).h(pct(100)));
    const NodeId c = e.newLeaf(S().size(20, 20).margin({pct(5), px(0), px(0), pct(10)}));
    const NodeId row = node(e, S().row().size(400, 200), {a, b, c});
    e.compute(row, Size{800, 600});
    CHECK_RECT(a, 0, 0, 200, 50);
    CHECK_RECT(b, 200, 0, 100, 200);
    CHECK_RECT(c, 340, 20, 20, 20);

    // In a column of auto height: a height of 50 % behaves as auto, a width of 50 % resolves,
    // a min-height of 50 % is 0, a max-width of 25 % caps a stretched item.
    e.clear();
    const NodeId d = e.newLeaf(S().h(pct(50)).minH(pct(50)), words(1, 30));
    const NodeId f = e.newLeaf(S().w(pct(50)).h(10));
    const NodeId g = e.newLeaf(S().maxW(pct(25)).h(10));
    const NodeId column = node(e, S().col().w(400), {d, f, g});
    e.compute(column, Size{800, 600});
    CHECK_RECT(d, 0, 0, 400, 20);
    CHECK_RECT(f, 0, 20, 200, 10);
    CHECK_RECT(g, 0, 30, 100, 10);
    CHECK_RECT(column, 0, 0, 400, 40);

    // The root's percentages are of the window.
    e.clear();
    const NodeId root = e.newNode(S().w(pct(50)).h(pct(10)), {});
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 400, 60);
}

void TestAtramentLayout::aspectRatio()
{
    LayoutEngine e;
    // A width and a ratio of 2 give the height; a height and 0.5 the width.
    NodeId a = e.newLeaf(S().w(100).ratio(2));
    NodeId b = e.newLeaf(S().h(40).ratio(0.5f));
    NodeId row = node(e, S().row().size(400, 200).items(AlignItems::FlexStart), {a, b});
    e.compute(row, Size{800, 600});
    CHECK_RECT(a, 0, 0, 100, 50);
    CHECK_RECT(b, 100, 0, 20, 40);

    // Stretched to a column's width (300): the height follows (100).
    e.clear();
    a = e.newLeaf(S().ratio(3));
    const NodeId column = node(e, S().col().w(300), {a});
    e.compute(column, Size{800, 600});
    CHECK_RECT(a, 0, 0, 300, 100);
    CHECK_RECT(column, 0, 0, 300, 100);

    // Stretched to a row's height (60): the width follows (120).
    e.clear();
    a = e.newLeaf(S().ratio(2));
    b = e.newLeaf(S().size(10, 10));
    row = node(e, S().row().h(60), {a, b});
    e.compute(row, Size{800, 600});
    CHECK_RECT(a, 0, 0, 120, 60);
    CHECK_RECT(b, 120, 0, 10, 10);
}

void TestAtramentLayout::paddingAndBorder()
{
    LayoutEngine e;
    // The content box of a row 300 × 100 with padding 10 and border 5: 270 × 70 at (15, 15).
    NodeId a = e.newLeaf(S().w(50));
    NodeId row = node(e, S().row().size(300, 100).pad(10).border(5), {a});
    e.compute(row, Size{800, 600});
    CHECK_RECT(a, 15, 15, 50, 70);

    // An auto-sized column adds them to its content.
    e.clear();
    a = e.newLeaf(S().size(50, 20));
    NodeId column = node(e, S().col().pad(8).border(2), {a});
    e.compute(column, Size{800, 600});
    CHECK_RECT(column, 0, 0, 70, 40);
    CHECK_RECT(a, 10, 10, 50, 20);

    // A border box is never smaller than its padding and border (width 10, padding 20 a side).
    e.clear();
    a = e.newLeaf(S().size(10, 10).pad(Edges::symmetric(0, 20)));
    row = node(e, S().row().size(300, 100).items(AlignItems::FlexStart), {a});
    e.compute(row, Size{800, 600});
    CHECK_RECT(a, 0, 0, 40, 10);

    // A measured leaf with padding 5: the measure sees the content box (two words of 30, 70 wide).
    e.clear();
    std::vector<MeasureCall> calls;
    Paragraph p;
    p.words = {30, 30};
    p.log = &calls;
    const NodeId text = e.newLeaf(S().pad(5), p);
    row = node(e, S().row().items(AlignItems::FlexStart), {text});
    e.compute(row, Size{800, 600});
    CHECK_RECT(text, 0, 0, 80, 30);
    QVERIFY(!calls.empty());
    QCOMPARE(calls.front().availableWidth.kind, AvailableSpace::Kind::MaxContent);
}

void TestAtramentLayout::displayNone()
{
    LayoutEngine e;
    // A hidden item and its subtree take no space and get zero rects; gaps skip it.
    const NodeId a = e.newLeaf(S().size(50, 20));
    const NodeId hiddenChild = e.newLeaf(S().size(30, 30));
    const NodeId hidden = node(e, S().size(100, 100).none(), {hiddenChild});
    const NodeId b = e.newLeaf(S().size(50, 20));
    const NodeId column = node(e, S().col().gap(10, 0), {a, hidden, b});
    e.compute(column, Size{800, 600});
    CHECK_RECT(a, 0, 0, 50, 20);
    CHECK_RECT(b, 0, 30, 50, 20);
    CHECK_RECT(hidden, 0, 0, 0, 0);
    CHECK_RECT(hiddenChild, 0, 0, 0, 0);
    CHECK_RECT(column, 0, 0, 50, 50);

    // A hidden root: everything zero, also after an earlier layout.
    e.clear();
    const NodeId child = e.newLeaf(S().size(30, 30));
    const NodeId visible = node(e, S().size(100, 100), {child});
    const NodeId root = node(e, S().none().size(200, 200), {visible});
    e.compute(visible, Size{800, 600});
    CHECK_RECT(visible, 0, 0, 100, 100);
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 0, 0);
    CHECK_RECT(visible, 0, 0, 0, 0);
    CHECK_RECT(child, 0, 0, 0, 0);
}

void TestAtramentLayout::absoluteChildren()
{
    // A container 300 × 200 with padding 10 and border 5: the padding box is 290 × 190 at (5, 5),
    // the content box 270 × 170 at (15, 15).
    LayoutEngine e;
    const auto place = [&e](const LayoutStyle &container, const LayoutStyle &child,
                            MeasureFunction measure = {}) -> Rect {
        e.clear();
        const NodeId inFlow = e.newLeaf(S().size(50, 50));
        const NodeId abs = e.newLeaf(child, std::move(measure));
        S s;
        s.s = container;
        const NodeId root = node(e, s.size(300, 200).pad(10).border(5), {inFlow, abs});
        e.compute(root, Size{800, 600});
        // The in-flow item is placed as if the absolute one were not there.
        const Rect flow = e.layout(inFlow).rect;
        if (container.flexDirection == FlexDirection::Row && container.justifyContent == JustifyContent::FlexStart
            && flow.origin != Point{15, 15})
            return Rect::fromXYWH(-1, -1, -1, -1);
        return e.layout(abs).rect;
    };
    const auto check = [](Rect actual, Rect expected) {
        const QString m = mismatch(actual, expected);
        if (!m.isEmpty())
            qWarning("%s", qPrintable(m));
        return m.isEmpty();
    };
    const LayoutStyle row = S().row();

    QVERIFY(check(place(row, S().size(50, 40).absolute({px(30), autoL, autoL, px(20)})),
                  Rect::fromXYWH(25, 35, 50, 40)));
    QVERIFY(check(place(row, S().size(50, 40).absolute({autoL, px(20), px(30), autoL})),
                  Rect::fromXYWH(225, 125, 50, 40)));
    // Both insets set: the size between them.
    QVERIFY(check(place(row, S().absolute({px(0), px(10), px(0), px(10)})), Rect::fromXYWH(15, 5, 270, 190)));
    // Percentages of the padding box: left 10 % of 290, top 50 % of 190, 50 % × 10 %.
    QVERIFY(check(place(row, S().w(pct(50)).h(pct(10)).absolute({pct(50), autoL, autoL, pct(10)})),
                  Rect::fromXYWH(34, 100, 145, 19)));
    // Auto insets: the static position, by justify-content and align-items / align-self.
    QVERIFY(check(place(row, S().size(50, 40).absolute()), Rect::fromXYWH(15, 15, 50, 40)));
    QVERIFY(check(
            place(S().row().justify(JustifyContent::Center).items(AlignItems::Center), S().size(50, 40).absolute()),
            Rect::fromXYWH(125, 80, 50, 40)));
    QVERIFY(check(
            place(S().row().justify(JustifyContent::FlexEnd), S().size(50, 40).absolute().self(AlignItems::FlexEnd)),
            Rect::fromXYWH(235, 145, 50, 40)));
    QVERIFY(check(place(S().dir(FlexDirection::RowReverse), S().size(50, 40).absolute()),
                  Rect::fromXYWH(235, 15, 50, 40)));
    QVERIFY(check(
            place(S().col().justify(JustifyContent::Center).items(AlignItems::FlexEnd), S().size(50, 40).absolute()),
            Rect::fromXYWH(235, 80, 50, 40)));
    // Content-sized: two words of 40.
    QVERIFY(check(place(row, S().absolute({px(0), autoL, autoL, px(0)}), words(2, 40)), Rect::fromXYWH(5, 5, 90, 20)));
    // A long text wraps at the space the insets leave (290 - 100): 190, three words of 50 a line.
    QVERIFY(check(place(row, S().absolute({px(0), autoL, autoL, px(100)}), words(6, 50)),
                  Rect::fromXYWH(105, 5, 190, 40)));
    // Auto margins between two insets centre it.
    QVERIFY(check(
            place(row, S().size(50, 40).margin({px(0), autoL, px(0), autoL}).absolute({px(0), px(0), autoL, px(0)})),
            Rect::fromXYWH(125, 5, 50, 40)));
}

void TestAtramentLayout::measureArguments()
{
    // A text with padding 5 stretched in a column 200 wide: one measurement, of the content box
    // at the known width 190; the final layout needs none.
    LayoutEngine e;
    std::vector<MeasureCall> calls;
    const NodeId text = e.newLeaf(S().pad(5), words(3, 50, &calls));
    const NodeId column = node(e, S().col().w(200), {text});
    e.compute(column, Size{800, 600});
    CHECK_RECT(text, 0, 0, 200, 30);
    QCOMPARE(calls.size(), std::size_t(1));
    QCOMPARE(e.measureCalls(), std::size_t(1));
    QCOMPARE(calls[0].knownWidth, std::optional<float>(190));
    QCOMPARE(calls[0].knownHeight, std::nullopt);
    QCOMPARE(calls[0].availableWidth, AvailableSpace::definite(190));
    QCOMPARE(calls[0].availableHeight.kind, AvailableSpace::Kind::MaxContent);

    // In a row: its max-content width first (an intrinsic width: the height available does not
    // matter); laid out at that width it needs no second measurement.
    e.clear();
    calls.clear();
    const NodeId text2 = e.newLeaf(S().pad(5), words(3, 50, &calls));
    const NodeId row = node(e, S().row().w(300), {text2});
    e.compute(row, Size{800, 600});
    CHECK_RECT(text2, 0, 0, 180, 30);
    QCOMPARE(calls.size(), std::size_t(1));
    QCOMPARE(calls[0].knownWidth, std::nullopt);
    QCOMPARE(calls[0].availableWidth.kind, AvailableSpace::Kind::MaxContent);
    QCOMPARE(calls[0].availableHeight.kind, AvailableSpace::Kind::MaxContent);

    // compute() with explicit available spaces: a list measured at max-content height.
    e.clear();
    calls.clear();
    const NodeId text3 = e.newLeaf({}, words(3, 40, &calls));
    const NodeId list = node(e, S().col(), {text3});
    e.compute(list, AvailableSpace::definite(100), AvailableSpace::maxContent());
    CHECK_RECT(text3, 0, 0, 100, 40); // fit-content of 100: two words, then one
    CHECK_RECT(list, 0, 0, 100, 40);
    QCOMPARE(calls.size(), std::size_t(1));
}

void TestAtramentLayout::contentSize()
{
    // A scroll container 100 × 100 with padding 10 and a child 300 high: the content is the
    // child's margin box (and a margin of 4) and the padding around it.
    LayoutEngine e;
    const NodeId child = e.newLeaf(S().h(300).shrink(0).margin(0, 0, 4, 0));
    const NodeId scroller = node(e, S().col().size(100, 100).pad(10).border(1).overflow(Overflow::Scroll), {child});
    e.compute(scroller, Size{800, 600});
    CHECK_RECT(child, 11, 11, 78, 300);
    QCOMPARE(e.layout(scroller).contentSize, (Size{98, 324}));
    QCOMPARE(e.layout(child).contentSize, (Size{78, 300})); // a leaf: its padding box
}

void TestAtramentLayout::absoluteCoordinates()
{
    LayoutEngine e;
    const NodeId leaf = e.newLeaf(S().size(10, 10));
    const NodeId middle = node(e, S().row().pad(7).margin(0, 0, 0, 5), {leaf});
    const NodeId root = node(e, S().row().size(200, 200).pad(10).items(AlignItems::FlexStart), {middle});
    e.compute(root, Size{800, 600});
    CHECK_RECT(middle, 15, 10, 24, 24);
    QCOMPARE(e.absolute(leaf), Rect::fromXYWH(22, 17, 10, 10));
    QCOMPARE(e.absolute(root), Rect::fromXYWH(0, 0, 200, 200));
}

void TestAtramentLayout::malformedInput()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    LayoutEngine e;
    std::vector<NodeId> all;
    const auto add = [&all](NodeId id) {
        all.push_back(id);
        return id;
    };

    // Negative sizes (auto), padding and gaps (0); NaN and infinite values; max below min (min wins).
    const NodeId negative = add(e.newLeaf(S().w(-50).h(-1).pad(-5).border(-2)));
    const NodeId minWins = add(e.newLeaf(S().w(10).h(10).minW(px(100)).maxW(px(50))));
    const NodeId notANumber =
            add(e.newLeaf(S().w(nan).h(nan).grow(nan).shrink(nan).basis(nan).margin(nan, nan, nan, nan)));
    const NodeId infinite = add(e.newLeaf(S().w(inf).h(20).minH(px(inf)).basis(inf)));
    const NodeId hugePercent =
            add(e.newLeaf(S().w(pct(1e30f)).h(pct(-1e30f)).margin({pct(1e30f), pct(-1e30f), pct(nan), pct(inf)})));
    const NodeId badRatio = add(e.newLeaf(S().w(10).ratio(nan)));
    const NodeId zeroRatio = add(e.newLeaf(S().w(10).ratio(0)));
    const NodeId tinyRatio = add(e.newLeaf(S().w(10).ratio(1e-30f)));
    const NodeId crazyMeasure =
            add(e.newLeaf({}, [](std::optional<float>, std::optional<float>, AvailableSpace, AvailableSpace) {
                return Size{std::numeric_limits<float>::quiet_NaN(), -std::numeric_limits<float>::infinity()};
            }));
    const NodeId infiniteMeasure =
            add(e.newLeaf({}, [](std::optional<float>, std::optional<float>, AvailableSpace, AvailableSpace) {
                return Size{std::numeric_limits<float>::infinity(), 1e38f};
            }));
    const NodeId absNan = add(e.newLeaf(S().absolute({px(nan), px(inf), pct(nan), px(-inf)})));
    const NodeId row = add(e.newNode(S().row().wrap().gap(-10, nan).pad(nan).w(nan),
                                     std::vector<NodeId>{negative, minWins, notANumber, infinite, hugePercent, badRatio,
                                                         zeroRatio, tinyRatio, crazyMeasure, infiniteMeasure, absNan}));
    e.compute(row, AvailableSpace::definite(nan), AvailableSpace::definite(-inf));
    for (const NodeId id : all)
        QVERIFY2(finite(e.layout(id).rect), qPrintable(QStringLiteral("node %1").arg(id)));
    QCOMPARE(e.layout(minWins).rect.width(), 100.0f);
    QCOMPARE(e.layout(negative).rect.width(), 0.0f);

    e.compute(row, AvailableSpace::definite(inf), AvailableSpace::minContent());
    for (const NodeId id : all)
        QVERIFY2(finite(e.layout(id).rect), qPrintable(QStringLiteral("node %1").arg(id)));
    e.compute(row, Size{-100, nan});
    for (const NodeId id : all)
        QVERIFY2(finite(e.layout(id).rect), qPrintable(QStringLiteral("node %1").arg(id)));

    // Huge flex factors keep their proportions; infinite ones count as the largest float.
    e.clear();
    NodeId a = e.newLeaf(S().basis(0).grow(1e30f).h(10));
    NodeId b = e.newLeaf(S().basis(0).grow(3e30f).h(10));
    NodeId r = node(e, S().row().size(400, 10), {a, b});
    e.compute(r, Size{800, 600});
    CHECK_RECT(a, 0, 0, 100, 10);
    CHECK_RECT(b, 100, 0, 300, 10);
    e.clear();
    a = e.newLeaf(S().basis(0).grow(inf).h(10));
    b = e.newLeaf(S().basis(0).grow(std::numeric_limits<float>::max()).h(10));
    const NodeId c = e.newLeaf(S().basis(300).shrink(inf).h(10));
    const NodeId d = e.newLeaf(S().basis(300).shrink(std::numeric_limits<float>::max()).h(10));
    r = node(e, S().row().size(400, 10), {a, b});
    const NodeId r2 = node(e, S().row().size(400, 10), {c, d});
    const NodeId both = node(e, S().col().size(400, 20), {r, r2});
    e.compute(both, Size{800, 600});
    CHECK_RECT(a, 0, 0, 200, 10);
    CHECK_RECT(b, 200, 0, 200, 10);
    CHECK_RECT(c, 0, 0, 200, 10);
    CHECK_RECT(d, 200, 0, 200, 10);
}

void TestAtramentLayout::cachePerCompute()
{
    // Every compute() starts afresh: the same measurements again, counted for that compute only.
    LayoutEngine e;
    std::vector<MeasureCall> calls;
    const NodeId icon = e.newLeaf(S().size(24, 24).shrink(0));
    const NodeId text = e.newLeaf(S().grow(1), words(10, 40, &calls));
    const NodeId row = node(e, S().row().w(300), {icon, text});
    e.compute(row, Size{800, 600});
    const std::size_t first = e.measureCalls();
    QCOMPARE(calls.size(), first);
    e.compute(row, Size{800, 600});
    QCOMPARE(e.measureCalls(), first);
    QCOMPARE(calls.size(), 2 * first);
    QVERIFY2(first <= 3, qPrintable(QString::number(first)));
    CHECK_RECT(text, 24, 0, 276, 40);
}

void TestAtramentLayout::clearAndRebuild()
{
    LayoutEngine e;
    for (int frame = 0; frame < 3; ++frame) {
        e.clear();
        QCOMPARE(e.nodeCount(), std::size_t(0));
        const NodeId a = e.newLeaf(S().size(10 + float(frame), 10));
        const NodeId b = e.newLeaf(S().grow(1).h(10));
        const NodeId row = node(e, S().row().w(100), {a, b});
        QCOMPARE(a, NodeId(0));
        QCOMPARE(row, NodeId(2));
        e.compute(row, Size{800, 600});
        CHECK_RECT(b, 10 + float(frame), 0, 90 - float(frame), 10);
        QCOMPARE(e.style(a).width, px(10 + float(frame)));
    }
}

// Compositions laid out by Chromium 158 (browser_scenes.html, which prints these rows): every
// node's border box relative to its parent's, in document order.
void TestAtramentLayout::browserScenes_data()
{
    QTest::addColumn<float>("windowWidth");
    QTest::addColumn<float>("windowHeight");
    QTest::addColumn<QString>("scene");
    QTest::addColumn<QList<float>>("rects");

    // clang-format off
    QTest::newRow("flex-basis 0 texts, auto row") << 800.0f << 600.0f
            << QStringLiteral("(| [40 40 40|flex-grow:1;flex-basis:0px] [60 60|flex-grow:1;flex-basis:0px])")
            << QList<float>{ 0, 0, 270, 40,   0, 0, 135, 40,   135, 0, 135, 40 };
    QTest::newRow("flex-basis 0 texts, narrow window") << 150.0f << 600.0f
            << QStringLiteral("(| [40 40 40|flex-grow:1;flex-basis:0px] [60 60|flex-grow:1;flex-basis:0px])")
            << QList<float>{ 0, 0, 150, 60,   0, 0, 75, 60,   75, 0, 75, 60 };
    QTest::newRow("text basis 0 beside basis auto") << 800.0f << 600.0f
            << QStringLiteral("(width:400px| [100 100 100|flex-grow:1;flex-basis:0px] [100 100|flex-grow:1])")
            << QList<float>{ 0, 0, 400, 60,   0, 0, 100, 60,   100, 0, 300, 60 };
    QTest::newRow("wrapping row, fit-content") << 250.0f << 600.0f
            << QStringLiteral("(flex-wrap:wrap;column-gap:10px| (width:100px;height:20px) (width:100px;"
                              "height:20px) (width:100px;height:20px))")
            << QList<float>{ 0, 0, 250, 40,   0, 0, 100, 20,   110, 0, 100, 20,   0, 20, 100, 20 };
    QTest::newRow("list of rows with growing text") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:320px;padding:12px;row-gap:6px| (column-gap:8px;"
                              "align-items:center| (width:20px;height:20px;flex-shrink:0) [50 30 70 40 60 "
                              "50|flex-grow:1] (width:12px;height:12px;flex-shrink:0)) (column-gap:8px| [80 80 "
                              "80|flex-grow:1] [30|]))")
            << QList<float>{ 0, 0, 320, 110,   12, 12, 296, 40,   0, 10, 20, 20,   28, 0, 248, 40,
                             284, 14, 12, 12,   12, 58, 296, 40,   0, 0, 258, 40,   266, 0, 30, 40 };
    QTest::newRow("column, flex-start texts") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:300px;align-items:flex-start| [50 50 50 50 50 50 50 "
                              "50|] [30 30|])")
            << QList<float>{ 0, 0, 300, 60,   0, 0, 300, 40,   0, 40, 70, 20 };
    QTest::newRow("column, centred texts") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:300px;align-items:center| [50 50|] [50 50 50 50 50 "
                              "50|])")
            << QList<float>{ 0, 0, 300, 60,   95, 0, 110, 20,   0, 20, 300, 40 };
    QTest::newRow("grow with max and min") << 800.0f << 600.0f
            << QStringLiteral("(width:500px| (flex-grow:1;max-width:100px;height:10px) (flex-grow:1;height:10px) "
                              "(flex-grow:2;min-width:250px;height:10px))")
            << QList<float>{ 0, 0, 500, 10,   0, 0, 100, 10,   100, 0, 133.328f, 10,   233.328f, 0, 266.672f, 10 };
    QTest::newRow("wrap, each line grows") << 800.0f << 600.0f
            << QStringLiteral("(flex-wrap:wrap;width:300px;row-gap:4px| (flex-grow:1;flex-basis:120px;height:10px) "
                              "(flex-grow:1;flex-basis:120px;height:10px) (flex-grow:1;flex-basis:120px;"
                              "height:10px) (flex-grow:3;flex-basis:50px;height:10px))")
            << QList<float>{ 0, 0, 300, 24,   0, 0, 150, 10,   150, 0, 150, 10,   0, 14, 152.5f, 10,
                             152.5f, 14, 147.5f, 10 };
    QTest::newRow("sidebar and content") << 800.0f << 600.0f
            << QStringLiteral("(width:100%;height:100%| (flex-direction:column;width:200px;flex-shrink:0;"
                              "padding:8px;row-gap:4px| [60 40|] [70 50 30|] [40|]) (flex-direction:column;"
                              "flex-grow:1;min-width:0| (height:40px;flex-shrink:0;padding:0px 10px;"
                              "align-items:center| [80 60|flex-grow:1] (width:24px;height:24px)) "
                              "(flex-direction:column;flex-grow:1;overflow:hidden;padding:10px;row-gap:10px| [50 "
                              "50 50 50 50 50 50 50 50 50 50 50 50 50 50 50 50 50 50 50|] [40 40 40|])))")
            << QList<float>{ 0, 0, 800, 600,   0, 0, 200, 600,   8, 8, 184, 20,   8, 32, 184, 20,   8, 56, 184, 20,
                             200, 0, 600, 600,   0, 0, 600, 40,   10, 10, 556, 20,   566, 8, 24, 24,
                             0, 40, 600, 560,   10, 10, 580, 60,   10, 80, 580, 20 };
    QTest::newRow("nested row keeps its longest word") << 800.0f << 600.0f
            << QStringLiteral("(width:200px| (flex-grow:1| [300|]) (width:50px;height:10px))")
            << QList<float>{ 0, 0, 200, 20,   0, 0, 300, 20,   0, 0, 300, 20,   300, 0, 0, 10 };
    QTest::newRow("nested row with min-width 0") << 800.0f << 600.0f
            << QStringLiteral("(width:200px| (flex-grow:1;min-width:0px| [300|]) (width:50px;height:10px))")
            << QList<float>{ 0, 0, 200, 20,   0, 0, 171.422f, 20,   0, 0, 300, 20,   171.422f, 0, 28.578f, 10 };
    QTest::newRow("column, percentage basis of auto height") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:200px| (flex-basis:50%| [50 50 50 50|]) (height:30px))")
            << QList<float>{ 0, 0, 200, 70,   0, 0, 200, 40,   0, 0, 200, 40,   0, 40, 200, 30 };
    QTest::newRow("column shrinks to a min-height") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:100px;height:100px| (height:80px;min-height:60px) "
                              "(height:80px))")
            << QList<float>{ 0, 0, 100, 100,   0, 0, 100, 60,   0, 60, 100, 40 };
    QTest::newRow("margins in wrapped lines") << 800.0f << 600.0f
            << QStringLiteral("(flex-wrap:wrap;width:220px| (width:100px;height:20px;margin:5px) (width:100px;"
                              "height:30px;margin:5px 10px) (width:100px;height:10px;margin:0px 0px 0px auto))")
            << QList<float>{ 0, 0, 220, 70,   5, 5, 100, 20,   10, 35, 100, 30,   120, 30, 100, 10 };
    QTest::newRow("column auto margins") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:100px;height:200px| (height:30px;margin-top:auto) "
                              "(height:30px) (height:30px;margin-bottom:auto) (height:30px))")
            << QList<float>{ 0, 0, 100, 200,   0, 40, 100, 30,   0, 70, 100, 30,   0, 100, 100, 30,
                             0, 170, 100, 30 };
    QTest::newRow("absolute in column-reverse") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column-reverse;width:200px;height:200px;padding:10px;"
                              "align-items:center| (height:50px;width:50px) (position:absolute;width:40px;"
                              "height:40px))")
            << QList<float>{ 0, 0, 200, 200,   75, 140, 50, 50,   80, 150, 40, 40 };
    QTest::newRow("column space-between, auto height") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;justify-content:space-between;width:100px| (height:20px) "
                              "(height:30px))")
            << QList<float>{ 0, 0, 100, 50,   0, 0, 100, 20,   0, 20, 100, 30 };
    QTest::newRow("row-reverse wrap with gaps") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:row-reverse;flex-wrap:wrap;width:250px;column-gap:10px;row-gap:5px| "
                              "(width:100px;height:20px) (width:100px;height:20px) (width:100px;height:20px))")
            << QList<float>{ 0, 0, 250, 45,   150, 0, 100, 20,   40, 0, 100, 20,   150, 25, 100, 20 };
    QTest::newRow("absolute text, static centre") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;height:100px;justify-content:center| [60 60 60 60 60|position:absolute;"
                              "top:10px])")
            << QList<float>{ 0, 0, 300, 100,   0, 10, 300, 40 };
    QTest::newRow("scrolled column beside a growing item") << 800.0f << 600.0f
            << QStringLiteral("(width:400px;height:300px| (flex-direction:column;width:150px;overflow:auto| "
                              "(height:500px;flex-shrink:0)) (flex-grow:1))")
            << QList<float>{ 0, 0, 400, 300,   0, 0, 150, 300,   0, 0, 150, 500,   150, 0, 250, 300 };
    QTest::newRow("stretched lines with min and max") << 800.0f << 600.0f
            << QStringLiteral("(flex-wrap:wrap;width:200px;height:100px| (width:120px;max-height:30px) "
                              "(width:120px;min-height:70px))")
            << QList<float>{ 0, 0, 200, 100,   0, 0, 120, 15,   0, 15, 120, 85 };
    QTest::newRow("column of texts that shrink") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:100px;height:100px| [40 40 40 40 40 "
                              "40|overflow:hidden] [40 40 40 40 40 40|overflow:hidden])")
            << QList<float>{ 0, 0, 100, 100,   0, 0, 100, 50,   0, 50, 100, 50 };
    QTest::newRow("card grid") << 800.0f << 600.0f
            << QStringLiteral("(flex-wrap:wrap;width:520px;padding:10px;column-gap:10px;row-gap:10px;"
                              "align-content:flex-start| (flex-direction:column;width:160px;padding:8px;"
                              "border-width:1px| (height:90px) [70 50 80|margin-top:6px] [40 30|]) "
                              "(flex-direction:column;width:160px;padding:8px;border-width:1px| (height:90px) "
                              "[70|margin-top:6px]) (flex-direction:column;width:160px;padding:8px;"
                              "border-width:1px| (height:90px) [70 50 80 60 40 30|margin-top:6px]) "
                              "(flex-direction:column;width:160px;padding:8px;border-width:1px| (height:90px)))")
            << QList<float>{ 0, 0, 520, 332,   10, 10, 160, 194,   9, 9, 142, 90,   9, 105, 142, 40,
                             9, 145, 142, 20,   180, 10, 160, 194,   9, 9, 142, 90,   9, 105, 142, 20,
                             350, 10, 160, 194,   9, 9, 142, 90,   9, 105, 142, 80,   10, 214, 160, 108,
                             9, 9, 142, 90 };
    QTest::newRow("toolbar with spacer") << 800.0f << 600.0f
            << QStringLiteral("(width:500px;height:40px;align-items:center;padding:0px 8px;column-gap:4px| "
                              "(width:32px;height:32px) [60 40|] (flex-grow:1) (width:32px;height:32px) "
                              "(width:32px;height:32px;margin-left:12px))")
            << QList<float>{ 0, 0, 500, 40,   8, 4, 32, 32,   44, 10, 110, 20,   158, 20, 250, 0,   412, 4, 32, 32,
                             460, 4, 32, 32 };
    QTest::newRow("row in fit-content column") << 400.0f << 600.0f
            << QStringLiteral("(flex-direction:column;align-items:flex-start;padding:4px| (column-gap:6px| [30 30 "
                              "30 30 30 30 30 30|] (width:16px;height:16px;flex-shrink:0)) (width:50px;"
                              "height:10px))")
            << QList<float>{ 0, 0, 340, 38,   4, 4, 332, 20,   0, 0, 310, 20,   316, 0, 16, 16,   4, 24, 50, 10 };
    QTest::newRow("column grow in definite height") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:200px;height:400px| (height:50px) (flex-grow:1;"
                              "flex-direction:column| [40 40 40 40 40|]) (flex-grow:2) (height:30px;flex-shrink:0))")
            << QList<float>{ 0, 0, 200, 400,   0, 0, 200, 50,   0, 50, 200, 133.328f,   0, 0, 200, 40,
                             0, 183.328f, 200, 186.672f,   0, 370, 200, 30 };
    QTest::newRow("nested wrapping text column in row") << 800.0f << 600.0f
            << QStringLiteral("(width:500px;column-gap:10px;align-items:flex-start| (flex-direction:column;"
                              "flex-grow:1;flex-basis:0px| [50 50 50 50 50 50 50 50|] [20|]) "
                              "(flex-direction:column;flex-grow:2;flex-basis:0px| [90 90 90 90 90|]))")
            << QList<float>{ 0, 0, 500, 100,   0, 0, 163.328f, 100,   0, 0, 163.328f, 80,   0, 80, 163.328f, 20,
                             173.328f, 0, 326.672f, 40,   0, 0, 326.672f, 40 };
    QTest::newRow("max-width centred column") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:100%;align-items:center;padding:20px| "
                              "(flex-direction:column;width:100%;max-width:300px;row-gap:8px| [80 80 80 80 80|] "
                              "(height:40px)))")
            << QList<float>{ 0, 0, 800, 128,   250, 20, 300, 88,   0, 0, 300, 40,   0, 48, 300, 40 };
    QTest::newRow("percentage of a stretched column") << 800.0f << 600.0f
            << QStringLiteral("(height:200px| (flex-direction:column;width:100px| (height:50%;width:50%)))")
            << QList<float>{ 0, 0, 100, 200,   0, 0, 100, 200,   0, 0, 50, 100 };
    QTest::newRow("aspect ratio under a min-width") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;height:50px;align-items:flex-start| (aspect-ratio:2;width:20px;"
                              "min-width:40px) (aspect-ratio:2;height:10px;max-height:6px))")
            << QList<float>{ 0, 0, 300, 50,   0, 0, 40, 20,   40, 0, 12, 6 };
    QTest::newRow("column wrap with align-content and gaps") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;flex-wrap:wrap;height:100px;width:200px;"
                              "align-content:space-between;row-gap:10px;column-gap:5px| (width:40px;height:40px) "
                              "(width:50px;height:40px) (width:40px;height:40px) (width:30px;height:20px))")
            << QList<float>{ 0, 0, 200, 100,   0, 0, 40, 40,   0, 50, 50, 40,   160, 0, 40, 40,   160, 50, 30, 20 };
    QTest::newRow("auto margin in a wrapped line") << 800.0f << 600.0f
            << QStringLiteral("(flex-wrap:wrap;width:250px;justify-content:center| (width:100px;height:10px) "
                              "(width:100px;height:10px) (width:100px;height:10px;margin-left:auto))")
            << QList<float>{ 0, 0, 250, 20,   25, 0, 100, 10,   125, 0, 100, 10,   150, 10, 100, 10 };
    QTest::newRow("absolute text with right inset") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;height:100px;padding:5px| [40 40 40 40 40 40 40 40|position:absolute;"
                              "right:10px;bottom:0px])")
            << QList<float>{ 0, 0, 300, 100,   0, 60, 290, 40 };
    QTest::newRow("absolute text with left inset") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;height:100px| [40 40 40 40 40 40 40 40|position:absolute;left:150px])")
            << QList<float>{ 0, 0, 300, 100,   150, 0, 150, 60 };
    QTest::newRow("percentage basis in an auto row") << 800.0f << 600.0f
            << QStringLiteral("(| [40 40|flex-grow:1;flex-basis:0%] [60|flex-grow:1;flex-basis:0%])")
            << QList<float>{ 0, 0, 150, 40,   0, 0, 75, 40,   75, 0, 75, 40 };
    QTest::newRow("stretch with max-width in a column") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:300px| [50 50 50 50 50 50|max-width:120px] "
                              "(height:10px;max-width:50%) (height:10px;width:80%;align-self:center))")
            << QList<float>{ 0, 0, 300, 80,   0, 0, 120, 60,   0, 60, 150, 10,   30, 70, 240, 10 };
    QTest::newRow("padded bordered text shrinks") << 800.0f << 600.0f
            << QStringLiteral("(width:150px| [40 40 40 40|padding:5px;border-width:2px] (width:60px;height:10px))")
            << QList<float>{ 0, 0, 150, 54,   0, 0, 117.359f, 54,   117.359f, 0, 32.641f, 10 };
    QTest::newRow("column cannot shrink below wrapped rows") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:200px;height:50px| (| [50 50 50 50 50 50|]) "
                              "(height:40px))")
            << QList<float>{ 0, 0, 200, 50,   0, 0, 200, 40,   0, 0, 200, 40,   0, 40, 200, 10 };
    QTest::newRow("auto column with growing items") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:100px| (height:20px;flex-grow:1) (height:30px;"
                              "flex-grow:2))")
            << QList<float>{ 0, 0, 100, 50,   0, 0, 100, 20,   0, 20, 100, 30 };
    QTest::newRow("min-height above the row height") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;height:50px| (width:50px;min-height:80px) (width:50px))")
            << QList<float>{ 0, 0, 300, 50,   0, 0, 50, 80,   50, 0, 50, 50 };
    QTest::newRow("negative margins") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;height:100px| (width:100px;height:50px;margin:-10px -20px 0px 0px) "
                              "(width:100px;height:50px;margin-left:-30px) (width:100px;height:50px;"
                              "margin-top:-5px))")
            << QList<float>{ 0, 0, 300, 100,   0, -10, 100, 50,   50, 0, 100, 50,   150, -5, 100, 50 };
    QTest::newRow("column-reverse with auto margins") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column-reverse;width:100px;height:200px| (height:30px;"
                              "margin-bottom:auto) (height:40px))")
            << QList<float>{ 0, 0, 100, 200,   0, 40, 100, 30,   0, 0, 100, 40 };
    QTest::newRow("centred overflow in a column") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:100px;height:100px;justify-content:center| "
                              "(height:80px;flex-shrink:0) (height:60px;flex-shrink:0))")
            << QList<float>{ 0, 0, 100, 100,   0, -20, 100, 80,   0, 60, 100, 60 };
    QTest::newRow("wrapped lines centred") << 800.0f << 600.0f
            << QStringLiteral("(flex-wrap:wrap;width:200px;align-items:center| (width:90px;height:20px) "
                              "(width:90px;height:50px) (width:90px;height:30px) (width:90px;height:10px))")
            << QList<float>{ 0, 0, 200, 80,   0, 15, 90, 20,   90, 0, 90, 50,   0, 50, 90, 30,   90, 60, 90, 10 };
    QTest::newRow("gaps and space-between") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;column-gap:20px;justify-content:space-between| (width:50px;"
                              "height:10px) (width:50px;height:10px) (width:50px;height:10px))")
            << QList<float>{ 0, 0, 300, 10,   0, 0, 50, 10,   125, 0, 50, 10,   250, 0, 50, 10 };
    QTest::newRow("percentage min, max and margins") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:400px;height:300px| (min-height:20%;width:10%) "
                              "(max-width:50%;height:10px;margin:5% 10%) (height:10%;flex-shrink:0))")
            << QList<float>{ 0, 0, 400, 300,   0, 0, 40, 60,   40, 80, 200, 10,   0, 110, 400, 30 };
    QTest::newRow("row with a max-height stretches less") << 800.0f << 600.0f
            << QStringLiteral("(width:300px;max-height:40px| (width:50px;height:100px) (width:50px))")
            << QList<float>{ 0, 0, 300, 40,   0, 0, 50, 100,   50, 0, 50, 40 };
    QTest::newRow("flex base above the max") << 800.0f << 600.0f
            << QStringLiteral("(width:400px| (flex-basis:300px;max-width:100px;flex-grow:1;height:10px) "
                              "(flex-basis:100px;flex-grow:1;height:10px))")
            << QList<float>{ 0, 0, 400, 10,   0, 0, 100, 10,   100, 0, 300, 10 };
    QTest::newRow("text stretched up to a max-width") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:400px| [60 60 60 60 60 60|max-width:200px;padding:0px "
                              "10px])")
            << QList<float>{ 0, 0, 400, 60,   0, 0, 200, 60 };
    QTest::newRow("deep auto layout") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;padding:10px;row-gap:5px| (column-gap:5px| "
                              "(flex-direction:column| [30 30|] [50|]) (flex-direction:column;"
                              "justify-content:flex-end| (width:20px;height:5px))) (padding:3px;border-width:1px| "
                              "[100 100 100|]))")
            << QList<float>{ 0, 0, 348, 93,   10, 10, 328, 40,   0, 0, 70, 40,   0, 0, 70, 20,   0, 20, 70, 20,
                             75, 0, 20, 40,   0, 35, 20, 5,   10, 55, 328, 28,   4, 4, 320, 20 };
    QTest::newRow("auto root column, percentage width") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column| [40 40 40|] (width:50%;height:10px))")
            << QList<float>{ 0, 0, 140, 30,   0, 0, 140, 20,   0, 20, 70, 10 };
    QTest::newRow("auto root row, percentage margin") << 800.0f << 600.0f
            << QStringLiteral("(| (width:100px;height:10px) (width:20px;height:10px;margin-left:10%))")
            << QList<float>{ 0, 0, 120, 10,   0, 0, 90, 10,   102, 0, 18, 10 };
    QTest::newRow("outline blocks") << 800.0f << 600.0f
            << QStringLiteral("(flex-direction:column;width:300px;row-gap:2px| (column-gap:6px| (width:6px;"
                              "height:6px;margin-top:7px;flex-shrink:0) [60 40 70 50 30 80 40|]) (column-gap:6px;"
                              "padding-left:20px| (width:6px;height:6px;margin-top:7px;flex-shrink:0) [30|]))")
            << QList<float>{ 0, 0, 300, 62,   0, 0, 300, 40,   0, 7, 6, 6,   12, 0, 288, 40,   0, 42, 300, 20,
                             20, 7, 6, 6,   32, 0, 30, 20 };
    // clang-format on
}

void TestAtramentLayout::browserScenes()
{
    QFETCH(float, windowWidth);
    QFETCH(float, windowHeight);
    QFETCH(QString, scene);
    QFETCH(QList<float>, rects);
    LayoutEngine e;
    SceneParser parser(e, scene.toStdString());
    const NodeId root = parser.parse();
    QVERIFY2(parser.error.empty(), parser.error.c_str());
    QCOMPARE(qsizetype(parser.preorder.size() * 4), rects.size());
    e.compute(root, Size{windowWidth, windowHeight});
    for (std::size_t i = 0; i < parser.preorder.size(); ++i) {
        const auto at = qsizetype(i * 4);
        const QString m = mismatch(e.layout(parser.preorder[i]).rect,
                                   Rect::fromXYWH(rects[at], rects[at + 1], rects[at + 2], rects[at + 3]), 0.02f);
        QVERIFY2(m.isEmpty(), qPrintable(QStringLiteral("node %1 in document order: ").arg(i) + m));
    }
}

void TestAtramentLayout::tenThousandNodes()
{
    // A list of 2500 rows, each a row of an icon, a text that takes the rest and a trailing mark:
    // 10 001 nodes, one measured leaf a row.
    LayoutEngine e;
    std::vector<NodeId> rows;
    const auto build = [&e, &rows]() {
        e.clear();
        rows.clear();
        for (int i = 0; i < 2500; ++i) {
            const NodeId icon = e.newLeaf(S().size(24, 24).shrink(0));
            const NodeId label = e.newLeaf(S().grow(1), words({40, 55, 30, float(20 + i % 50)}));
            const NodeId trailing = e.newLeaf(S().size(16, 16).shrink(0));
            rows.push_back(node(e, S().row().pad(8).gap(0, 8).items(AlignItems::Center), {icon, label, trailing}));
        }
        return e.newNode(S().col().w(800), rows);
    };
    NodeId root = build();
    QCOMPARE(e.nodeCount(), std::size_t(10001));
    e.compute(root, Size{800, 600});
    CHECK_RECT(root, 0, 0, 800, 100000);
    CHECK_RECT(rows[0], 0, 0, 800, 40);
    CHECK_RECT(rows[2499], 0, 99960, 800, 40);
    CHECK_RECT(rows[2499] - 3, 8, 8, 24, 24);    // its icon
    CHECK_RECT(rows[2499] - 2, 40, 10, 728, 20); // its text
    CHECK_RECT(rows[2499] - 1, 776, 12, 16, 16);
    const std::size_t calls = e.measureCalls();

    const qint64 computeNs = brulion::test::fastestNs(10, [&]() { e.compute(root, Size{800, 600}); });
    const qint64 frameNs = brulion::test::fastestNs(10, [&]() {
        root = build();
        e.compute(root, Size{800, 600});
    });
    qInfo("10 001 nodes: compute %.3f ms, build + compute %.3f ms, %zu measure calls", double(computeNs) / 1e6,
          double(frameNs) / 1e6, calls);
#ifdef __OPTIMIZE__
    QVERIFY2(computeNs < 5'000'000LL * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(computeNs)));
#endif
}

QTEST_GUILESS_MAIN(TestAtramentLayout)
#include "tst_atrament_layout.moc"
