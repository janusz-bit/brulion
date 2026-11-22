// JournalDayItem — a day of the journal stream that is not being edited, drawn by one C++ item
// instead of one QML BlockDelegate per block (docs/PERFORMANCE.md Z10 stage A2; docs/UI.md "A
// journal day as one item"). Behind BRULION_JOURNAL_DAY_ITEM=1 (EditorInput.journalDayItem).
//
// It reads the day's BlockListModel and draws what a plain block shows — the indentation's guide
// lines, the fold arrows (on hover; always when folded or on touch UIs), bullets with their halo,
// the task boxes, a document's list numbers, headings, quotes' bars, rules — with the block's
// rendered text in a RenderedTextItem child per block (stage A's item, created from C++: no QML
// object per block). Every number is the BlockDelegate's (the same formulas, from the day's
// BlockEditor's geometry, which the QML binds in): a row's height, its text's width and place, its
// first line's centre, so a day switched to the editor's rows keeps every pixel where it was.
//
//   JournalDayItem { model: page.blocks; blocked: page.editingRow >= 0 || …
//                    contentX: dayEditor.contentX; contentWidth: dayEditor.contentWidth; …
//                    fontSizes: [dayEditor.headingSize(0), …]; textColor: Theme.fg; … }
//
// `drawable` is false while a shown block is one it does not draw (a table, code, a picture, an
// embedded page, properties, a callout, a formula with fonts of its own, …), a block is edited or
// selected, or `blocked` (zoom, a conflict, a title above the properties): the day then shows its
// editor's rows (ContentJournalDay.materialize()). The QML around it turns input into the editor's
// own calls once the rows exist (partAt(), linkAt(), the geometry below); hover is drawn here
// (setHover(): the fold arrow of the row under the pointer, the bullet's halo, a task box's tint,
// the hovered link's underline in its text).
#pragma once

#include "renderedtextitem.h"

#include <QColor>
#include <QList>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace brulion::editor {

class BlockListModel;

class JournalDayItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(brulion::editor::BlockListModel *model READ model WRITE setModel NOTIFY modelChanged FINAL)
    /// The day is in a state the item does not show (editing, a selection, a zoom, a conflict, a
    /// title drawn above the properties): `drawable` is false.
    Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY blockedChanged FINAL)
    /// Every shown block is one the item draws (and the day is not `blocked`).
    Q_PROPERTY(bool drawable READ drawable NOTIFY drawableChanged FINAL)
    /// The blocks' height (the sum of the rows' heights, as the editor's list's content without its
    /// header and footer); also the implicit height.
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged FINAL)
    /// Rows drawn (shown blocks), text items made so far (tests, measurements).
    Q_PROPERTY(int shownRows READ shownRows NOTIFY contentHeightChanged FINAL)

    // ---- The day's BlockEditor's geometry (its properties and functions, bound in QML) --------
    Q_PROPERTY(bool documentMode MEMBER m_documentMode NOTIFY metricsChanged FINAL)
    Q_PROPERTY(bool readOnly MEMBER m_readOnly NOTIFY metricsChanged FINAL)
    Q_PROPERTY(bool touchUi MEMBER m_touchUi NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal contentX MEMBER m_contentX NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal contentWidth MEMBER m_contentWidth NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal gutter MEMBER m_gutter NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal indentStep MEMBER m_indentStep NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal bulletColumn MEMBER m_bulletColumn NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal arrowColumn MEMBER m_arrowColumn NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal listIndent MEMBER m_listIndent NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal taskBoxWidth MEMBER m_taskBoxWidth NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal paragraphGap MEMBER m_paragraphGap NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal listGap MEMBER m_listGap NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal headingGapBelow MEMBER m_headingGapBelow NOTIFY metricsChanged FINAL)
    /// BlockEditor.headingGapAbove(level), headingSize(level), headingLineHeight(level) and
    /// firstLineCenter(level) for the levels 0 (body) … 6.
    Q_PROPERTY(QList<qreal> headingGapsAbove MEMBER m_headingGapsAbove NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QList<qreal> fontSizes MEMBER m_fontSizes NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QList<qreal> lineHeights MEMBER m_lineHeights NOTIFY metricsChanged FINAL)
    Q_PROPERTY(QList<qreal> firstLineCenters MEMBER m_firstLineCenters NOTIFY metricsChanged FINAL)
    /// Theme.space1 … Theme.space6 (the delegates' paddings).
    Q_PROPERTY(QList<qreal> spaces MEMBER m_spaces NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal touchTarget MEMBER m_touchTarget NOTIFY metricsChanged FINAL)
    Q_PROPERTY(qreal iconSize MEMBER m_iconSize NOTIFY metricsChanged FINAL)   ///< Theme.iconSizeSm
    Q_PROPERTY(qreal boxRadius MEMBER m_boxRadius NOTIFY metricsChanged FINAL) ///< Theme.radiusXs
    Q_PROPERTY(QString fontFamily MEMBER m_fontFamily NOTIFY metricsChanged FINAL)
    Q_PROPERTY(int semiBoldWeight MEMBER m_semiBoldWeight NOTIFY metricsChanged FINAL)
    Q_PROPERTY(int normalWeight MEMBER m_normalWeight NOTIFY metricsChanged FINAL)
    /// The strip under the blocks (BlockEditor.bottomSpace): partAt() says "footer" there.
    Q_PROPERTY(qreal footerHeight MEMBER m_footerHeight NOTIFY metricsChanged FINAL)

    // ---- Colours (Theme) ---------------------------------------------------------------------
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor mutedColor MEMBER m_mutedColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor faintColor MEMBER m_faintColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor linkColor MEMBER m_linkColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor guideColor MEMBER m_guideColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor hoverColor MEMBER m_hoverColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor pressedColor MEMBER m_pressedColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor successColor MEMBER m_successColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor warningColor MEMBER m_warningColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor warningTextColor MEMBER m_warningTextColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor controlBorderColor MEMBER m_controlBorderColor NOTIFY colorsChanged FINAL)
    /// A done task's check (Theme.dark ? Theme.bg : white).
    Q_PROPERTY(QColor checkColor MEMBER m_checkColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor quoteBarColor MEMBER m_quoteBarColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor ruleColor MEMBER m_ruleColor NOTIFY colorsChanged FINAL)

    // ---- The find bar's matches (as BlockText gives them to its RenderedTextItem) --------------
    Q_PROPERTY(QString findQuery MEMBER m_findQuery NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchCase MEMBER m_findCase NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchDiacritics MEMBER m_findDiacritics NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findWholeWord MEMBER m_findWholeWord NOTIFY findChanged FINAL)
    Q_PROPERTY(QColor findColor MEMBER m_findColor NOTIFY findChanged FINAL)

    // ---- Hover (setHover) ----------------------------------------------------------------------
    /// The link drawn underlined in the hovered row's text ("" none; BlockText's hoveredLink rule).
    Q_PROPERTY(QString hoveredLink READ hoveredLink WRITE setHoveredLink NOTIFY hoverChanged FINAL)
    Q_PROPERTY(int hoveredRow READ hoveredRow NOTIFY hoverChanged FINAL)
    Q_PROPERTY(QString hoveredPart READ hoveredPart NOTIFY hoverChanged FINAL)

