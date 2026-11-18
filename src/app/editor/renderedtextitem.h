// RenderedTextItem — a block's rendered text drawn from a cached QTextLayout through the scene
// graph (QSGTextNode), instead of a rich-text Text / TextEdit fed with HTML (a QTextDocument per
// row, RichTextPills, BTintShapes' Repeaters, RichTextLinks). Stage A of the editor's rewrite
// (docs/PERFORMANCE.md Z10; docs/UI.md "Rendered text without rich text").
//
// It shows a markdown::RenderedText (markdown/renderedtext.h) — the renderer's output as text with
// style runs: `renderedText` (BlockRowData.renderedText, with the source map) or `html` (the
// renderer's HTML, read into one; no source map). One QTextLayout with a format per run (bold,
// italic, strike, inline code's font, the run's colour) is laid out with the item's width, font,
// fixed line height (`lineHeight`: the baseline where QTextDocument's FixedHeight puts it, so it
// matches the rich-text path and the inline editor) and alignment, synchronously when a property
// changes (its implicitHeight is right as soon as the row is created: docs/UI.md "Scrolling
// performance" rule 1) and only then. The chips', tags', code's and highlights' rounded shapes are
// one geometry node under the text (RichTextPills' rules: pills for spans with their own text
// colour, boxes else; adjacent spans of one colour are one shape); the hovered link's underline and
// the find bar's matches are extra geometry over the same layout (no new layout); a page's app
// icon is a reserved advance whose place `iconShapes` gives (a Repeater of BIcon draws it).
//
//   RenderedTextItem { width: parent.width; renderedText: block.rowData.renderedText
//                      font.pixelSize: 15; lineHeight: 26; color: Theme.fg
//                      hoveredLink: root.hoveredLink; findQuery: find.appliedQuery }
//
// `maximumLineCount` cuts the text after that many lines and ends the last one with "…"
// (`truncated`). linkAt(x, y) is the link under a point ("" none), positionAt(x, y) the source
// position (display position without a source map), rectForSource(from, to) where a source range
// is shown. Accessibility: the item is StaticText with the plain text as its name (set by QML);
// with a screen reader on, BlockText keeps the rich-text path whose links are accessible items.
#pragma once

#include "markdown/renderedtext.h"
#include "richtextpills.h"

#include <QColor>
#include <QFont>
#include <QList>
#include <QQmlParserStatus>
#include <QQuickItem>
#include <QRectF>
#include <QTextLayout>

#include <memory>

