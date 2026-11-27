// PageView — a page's blocks drawn by Atrament (docs/ATRAMENT.md "The block editor in Atrament
// (M2)", step 1 "Reading"): an Atrament view over the editor's BlockListModel, its rows a list()
// whose heights live in PageRows (the model's rows followed as they are inserted, removed, moved,
// folded) through PageExtents. Only in builds with Atrament (brulion_editor_atrament).
//
// Drawn by Atrament: rows whose rendered text it can draw (RenderedText::drawable) and whose view is
// text, a heading or a quote — paragraphs, list items with their bullet or number, headings,
// quotes with their bar, tasks with their box (done and cancelled struck through). Every other row
// is *foreign* (code, tables, math, queries, pictures, frontmatter…): an empty row of the height
// its QML delegate has, which the host places over it after each frame (foreignrows.h: where each
// was painted, `foreignFrames()`; its delegate's height back, `setForeignHeight()`). The page's
// header and footer (the title and properties above the blocks, the room under them) are the
// list's header and footer, QML items placed the same way.
//
//   auto page = app.newEntity<PageView>(model);   // a BlockListModel the caller keeps alive
//   AtramentItem::attach(app, page)               // or a TestWindow in tests
#pragma once

#include "foreignrows.h"
#include "pagerows.h"
#include "renderedspans.h"

#include "ui/atrament.h"

#include <QMetaObject>
#include <QPointer>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace brulion::editor {

class BlockListModel;

namespace atr {

// The list's RowExtents over PageRows: list row i is model row i (PageRows' tree row i + 1; the
// header and the footer are not the list's). A row PageRows does not show (a fold, a zoom) is
// hidden.
class PageExtents final : public atrament::RowExtents {
public:
    explicit PageExtents(PageRows &rows) : m_rows(rows) { }
    std::size_t count() const override;
    float offsetOf(std::size_t row) const override;
    float heightOf(std::size_t row) const override;
    bool isMeasured(std::size_t row) const override;
    std::size_t rowAt(float y) const override;
    void setMeasured(std::size_t row, float height) override;
    void markAllEstimated() override;

    // The frame's foreign rows (PageView's): their heights are their QML delegates'
    // (PageView::setForeignHeight), the list's layout of them is no measurement.
    const std::vector<std::size_t> *foreign = nullptr;

private:
    float headerHeight() const;
    PageRows &m_rows;
};

// The editor's geometry (BlockEditor / BlockDelegate's metrics).
struct PageMetrics {
    float contentX = 0;        // the text column's left edge in the view
    float contentWidth = 0;    // its width (0: the view's)
    float gutter = 16;         // from the column's edge to depth 0's text (the fold arrow, the bullet)
    float indentStep = 24;     // per depth (an outline)
    bool documentMode = false; // a document (Obsidian-like): list items indented by listIndent
    float listIndent = 27;     // a document's list item, per depth
    float bulletColumn = 20;   // the bullet's or number's column of a list item
    float rowPad = 3;          // above and below a row's text
    float fontSize = 16;
    float lineHeight = 24;
    float headingSizes[7] = {16, 28, 23, 20, 18, 16, 15}; // by heading level (0: body)
    float taskBox = 14;
};

class PageView {
public:
    PageView(atrament::Context<PageView> &cx, BlockListModel *model, PageMetrics metrics = {},
             PagePalette palette = {});
    ~PageView();
    PageView(const PageView &) = delete;
    PageView &operator=(const PageView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<PageView> &cx);

    BlockListModel *model() const { return m_model.data(); }
    // A new look (the theme changed): every row is laid out again.
    void setStyle(const PageMetrics &metrics, const PagePalette &palette);
    const PageMetrics &metrics() const { return m_metrics; }
    const PagePalette &palette() const { return m_palette; }
    // What a click asks of the editor: a link of row `row` (its href: `page:`, `tag:`, a URL…)
    // activated, a task's box toggled.
    std::function<void(int row, const QString &href)> onLink;
    std::function<void(int row)> onToggleTask;
    const PageRows &rows() const { return *m_rows; }
    atrament::ListHandle &handle() { return m_handle; }
    const atrament::ListHandle &handle() const { return m_handle; }

    // The last frame (tests, the host): the model rows drawn by Atrament and the foreign ones.
    // (A row laid out twice in a frame — measured, then shown — counts once.)
    int drawnRows() const;
    std::vector<std::size_t> foreignRows() const;
    // Whether a model row is Atrament's to draw (its roles as the model gives them).
    bool drawsRow(int row) const;

private:
    atrament::AnyElement renderRow(std::size_t row);

    QPointer<BlockListModel> m_model;
    std::unique_ptr<PageRows> m_rows;
    PageExtents m_extents;
    atrament::ListHandle m_handle;
    PageMetrics m_metrics;
    PagePalette m_palette;
    std::vector<QMetaObject::Connection> m_connections;
    std::vector<std::size_t> m_drawn;
    std::vector<std::size_t> m_foreign;

    // --- Foreign rows, the header and the footer: QML items placed over the frame by the host
    // (foreignrows.h, foreignrows.cpp; docs/ATRAMENT.md "Rows Atrament does not draw yet").
public:
    // Where the last frame painted the foreign rows, the header and the footer.
    const std::vector<ForeignFrame> &foreignFrames() const { return m_foreignFrames; }
    // A foreign row's height as its QML delegate lays it out: the row's measured height from now
    // on (rows above the anchor move nothing on screen). Whether it changed; call it inside an
    // update and notify (the page draws again).
    bool setForeignHeight(int row, float height);
    // The heights of the QML items above the blocks and below them.
    bool setHeaderHeight(float height);
    bool setFooterHeight(float height);
    float headerHeight() const { return m_headerHeight; }
    float footerHeight() const { return m_footerHeight; }
    // The foreign rows from `above` px above the view to `below` px below it, laid out or not, by
    // the last frame's scroll (their delegates are made and measured before they scroll in).
    std::vector<int> foreignRowsNear(float above, float below) const;
    // Where model row `row` is in the window (logical px) by the last frame's scroll, its height
    // the estimate until it is measured; nothing before the first frame or for a hidden row.
    std::optional<atrament::Rect> rowBounds(int row) const;
    // The model row at window y by the last frame's scroll (−1: the header, the footer, none).
    int rowAtY(float y) const;

private:
    atrament::AnyElement foreignRow(std::size_t row);
    atrament::AnyElement edgeElement(ForeignFrame::Kind kind);
    const ForeignFrame *edgeFrame(ForeignFrame::Kind kind) const;

    std::vector<ForeignFrame> m_foreignFrames;
    float m_headerHeight = 0;
    float m_footerHeight = 0;
};

} // namespace atr
} // namespace brulion::editor
