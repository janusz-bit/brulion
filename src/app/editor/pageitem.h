// PageItem — a page's blocks as one item (Z10 stage C; docs/UI.md "One item per page (Z10 stage
// C)"): it lays out and draws only the rows on screen, from a cache of RenderedTextLayouts by block
// uid, with the rows' heights in a height sum tree and the scroll position as an anchor (PageRows:
// RowHeightTree + ScrollAnchor). Behind BRULION_PAGE_ITEM=1 (EditorInput.pageItem), off by default.
//
// It sits over a plain Flickable (`flickable`: kinetic scrolling, TouchFlick, SmoothScroll), fills
// its viewport and drives its contentHeight and contentY: the Flickable scrolls, the item reads
// its contentY into the anchor; heights changing above the anchor move contentY only while the
// Flickable stands still (a write stops a flick) — while it moves the difference is a slack,
// folded in when the movement ends.
//
// Rows it draws itself: paragraphs, headings and list items whose rendered text RenderedTextLayout
// can draw, with their bullets, numbers and guide lines (BlockDelegate's geometry, from the
// editor's metrics below). Every other row — code, tables, queries, frames, pictures, embedded
// pages, tasks, selected, folded or edited rows, the title's place swap — is hosted: a delegate
// (`delegate`, the editor's BlockDelegate) created for it with the row's roles as its required
// properties and a model object of its own (what BlockRowData reads), placed over the row while it
// is in or near the viewport (the edited row as long as it is edited or holds the keyboard).
//
//   Flickable { id: flick; anchors.fill: parent }
//   PageItem { anchors.fill: flick; flickable: flick; model: controller.blocks
//              delegate: Component { BlockDelegate { editorItem: editor } } … metrics … }
#pragma once

#include "pagerows.h"
#include "renderedtextitem.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QQuickItem>
#include <QSet>

#include <memory>

class QQmlComponent;
class QQmlPropertyMap;

namespace brulion::editor {

class BlockListModel;

class PageItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(brulion::editor::BlockListModel *model READ model WRITE setModel NOTIFY modelChanged FINAL)
    /// The Flickable that scrolls (its contentY and contentHeight are this item's).
    Q_PROPERTY(QQuickItem *flickable READ flickable WRITE setFlickable NOTIFY flickableChanged FINAL)
    /// The delegate of hosted rows (a BlockDelegate; required properties named as the model's roles).
    Q_PROPERTY(QQmlComponent *delegate READ delegate WRITE setDelegate NOTIFY delegateChanged FINAL)
    /// Shown above the blocks and below them (the page title, the room to click under the last block).
    Q_PROPERTY(QQuickItem *header READ header WRITE setHeader NOTIFY headerChanged FINAL)
    Q_PROPERTY(QQuickItem *footer READ footer WRITE setFooter NOTIFY footerChanged FINAL)

    // ---- The editor's metrics (BlockEditor / BlockDelegate / BlockText) ------------------------
    Q_PROPERTY(bool documentMode READ documentMode WRITE setDocumentMode NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal contentX READ contentX WRITE setContentX NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal contentWidth READ contentWidth WRITE setContentWidth NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal gutter READ gutter WRITE setGutter NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal indentStep READ indentStep WRITE setIndentStep NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal bulletColumn READ bulletColumn WRITE setBulletColumn NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal listIndent READ listIndent WRITE setListIndent NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal rowPad READ rowPad WRITE setRowPad NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal paragraphGap READ paragraphGap WRITE setParagraphGap NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal listGap READ listGap WRITE setListGap NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal headingGapBelow READ headingGapBelow WRITE setHeadingGapBelow NOTIFY metricsChanged FINAL)
    /// By heading level 0 … 6 (0: body text): BlockEditor.headingGapAbove(), headingSize(),
    /// headingLineHeight(), firstLineCenter().
    Q_PROPERTY(
            QList<qreal> headingGapsAbove READ headingGapsAbove WRITE setHeadingGapsAbove NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QList<int> fontSizes READ fontSizes WRITE setFontSizes NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QList<int> lineHeights READ lineHeights WRITE setLineHeights NOTIFY metricsChanged FINAL)
    Q_PROPERTY(
            QList<qreal> firstLineCenters READ firstLineCenters WRITE setFirstLineCenters NOTIFY metricsChanged FINAL)
    /// Theme.space1 … Theme.space6.
    Q_PROPERTY(QList<qreal> spaces READ spaces WRITE setSpaces NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY metricsChanged FINAL)
    Q_PROPERTY(int fontWeight READ fontWeight WRITE setFontWeight NOTIFY metricsChanged FINAL)
    Q_PROPERTY(int headingWeight READ headingWeight WRITE setHeadingWeight NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QColor mutedColor READ mutedColor WRITE setMutedColor NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QColor guideColor READ guideColor WRITE setGuideColor NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QColor linkColor READ linkColor WRITE setLinkColor NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal boxRadius READ boxRadius WRITE setBoxRadius NOTIFY metricsChanged FINAL)
    /// The leading "# Title" drawn above the properties (BlockEditor.titleFirst): that row, the one
    /// before and the one after are hosted (their delegates trade places). −1: none.
    Q_PROPERTY(int titleRow READ titleRow WRITE setTitleRow NOTIFY metricsChanged FINAL)
    // The find bar's matches (NoteFind), drawn over the rows' text.
    Q_PROPERTY(QString findQuery READ findQuery WRITE setFindQuery NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchCase READ findMatchCase WRITE setFindMatchCase NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchDiacritics READ findMatchDiacritics WRITE setFindMatchDiacritics NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findWholeWord READ findWholeWord WRITE setFindWholeWord NOTIFY findChanged FINAL)
    Q_PROPERTY(QColor findColor READ findColor WRITE setFindColor NOTIFY findChanged FINAL)

