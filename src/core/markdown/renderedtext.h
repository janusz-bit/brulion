// RenderedText — a block's rendered text as plain text with style runs instead of HTML: what a
// C++ item lays out with QTextLayout and draws through the scene graph (docs/UI.md "Rendered text
// without rich text"). Part of the markdown module — see markdown/markdown.h for the module
// overview.
//
// It is the InlineRenderer's output taken apart: renderedTextFromHtml() reads the renderer's own
// HTML subset (InlineRenderer::render / renderBlock, the editor's task chips) into
//   * `text`     what Qt's rich text shows: entities decoded, `<br/>` as '\n', white space
//                collapsed as Qt's HTML import collapses it (a run of spaces is one, none at the
//                start of a line) — U+00A0 stays;
//   * `elements` every tag pair of the HTML (<b>, <a href …>, <span style …>, …) as a display
//                range with its attributes, in document order with its nesting depth;
//   * `runs`     the same flattened: maximal ranges with one look (style bits, colours, the kind
//                of background, the link, a page icon);
//   * `objects`  what is not text: a page's app icon (three no-break spaces reserved for it), a
//                rule;
//   * `source`   display ↔ source mapping (InlineRenderer::renderRuns/renderBlockRuns): the
//                verbatim text maps character by character, a construct (a link, a chip, an
//                escape) as a whole;
//   * `spellings` how the HTML wrote what is not its default spelling (`&nbsp;` for U+00A0, a
//                collapsed space), so that runsToHtml() gives the HTML back exactly.
// runsToHtml(renderedTextFromHtml(html)) == html for everything the renderer writes (the parity
// test: tst_markdown_renderparity runs the whole renderer test corpus through it). `valid` is
// false for HTML that is not well formed (a `<mark>` pair across emphasis: `<mark>a **b</mark>
// c**`); `drawable` is false for what the item does not draw (formulas with their own fonts, a
// rule, preformatted text): such a block keeps the rich-text path.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <utility>