namespace brulion::editor {

/// A shared, immutable RenderedText as a QML value (`renderedText`): copied by pointer.
struct RenderedTextRef {
    Q_GADGET
    /// The item can draw it (markdown::RenderedText::drawable); false for none.
    Q_PROPERTY(bool drawable READ drawable FINAL)
    /// The shown text (plain).
    Q_PROPERTY(QString text READ text FINAL)
    Q_PROPERTY(bool isNull READ isNull FINAL)
public:
    std::shared_ptr<const markdown::RenderedText> d;
    bool drawable() const { return d && d->valid && d->drawable; }
    QString text() const { return d ? d->text : QString(); }
    bool isNull() const { return !d; }
    bool operator==(const RenderedTextRef &other) const { return d == other.d || (d && other.d && *d == *other.d); }
};

class RenderedTextItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(brulion::editor::RenderedTextRef renderedText READ renderedText WRITE setRenderedText NOTIFY
                       renderedTextChanged FINAL)
    Q_PROPERTY(QString html READ html WRITE setHtml NOTIFY htmlChanged FINAL)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged FINAL)
    /// Links without a colour of their own (the rich text's palette link colour).
    Q_PROPERTY(QColor linkColor READ linkColor WRITE setLinkColor NOTIFY linkColorChanged FINAL)
    /// Fixed line height in pixels (0: the font's own line spacing).
    Q_PROPERTY(qreal lineHeight READ lineHeight WRITE setLineHeight NOTIFY lineHeightChanged FINAL)
    /// Qt::AlignLeft or Qt::AlignHCenter (QML Text.AlignLeft / Text.AlignHCenter).
    Q_PROPERTY(int horizontalAlignment READ horizontalAlignment WRITE setHorizontalAlignment NOTIFY
                       horizontalAlignmentChanged FINAL)
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY wrapChanged FINAL)
    Q_PROPERTY(
            int maximumLineCount READ maximumLineCount WRITE setMaximumLineCount NOTIFY maximumLineCountChanged FINAL)
    Q_PROPERTY(bool truncated READ truncated NOTIFY truncatedChanged FINAL)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY layoutChanged FINAL)
    /// The radius of a box (inline code, a highlight; Theme.radiusXs) and the inset of shapes.
    Q_PROPERTY(qreal boxRadius READ boxRadius WRITE setBoxRadius NOTIFY boxRadiusChanged FINAL)
    Q_PROPERTY(qreal inset READ inset WRITE setInset NOTIFY insetChanged FINAL)
    /// The link drawn underlined (as InlineRenderer::withHoveredLink underlines it): "" none.
    Q_PROPERTY(QString hoveredLink READ hoveredLink WRITE setHoveredLink NOTIFY hoveredLinkChanged FINAL)
    /// The find bar's matches (text/find.h) on a tint of `findColor`: "" none.
    Q_PROPERTY(QString findQuery READ findQuery WRITE setFindQuery NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchCase READ findMatchCase WRITE setFindMatchCase NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchDiacritics READ findMatchDiacritics WRITE setFindMatchDiacritics NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findWholeWord READ findWholeWord WRITE setFindWholeWord NOTIFY findChanged FINAL)
    Q_PROPERTY(QColor findColor READ findColor WRITE setFindColor NOTIFY findChanged FINAL)
    /// Where the page icons go (kind "icon", as RichTextPills' iconShapes): a Repeater of BIcon.
    Q_PROPERTY(QList<brulion::editor::RichTextPill> iconShapes READ iconShapes NOTIFY iconShapesChanged FINAL)
    /// The shown text (plain; the accessible name).
    Q_PROPERTY(QString plainText READ plainText NOTIFY layoutChanged FINAL)
    /// The shown text as the renderer's HTML with the hovered link underlined (what the rich-text
    /// path shows; for tools and the self-tests — made when read).
    Q_PROPERTY(QString text READ text NOTIFY textChanged FINAL)

public:
    explicit RenderedTextItem(QQuickItem *parent = nullptr);
    ~RenderedTextItem() override;

    RenderedTextRef renderedText() const { return m_ref; }
    void setRenderedText(const RenderedTextRef &text);
    QString html() const { return m_html; }
    void setHtml(const QString &html);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    QColor linkColor() const { return m_linkColor; }
    void setLinkColor(const QColor &color);
    qreal lineHeight() const { return m_lineHeight; }
    void setLineHeight(qreal px);
    int horizontalAlignment() const { return m_alignment; }
    void setHorizontalAlignment(int alignment);
    bool wrap() const { return m_wrap; }
    void setWrap(bool wrap);
    int maximumLineCount() const { return m_maxLines; }
    void setMaximumLineCount(int lines);
    bool truncated() const { return m_truncated; }
    int lineCount() const { return m_layout ? m_layout->lineCount() : 0; }
    qreal boxRadius() const { return m_boxRadius; }
    void setBoxRadius(qreal radius);
    qreal inset() const { return m_inset; }
    void setInset(qreal inset);
    QString hoveredLink() const { return m_hoveredLink; }
    void setHoveredLink(const QString &href);
    QString findQuery() const { return m_findQuery; }
    void setFindQuery(const QString &query);
    bool findMatchCase() const { return m_findCase; }
    void setFindMatchCase(bool on);
    bool findMatchDiacritics() const { return m_findDiacritics; }
    void setFindMatchDiacritics(bool on);
    bool findWholeWord() const { return m_findWholeWord; }
    void setFindWholeWord(bool on);
    QColor findColor() const { return m_findColor; }
    void setFindColor(const QColor &color);
    QList<RichTextPill> iconShapes() const { return m_iconShapes; }
    QString plainText() const;
    QString text() const;

    /// The link at (x, y) ("" none): the character under the point, not past a line's text nor
    /// above or below its glyphs (as the rich text's exact hit test).
    Q_INVOKABLE QString linkAt(qreal x, qreal y) const;
    /// The source position of (x, y) (the renderer's input; the display position without a source
    /// map); the nearest position between characters.
    Q_INVOKABLE int positionAt(qreal x, qreal y) const;
    /// The display position of (x, y) (between characters).
    Q_INVOKABLE int displayPositionAt(qreal x, qreal y) const;
    /// The bounding rectangle of where source range [from, to) is shown (empty: not shown).
    Q_INVOKABLE QRectF rectForSource(int from, int to) const;

    /// One shape under or over the text (tests): a tinted span's line segment, a find match, the
    /// hovered link's underline.
    struct Shape {
        enum class Kind { Pill, Box, Find, Underline };
        Kind kind = Kind::Box;
        QRectF rect;
        QColor color;
        bool operator==(const Shape &) const = default;
    };
    const QList<Shape> &shapes() const { return m_shapes; }
    const QList<Shape> &overlays() const { return m_overlays; }
    /// The laid-out text (tests); nullptr before the first layout.
    const QTextLayout *textLayout() const { return m_layout.get(); }
    const markdown::RenderedText &shown() const { return m_shown; }
    /// Layouts made so far (tests: a property that changes nothing lays nothing out).
    int layoutCount() const { return m_layoutCount; }

    void componentComplete() override;