    // ---- What it shows (tests, the bench probe) -------------------------------------------------
    /// The page's height: the header, the rows (measured or estimated) and the footer.
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged FINAL)
    /// The viewport's top edge in the page (the anchor's y).
    Q_PROPERTY(qreal viewY READ viewY NOTIFY viewYChanged FINAL)
    /// Rows drawn by the item / hosted by a delegate in the last frame, the model rows shown first
    /// and last (−1: none).
    Q_PROPERTY(int drawnRowCount READ drawnRowCount NOTIFY shownChanged FINAL)
    Q_PROPERTY(int hostedRowCount READ hostedRowCount NOTIFY shownChanged FINAL)
    Q_PROPERTY(int firstShownRow READ firstShownRow NOTIFY shownChanged FINAL)
    Q_PROPERTY(int lastShownRow READ lastShownRow NOTIFY shownChanged FINAL)
    /// Rows measured / text layouts made so far (tests: a width change lays out what is on screen).
    Q_PROPERTY(int measuredRows READ measuredRows NOTIFY shownChanged FINAL)
    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY shownChanged FINAL)
    /// The link under the pointer (setPointer(); "" none), drawn underlined.
    Q_PROPERTY(QString hoveredLink READ hoveredLink NOTIFY hoveredLinkChanged FINAL)