public:
    explicit JournalDayItem(QQuickItem *parent = nullptr);
    ~JournalDayItem() override;

    BlockListModel *model() const { return m_model; }
    void setModel(BlockListModel *model);
    bool blocked() const { return m_blocked; }
    void setBlocked(bool blocked);
    bool drawable() const { return m_drawable; }
    qreal contentHeight() const { return m_contentHeight; }
    int shownRows() const;
    QString hoveredLink() const { return m_hoveredLink; }
    void setHoveredLink(const QString &link);
    int hoveredRow() const { return m_hoverRow; }
    QString hoveredPart() const { return m_hoverPart; }

    /// The row (model row) whose box holds `y`, -1 above, below or between the shown rows.
    Q_INVOKABLE int rowAt(qreal y) const;
    /// What is under (x, y), topmost first as in a BlockDelegate: "text" (the block's text line
    /// boxes), "dateButton" (an open task's "Add date" while its row is hovered), "taskBox",
    /// "number", "bullet", "arrow" (a fold arrow, where shown), "row" (elsewhere in a row), "footer"
    /// (the strip under the blocks) or "" (nothing).
    Q_INVOKABLE QString partAt(qreal x, qreal y) const;
    /// The link under (x, y) of a row's text ("" none), as RenderedTextItem::linkAt.
    Q_INVOKABLE QString linkAt(qreal x, qreal y) const;
    /// The source position in the block's text at (x, y) (RenderedTextItem::positionAt; -1: no text).
    Q_INVOKABLE int textPositionAt(qreal x, qreal y) const;
    /// The pointer moved to (x, y) / left: the row, part and their hover look.
    Q_INVOKABLE void setHover(qreal x, qreal y);
    Q_INVOKABLE void clearHover();
    /// A row's geometry in item coordinates (the BlockDelegate's, with the list's content at the
    /// item's origin): y, height, topPad, firstLineCenter (from the row's top), textX, textY,
    /// textWidth, lines, bulletX, levelX, contentRight, dateButtonRoom, shown; {} for a bad row.
    Q_INVOKABLE QVariantMap rowGeometry(int row) const;
    /// rowGeometry()'s values typed (compiled QML): a row's top and height in item coordinates, its
    /// first line's centre from its top, its text's place and width.
    Q_INVOKABLE qreal rowY(int row) const;
    Q_INVOKABLE qreal rowHeight(int row) const;
    Q_INVOKABLE qreal firstLineCenterOf(int row) const;
    Q_INVOKABLE QRectF textRectOf(int row) const;
    /// The hit area of a part of a row (item coordinates; see partAt(); empty: none shown).
    Q_INVOKABLE QRectF partRect(int row, const QString &part) const;
    /// Where a marker's own handler opens its menu (the BlockDelegate's coordinates: from the
    /// row's top-left): "bullet" and "number" under them, "taskBox" under the box.
    Q_INVOKABLE QPointF menuPoint(int row, const QString &part) const;
    /// The room of an open task's "Add date" button (item coordinates; empty: none).
    Q_INVOKABLE QRectF dateButtonArea(int row) const;
    /// The task of `row` has a date: its button adds a reminder or an alarm ("+").
    Q_INVOKABLE bool addableAt(int row) const;
    /// The text item of a row (null when it has none): its positions and links (tests).
    Q_INVOKABLE brulion::editor::RenderedTextItem *textItemOf(int row) const;
    /// Layouts of the rows' texts made so far (tests: a change that moves nothing lays nothing out).
    Q_INVOKABLE int textLayouts() const;

    void componentComplete() override;

    /// A row as the item draws it (tests read rows()).
    struct Row {
        bool shown = false;
        QString view;
        QString kind;
        QString taskStatus;
        QString listMarker;
        QString prevKind;
        bool taskDated = false;
        bool hasChildren = false;
        bool collapsed = false;
        int depth = 0;
        int headingLevel = 0;
        RenderedTextRef text;
        // Geometry (item coordinates; the row's own values relative to its top).
        qreal y = 0;
        qreal height = 0;
        qreal topPad = 0;
        qreal contentHeight = 0;
        qreal firstLineCenter = 0;
        qreal levelX = 0;
        qreal bulletX = 0;
        qreal contentLeft = 0;
        qreal contentRight = 0;
        qreal dateButtonRoom = 0;
        qreal textX = 0;
        qreal textWidth = 0;
        int level = 0; ///< shownHeadingLevel
        int lines = 0;
        RenderedTextItem *textItem = nullptr;
        RenderedTextItem *numberItem = nullptr;
    };
    const std::vector<Row> &rows() const { return m_rows; }