namespace brulion::markdown {

/// One tag pair of the rendered HTML.
struct RenderedElement {
    enum class Tag : quint8 { Bold, Italic, Strike, Code, Sup, Sub, Span, Anchor, Pre };
    Tag tag = Tag::Span;
    int depth = 0;      ///< nesting level (0: outermost)
    qsizetype from = 0; ///< display range
    qsizetype to = 0;
    QString href;  ///< Anchor (decoded)
    QString name;  ///< Anchor: `name` (an app icon: "brulion-icon:<name>")
    QString style; ///< the `style` attribute as written ("" without one)
    bool operator==(const RenderedElement &) const = default;
};

/// How a run's background is drawn (docs/UI.md "The block editor": RichTextPills' kinds).
enum class RenderedBackground : quint8 {
    None,
    Pill, ///< a chip or a tag: a span with its own text colour (radius = height / 2)
    Box,  ///< inline code, a highlight: a small radius
};

/// What a run is, for its colour (the palette's role).
enum class RenderedRole : quint8 { Text, Link, Muted, Chip, Tag, Code, Highlight, Icon };

/// One run of `RenderedText::runs`: [from, to) of the text with one look.
struct RenderedRun {
    enum Style : quint16 {
        Bold = 1,
        Italic = 2,
        Strike = 4,
        Code = 8, ///< fixed pitch
        Sup = 16,
        Sub = 32,
        Underline = 64,
        Overline = 128,
        SmallCaps = 256,
        SansSerif = 512,
    };
    qsizetype from = 0;
    qsizetype to = 0;
    quint16 style = 0;
    RenderedRole role = RenderedRole::Text;
    RenderedBackground background = RenderedBackground::None;
    QString foreground;      ///< CSS colour ("" = the text's own)
    QString backgroundColor; ///< CSS colour ("" = none)
    QString href;            ///< the innermost link ("" = none)
    QString icon;            ///< a page's app icon drawn over this run ("" = none)
    /// The innermost tinted element (index in `elements`, −1: none): adjacent runs of one element
    /// are one shape.
    qsizetype tintElement = -1;
    bool operator==(const RenderedRun &) const = default;
};

/// Something drawn instead of text.
struct RenderedObject {
    enum class Kind : quint8 { Icon, Rule };
    Kind kind = Kind::Icon;
    qsizetype from = 0; ///< display range it covers (Icon: its no-break spaces; Rule: empty)
    qsizetype to = 0;
    QString name;  ///< Icon: the icon's name
    QString color; ///< Icon: the link's colour
    bool operator==(const RenderedObject &) const = default;
};

/// A display range and the source range it shows (positions in the text given to the renderer).
struct RenderedSourceSegment {
    qsizetype displayFrom = 0;
    qsizetype displayTo = 0;
    qsizetype sourceFrom = 0;
    qsizetype sourceTo = 0;
    /// Character by character (verbatim text); else the ranges map as wholes.
    bool linear() const { return displayTo - displayFrom == sourceTo - sourceFrom; }
    bool operator==(const RenderedSourceSegment &) const = default;
};

/// The HTML of display range [at, at + length) when it is not the default spelling (& < > " as
/// entities, '\n' as `<br/>`, anything else as itself); `events` orders an empty one among the
/// tags at its position (the tags opened and closed before it).
struct RenderedSpelling {
    qsizetype at = 0;
    qsizetype length = 0;
    QString html;
    qsizetype events = 0;
    bool operator==(const RenderedSpelling &) const = default;
};

/// An HTML range of the renderer's output and the source range it shows (InlineRenderer's traced
/// rendering); `renderedTextFromHtml` turns them into RenderedText::source.
struct HtmlSourceAnchor {
    qsizetype htmlFrom = 0;
    qsizetype htmlTo = 0;
    qsizetype sourceFrom = 0;
    qsizetype sourceTo = 0;
    bool operator==(const HtmlSourceAnchor &) const = default;
};

/// Rendered HTML with the source ranges of its pieces (InlineRenderer::renderTraced()). Pieces
/// put together with append() keep their anchors.
struct TracedHtml {
    QString html;
    QList<HtmlSourceAnchor> anchors;
    /// Markup without a source (a separator, a chip).
    void append(QStringView markup) { html += markup; }
    /// `other` after this, its source positions moved by `sourceShift`.
    void append(const TracedHtml &other, qsizetype sourceShift = 0);
};

struct RenderedText {
    QString text;
    QList<RenderedElement> elements;
    QList<RenderedRun> runs;
    QList<RenderedObject> objects;
    QList<RenderedSourceSegment> source;
    QList<RenderedSpelling> spellings;
    /// Per element: the attributes it was written with (bits: href 1, name 2, style 4), for
    /// runsToHtml() (an empty `href=""` is not a missing one).
    QList<quint8> attributes;
    bool valid = true;
    bool drawable = true;

    /// The source position shown at display position `display` (0 … text.size()): in verbatim
    /// text the character's own, in a construct its start (its end at the construct's end); −1
    /// without a source map.
    qsizetype sourceAt(qsizetype display) const;
    /// The display position of source position `source` (the first segment showing it; a position
    /// inside a construct: its start); −1 without a source map.
    qsizetype displayAt(qsizetype source) const;
    /// The display range showing source range [from, to): verbatim text exactly, constructs as
    /// wholes; {−1, −1} when nothing shown covers it.
    std::pair<qsizetype, qsizetype> displayRange(qsizetype from, qsizetype to) const;
    /// The run at display position `display` (nullptr: none).
    const RenderedRun *runAt(qsizetype display) const;

    bool operator==(const RenderedText &) const = default;
};

/// The renderer's HTML read into RenderedText (see above); `anchors` (from a traced render) give
/// the source map.
RenderedText renderedTextFromHtml(QStringView html, const QList<HtmlSourceAnchor> &anchors = {});

/// The HTML `text` was read from (exactly, for valid text).
QString runsToHtml(const RenderedText &text);

} // namespace brulion::markdown