public:
    explicit PageItem(QQuickItem *parent = nullptr);
    ~PageItem() override;

    BlockListModel *model() const { return m_model.data(); }
    void setModel(BlockListModel *model);
    QQuickItem *flickable() const { return m_flickable.data(); }
    void setFlickable(QQuickItem *flickable);
    QQmlComponent *delegate() const { return m_delegate.data(); }
    void setDelegate(QQmlComponent *delegate);
    QQuickItem *header() const { return m_header.data(); }
    void setHeader(QQuickItem *item);
    QQuickItem *footer() const { return m_footer.data(); }
    void setFooter(QQuickItem *item);

    bool documentMode() const { return m_doc; }
    void setDocumentMode(bool on);
    qreal contentX() const { return m_contentX; }
    void setContentX(qreal x);
    qreal contentWidth() const { return m_contentWidth; }
    void setContentWidth(qreal w);
    qreal gutter() const { return m_gutter; }
    void setGutter(qreal v);
    qreal indentStep() const { return m_indentStep; }
    void setIndentStep(qreal v);
    qreal bulletColumn() const { return m_bulletColumn; }
    void setBulletColumn(qreal v);
    qreal listIndent() const { return m_listIndent; }
    void setListIndent(qreal v);
    qreal rowPad() const { return m_rowPad; }
    void setRowPad(qreal v);
    qreal paragraphGap() const { return m_paragraphGap; }
    void setParagraphGap(qreal v);
    qreal listGap() const { return m_listGap; }
    void setListGap(qreal v);
    qreal headingGapBelow() const { return m_headingGapBelow; }
    void setHeadingGapBelow(qreal v);
    QList<qreal> headingGapsAbove() const { return m_headingGapsAbove; }
    void setHeadingGapsAbove(const QList<qreal> &v);
    QList<int> fontSizes() const { return m_fontSizes; }
    void setFontSizes(const QList<int> &v);
    QList<int> lineHeights() const { return m_lineHeights; }
    void setLineHeights(const QList<int> &v);
    QList<qreal> firstLineCenters() const { return m_firstLineCenters; }
    void setFirstLineCenters(const QList<qreal> &v);
    QList<qreal> spaces() const { return m_spaces; }
    void setSpaces(const QList<qreal> &v);
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &v);
    int fontWeight() const { return m_fontWeight; }
    void setFontWeight(int v);
    int headingWeight() const { return m_headingWeight; }
    void setHeadingWeight(int v);
    QColor textColor() const { return m_textColor; }
    void setTextColor(const QColor &c);
    QColor mutedColor() const { return m_mutedColor; }
    void setMutedColor(const QColor &c);
    QColor guideColor() const { return m_guideColor; }
    void setGuideColor(const QColor &c);
    QColor linkColor() const { return m_linkColor; }
    void setLinkColor(const QColor &c);
    qreal boxRadius() const { return m_boxRadius; }
    void setBoxRadius(qreal v);
    int titleRow() const { return m_titleRow; }
    void setTitleRow(int row);
    QString findQuery() const { return m_findQuery; }
    void setFindQuery(const QString &q);
    bool findMatchCase() const { return m_findCase; }
    void setFindMatchCase(bool on);
    bool findMatchDiacritics() const { return m_findDiacritics; }
    void setFindMatchDiacritics(bool on);
    bool findWholeWord() const { return m_findWholeWord; }
    void setFindWholeWord(bool on);
    QColor findColor() const { return m_findColor; }
    void setFindColor(const QColor &c);

    qreal contentHeight() const { return m_rows.heights().total(); }
    qreal viewY() const { return m_rows.view().y(); }
    int drawnRowCount() const { return m_drawnCount; }
    int hostedRowCount() const { return int(m_hosted.size()); }
    int firstShownRow() const { return m_firstShown; }
    int lastShownRow() const { return m_lastShown; }
    int measuredRows() const { return m_rows.heights().measuredCount(); }
    int layoutCount() const { return m_layoutCount; }
    QString hoveredLink() const { return m_hoveredLink; }

    /// The model row at (x, y) of the item (−1: the header, the footer, nothing).
    Q_INVOKABLE int rowAt(qreal x, qreal y) const;
    /// Where model row `row` is, in the item's coordinates (its height an estimate until it is laid
    /// out); empty for an invalid row.
    Q_INVOKABLE QRectF rowRect(int row) const;
    /// The hosted delegate of model row `row` (null: drawn by the item, or not near the view).
    Q_INVOKABLE QQuickItem *rowItem(int row) const;
    /// The link at (x, y) of the item on a row the item draws ("" none).
    Q_INVOKABLE QString linkAt(qreal x, qreal y) const;
    /// (x, y) of the item in the coordinates of row `row`'s text (BlockText's: what editAt() takes).
    Q_INVOKABLE QPointF textPoint(int row, qreal x, qreal y) const;
    /// The plain text the item draws for model row `row` ("" when it does not draw it now).
    Q_INVOKABLE QString rowText(int row) const;
    /// The lines of model row `row`'s text as laid out (0: not drawn by the item now).
    Q_INVOKABLE int rowLineCount(int row) const;
    /// The pointer at (x, y) (hover; a negative x: gone): the link under it is underlined.
    Q_INVOKABLE void setPointer(qreal x, qreal y);
    /// "Scroll to block": model row `row` placed as ListView.positionViewAtIndex() places it
    /// (ListView.Beginning, Center, End; any other mode: Contain), exactly, in this frame's polish.
    Q_INVOKABLE void positionAtRow(int row, int mode);
    /// positionAtRow(row, Contain).
    Q_INVOKABLE void ensureRowVisible(int row) { positionAtRow(row, 3); }
    /// The page's top (the header) at the top edge.
    Q_INVOKABLE void positionAtBeginning();
    /// Scrolls so that [y, y + height) of the item (the caret of a hosted row's editor) is shown,
    /// `margin` px inside the edges; nothing when it is.
    Q_INVOKABLE void ensureVisible(qreal y, qreal height, qreal margin);
    /// ensureVisible() for `rect` of `item` (a hosted row's inline editor: its caret).
    Q_INVOKABLE void ensureItemVisible(QQuickItem *item, const QRectF &rect, qreal margin);
    /// Model row `row` hosted by a delegate from now on, also while the item could draw it (tests:
    /// a self-test inspecting a delegate's insides; −1 none).
    Q_INVOKABLE void hostRow(int row);
    /// Lays out what the next frame shows now (tests; the next polish does it anyway).
    Q_INVOKABLE void forceLayout();

    const PageRows &rows() const { return m_rows; }

