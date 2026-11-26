// PageView — a page's blocks drawn by Atrament (docs/ATRAMENT.md "The block editor in Atrament
// (M2)", step 1 "Reading"): an Atrament view over the editor's BlockListModel, its rows a list()
// whose heights live in PageRows (the model's rows followed as they are inserted, removed, moved,
// folded) through PageExtents. Only in builds with Atrament (brulion_editor_atrament).
//
// Drawn by Atrament: rows whose rendered text it can draw (RenderedText::drawable) and whose view is
// text, a heading or a quote — paragraphs, list items with their bullet or number, headings,
// quotes with their bar, tasks with their box (done and cancelled struck through). Every other row
// is *foreign* (code, tables, math, queries, pictures, frontmatter…): today a placeholder of its
// estimated height; the QML delegate placed over it comes with the host (`foreignRows()` lists
// them per frame for that).
//
//   auto page = app.newEntity<PageView>(model);   // a BlockListModel the caller keeps alive
//   AtramentItem::attach(app, page)               // or a TestWindow in tests
#pragma once

#include "pagerows.h"
#include "renderedspans.h"

#include "ui/atrament.h"

#include <QMetaObject>
#include <QPointer>

#include <functional>
#include <memory>
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
};

} // namespace atr
} // namespace brulion::editor