Q_SIGNALS:
    void modelChanged();
    void blockedChanged();
    void drawableChanged();
    void contentHeightChanged();
    void metricsChanged();
    void colorsChanged();
    void findChanged();
    void hoverChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    /// The rows read from the model again (what they show), then laid out.
    void reload();
    /// The rows' geometry and texts laid out with the current metrics and width.
    void layout();
    /// The decorations (guides, bullets, boxes, arrows, bars) computed again and redrawn.
    void decorate();
    void updateDrawable(bool drawable);
    void releaseTexts();
    /// A text item no row draws any more: out of the scene at once, deleted later — never right
    /// away, as QML may hold it (textItemOf()) in a compiled function's locals, which the garbage
    /// collector reads until that function returns (a freed one crashed the self-tests).
    static void discardText(RenderedTextItem *&item);
    void applyTextLook(Row &row, RenderedTextItem *text) const;
    void applyFind(RenderedTextItem *text) const;
    qreal level(const QList<qreal> &list, int level, qreal fallback) const;
    qreal space(int n) const;
    qreal markerX(int level) const;
    bool bulletShown(const Row &row) const;
    bool taskShown(const Row &row) const;
    bool numberShown(const Row &row) const;
    bool arrowShown(const Row &row) const;
    /// Hit rectangles (item coordinates) of a row's parts.
    QRectF arrowRect(const Row &row) const;
    QRectF arrowHitRect(const Row &row) const;
    QRectF bulletRect(const Row &row) const;
    QRectF taskRect(const Row &row) const;
    QRectF numberHitRect(const Row &row) const;
    QRectF dateButtonRect(const Row &row) const;
    QRectF textRect(const Row &row) const;
    int rowIndexAt(qreal y) const;

public:
    /// One thing drawn under the texts: a rectangle or a picture (sprite).
    struct Decoration {
        enum class Kind { Rect, Sprite };
        Kind kind = Kind::Rect;
        QRectF rect;
        QColor color;
        QString sprite; ///< the sprite's key (spriteImage())
    };
    const QList<Decoration> &decorations() const { return m_decorations; }
    /// A sprite's picture at `dpr` (cached; keys from the item: "dot:…", "box:…", "icon:…").
    static QImage spriteImage(const QString &key, qreal dpr);

private:
    QPointer<BlockListModel> m_model;
    std::vector<Row> m_rows;
    bool m_blocked = false;
    bool m_drawable = true;
    qreal m_contentHeight = 0;
    bool m_reloading = false;

    bool m_documentMode = false;
    bool m_readOnly = false;
    bool m_touchUi = false;
    qreal m_contentX = 0;
    qreal m_contentWidth = 120;
    qreal m_gutter = 0;
    qreal m_indentStep = 26;
    qreal m_bulletColumn = 20;
    qreal m_arrowColumn = 16;
    qreal m_listIndent = 26;
    qreal m_taskBoxWidth = 24;
    qreal m_paragraphGap = 12;
    qreal m_listGap = 2;
    qreal m_headingGapBelow = 10;
    QList<qreal> m_headingGapsAbove;
    QList<qreal> m_fontSizes;
    QList<qreal> m_lineHeights;
    QList<qreal> m_firstLineCenters;
    QList<qreal> m_spaces;
    qreal m_touchTarget = 28;
    qreal m_iconSize = 14;
    qreal m_boxRadius = 3;
    QString m_fontFamily;
    int m_semiBoldWeight = 600;
    int m_normalWeight = 400;
    qreal m_footerHeight = 0;

    QColor m_textColor{Qt::black};
    QColor m_mutedColor{Qt::darkGray};
    QColor m_faintColor{Qt::gray};
    QColor m_linkColor{Qt::blue};
    QColor m_guideColor{Qt::lightGray};
    QColor m_hoverColor{0, 0, 0, 14};
    QColor m_pressedColor{0, 0, 0, 26};
    QColor m_successColor{Qt::darkGreen};
    QColor m_warningColor{Qt::darkYellow};
    QColor m_warningTextColor{Qt::darkYellow};
    QColor m_controlBorderColor{Qt::gray};
    QColor m_checkColor{Qt::white};
    QColor m_quoteBarColor{Qt::gray};
    QColor m_ruleColor{Qt::lightGray};

    QString m_findQuery;
    bool m_findCase = false;
    bool m_findDiacritics = false;
    bool m_findWholeWord = false;
    QColor m_findColor{0xf2, 0xb3, 0x3d, 0x80};

    QString m_hoveredLink;
    int m_hoverRow = -1;
    QString m_hoverPart;

    QList<Decoration> m_decorations;
    bool m_nodesDirty = true;
    qreal m_laidOutWidth = -1;
};

} // namespace brulion::editor