Q_SIGNALS:
    void modelChanged();
    void flickableChanged();
    void delegateChanged();
    void headerChanged();
    void footerChanged();
    void metricsChanged();
    void findChanged();
    void contentHeightChanged();
    void viewYChanged();
    void shownChanged();
    void hoveredLinkChanged();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private Q_SLOTS:
    void flickMoved();
    void flickMovementEnded();

private:
    struct Row;
    struct Hosted;
    struct Decoration {
        QRectF rect;
        QColor color;
        bool round = false;
    };

    /// The model rows changed (inserted, removed, moved, reset): hosted delegates' indices.
    void rowsChanged();
    void rowDataChanged(int first, int last, const QList<int> &roles);
    /// A metric changed: every row is laid out again when next shown.
    void metricsTouched();
    /// An estimate of model row `row`'s height before its layout.
    qreal estimate(int row) const;
    /// The row is drawn by the item (else hosted).
    bool drawable(int row) const;
    /// The cache entry of model row `row`, laid out at the current metrics (made when needed).
    Row *layoutRow(int row);
    /// The hosted delegate of model row `row` (made when needed); null without a delegate.
    QQuickItem *hostedItem(int row);
    void releaseHosted(quint64 uid);
    /// The tree's y → the Flickable (contentHeight, contentY when it stands still).
    void syncFlickable();
    qreal at(const QList<qreal> &list, int index, qreal fallback) const;
    int atInt(const QList<int> &list, int index, int fallback) const;
    qreal space(int k) const { return at(m_spaces, k - 1, 4.0 * k); }
    qreal topPad(int row, int lvl, bool listItem, const QString &prevKind) const;
    qreal bottomPad(int lvl, bool listItem) const;
    qreal levelX(bool listItem, int depth) const;
    qreal markerX(int lvl) const;

    QPointer<BlockListModel> m_model;
    QPointer<QQuickItem> m_flickable;
    QPointer<QQmlComponent> m_delegate;
    QPointer<QQuickItem> m_header;
    QPointer<QQuickItem> m_footer;
    PageRows m_rows;

    QHash<quint64, Row *> m_cache;     ///< by uid (owned)
    QHash<quint64, Hosted *> m_hosted; ///< by uid (owned)
    QSet<quint64> m_forceHosted;
    QList<QSGNode *> m_deadNodes; ///< nodes of rows dropped, deleted in the next updatePaintNode()
    quint64 m_frame = 0;
    quint64 m_serial = 0; ///< decoration versions (unique across entries made again for a uid)
    int m_drawnCount = 0;
    int m_firstShown = -1;
    int m_lastShown = -1;
    int m_layoutCount = 0;
    qreal m_slack = 0;
    quint64 m_positionUid = 0; ///< positionAtRow()'s row, placed again once measured (polish)
    int m_positionMode = 0;
    bool m_writing = false;
    bool m_reflow = false; ///< the width changed: rows measured again are reflows
    QString m_hoveredLink;
    quint64 m_hoveredUid = 0;

    bool m_doc = false;
    qreal m_contentX = 0;
    qreal m_contentWidth = 600;
    qreal m_gutter = 36;
    qreal m_indentStep = 26;
    qreal m_bulletColumn = 20;
    qreal m_listIndent = 26;
    qreal m_rowPad = 2;
    qreal m_paragraphGap = 12;
    qreal m_listGap = 2;
    qreal m_headingGapBelow = 10;
    QList<qreal> m_headingGapsAbove;
    QList<int> m_fontSizes;
    QList<int> m_lineHeights;
    QList<qreal> m_firstLineCenters;
    QList<qreal> m_spaces;
    QString m_fontFamily;
    int m_fontWeight = 400;
    int m_headingWeight = 600;
    QColor m_textColor{Qt::black};
    QColor m_mutedColor{Qt::gray};
    QColor m_guideColor{Qt::lightGray};
    QColor m_linkColor{Qt::blue};
    qreal m_boxRadius = 3;
    int m_titleRow = -1;
    QString m_findQuery;
    bool m_findCase = false;
    bool m_findDiacritics = false;
    bool m_findWholeWord = false;
    QColor m_findColor{0xf2, 0xb3, 0x3d, 0x80};
};

} // namespace brulion::editor