Q_SIGNALS:
    void renderedTextChanged();
    void htmlChanged();
    void fontChanged();
    void colorChanged();
    void linkColorChanged();
    void lineHeightChanged();
    void horizontalAlignmentChanged();
    void wrapChanged();
    void maximumLineCountChanged();
    void truncatedChanged();
    void layoutChanged();
    void boxRadiusChanged();
    void insetChanged();
    void hoveredLinkChanged();
    void findChanged();
    void iconShapesChanged();
    void textChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    /// The text to show changed: the runs read, laid out again.
    void reload();
    /// Lays the text out with the current width (when complete), the shapes and overlays anew.
    void relayout();
    /// Lays `text` out (formats from the runs of `m_shown`) into m_layout; the lines' count.
    int layoutText(const QString &text);
    void computeShapes();
    void computeOverlays();
    /// The baseline-to-top offset of line `line` for the fixed line height.
    qreal lineTop(int index) const;
    /// The line whose box holds `y` (−1: none).
    int lineAtY(qreal y) const;
    /// One line's part of display range [from, to): RichTextPills' line segments.
    struct Segment {
        QTextLine line;
        qreal x1 = 0;
        qreal x2 = 0;
    };
    QList<Segment> segments(qsizetype from, qsizetype to) const;
    QFont runFont(const markdown::RenderedRun &run) const;

    RenderedTextRef m_ref;
    QString m_html;
    markdown::RenderedText m_shown; ///< what is shown (cut text: "…" at the end)
    qsizetype m_shownLength = 0;    ///< of m_shown.text taken from the source text (before "…")
    std::unique_ptr<QTextLayout> m_layout;
    QFont m_font;
    QColor m_color{Qt::black};
    QColor m_linkColor{Qt::blue};
    qreal m_lineHeight = 0;
    int m_alignment = Qt::AlignLeft;
    bool m_wrap = true;
    int m_maxLines = 0;
    bool m_truncated = false;
    qreal m_boxRadius = 3;
    qreal m_inset = 0;
    QString m_hoveredLink;
    QString m_findQuery;
    bool m_findCase = false;
    bool m_findDiacritics = false;
    bool m_findWholeWord = false;
    QColor m_findColor{0xf2, 0xb3, 0x3d, 0x80};
    QList<Shape> m_shapes;
    QList<Shape> m_overlays;
    QList<RichTextPill> m_iconShapes;
    qreal m_laidOutWidth = -1;
    qreal m_naturalWidth = 0;   ///< the widest line
    bool m_softWrapped = false; ///< a line was broken by the width (not by a line break)
    int m_layoutCount = 0;
    bool m_textDirty = true;   ///< the text node needs new glyphs
    bool m_shapesDirty = true; ///< the geometry nodes need new vertices
};

} // namespace brulion::editor
