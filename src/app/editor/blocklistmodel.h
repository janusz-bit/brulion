// BlockListModel — the rows of the block editor (docs/UI.md "The block editor"): one row per
// block of a markdown::BlockDocument, in document order. Zoom (show one block's subtree) and
// folding are model-level: hidden rows stay in the model with `visible` false, so row numbers
// are always document block indices (PageController commands take rows).
//
// Rows are identified by the BlockDocument uid while the document lives: the model keeps its
// own row list and replays an operation's BlockChanges with begin/end calls, so delegates of
// untouched blocks survive every edit (no model resets, no scroll jumps). Replacing the whole
// content (undo/redo, external changes) diffs old and new block sources and keeps the rows of
// unchanged blocks too.
//
// Roles (QML names):
//   raw          block source as written (prefix + body)        body        editable body
//   editText     the part edited inline (no task marker / fences) editOffset  hidden chars before it
//   html         rich text from markdown::InlineRenderer (escaped; chips, page:/tag:/block:/ext:/
//                reminder: links; embedded pages/sections/blocks hidden — see `transclusions`);
//                "" for code/calc/query/table/rule/frontmatter views
//   depth        display depth (relative to the zoomed block)    docDepth    outline depth in the file
//   kind         listItem | paragraph | heading | code | table | quote | callout | rule | html | frontmatter
//   view         text | heading | code | calc | query | table | quote | callout | rule | frontmatter | html
//   headingLevel, lang, hasChildren, collapsed, visible (zoom + folds; also `shown`), taskStatus ("" | todo |
//   doing | done | cancelled | deferred), taskDated (a task with a 📅 / ⏳ date or a Logseq
//   SCHEDULED: / DEADLINE: line: no "Add date" button, hasFilingDate()), blockId, isEditing,
//   isSelected, isZoomRoot,
//   selectionEdge  where a selected row sits in the band of selected visible rows: "single" |
//                "first" | "middle" | "last" ("" when not selected or hidden) — the band's rounded
//                ends; a selection is a set of rows (setSelectedUids: the editor's block selection
//                with the subtrees of its blocks, folded children too)
//   calcResults  calc views: [{ line, value, error, assigned }] per line of editText
//   tableData    table views: { header: [..], headerHtml: [..], aligns: ["left"|"center"|"right"|""],
//                rows: [[{ text, display, html, plain, error, formula }]] } with table formulas
//                evaluated; `html` is the cell's Markdown rendered by the InlineRenderer (formula
//                results and errors escaped), `plain` its visible text (column widths); the view's
//                filters (setTableFilter(), nothing written): filters ["" per column], filtering
//                (the filter row shown), shown [bool per row: passes the filters], shownCount
//   chartData    chart views (docs/SYNTAX.md §9 "Charts"): { ok, error (translated, with the line),
//                labels: [..], labelsName, series: [{ name, values: [number | NaN] }] } — the
//                numbers of the table, calc block or query the block names (chart::resolveData);
//                ChartView lays them out
//   embeds       [{ target, alt, image, url, path, drawing, darkUrl, width, height }] attachment
//                embeds (url from EditorHost::assetUrl; path: the attachment's vault path; drawing:
//                a drawing, EditorHost::isDrawing — an SVG, `![[x.excalidraw]]`, an Obsidian
//                Excalidraw plugin file `![[x.excalidraw.md]]` or a page embed `![[Plan]]` of one;
//                then url and darkUrl are its preview in the light and the dark theme,
//                EditorHost::drawingUrl, and image is true; width × height: a local picture's
//                pixel size, a drawing's preview size, 0 × 0 unknown)
//   onlyEmbeds   the block consists of embeds only — attachments, pages, sections, blocks (html is "")
//   transclusions  page / section / block embeds (`![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]`,
//                `!((id))`) with their content rendered read-only: [{ kind: "page" | "heading" |
//                "block", target, title, href (page:/block: link of the header), path ("" when not
//                found), found, message ("" | "missingPage" | "missingSection" | "missingBlock" |
//                "empty"), more (blocks left out), blocks: [{ kind: "block" | "embed" | "note", html,
//                depth, view, headingLevel, task, path, line, raw (a text block's Markdown: its
//                token menus, PageController::tokenChipIn), title, href, message ("cycle" |
//                "depth" | ...) }] }]; nested embeds up to MaxTransclusionDepth levels, cycles cut
//   properties   frontmatter: [{ key, value (one line), type, source, raw, items, empty, editable,
//                multiline, linkDate, html (a text value rendered: links, tags; escaped), itemsHtml
//                (a list's items rendered) }] (blocktext.h pagePropertyMap(), cached by body)
//   propertiesInfo  frontmatter: { source, editable, problem, canAdd } of the block
//   query        { source, filters, terms }
//   calloutType, calloutFolded, line (first line in the file), uid, plainText (one line, for
//   breadcrumbs and accessibility)
//   listMarker   list items: the marker as written ("-", "*", "+", "3.", "2)"); "" otherwise —
//                the document mode draws bullets and numbers from it
//   prevKind     `kind` of the previous visible row ("" for the first one): the document mode's
//                spacing between blocks (a list item after a list item sits tight)
//   noteModeOnly YAML frontmatter holding nothing but the note mode key `brulion` — such a row is
//                not `visible` (the page header's mode toggle stands for it) unless edited
// Blocks keep their uid, row and cached state through every change: operations and undo / redo
// (BlockDocument::revert / reapply) report only the rows they add, remove, move or change, and
// replaceContent() (a whole new text: another device, a paste of Markdown, source mode) gives the
// blocks it keeps their old uids. A leading frontmatter row whose text changed (a property edited
// in the panel, the note mode, another device) keeps its row there too: a dataChanged instead of
// a removal and an insertion, so its delegate (the expanded panel, a focused field) survives.
// Folding can be switched off (setFoldingEnabled(false), the document mode): folded blocks then
// show their children (`collapsed` false) while the folds are kept for the outline mode.
#pragma once

#include "blocktext.h"
#include "calc/calclocale.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"
#include "markdown/taskdeps.h"

#include <QAbstractListModel>
#include <QDate>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
class EditorHost;
}

namespace brulion::calc {
class PageCalc;
class Scope;
} // namespace brulion::calc

namespace brulion::editor {

class BlockListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Rows shown (`visible`): 0 for an empty page — also one holding nothing but its note mode.
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY visibleCountChanged FINAL)

public:
    enum Role {
        RawRole = Qt::UserRole + 1,
        BodyRole,
        EditTextRole,
        EditOffsetRole,
        HtmlRole,
        DepthRole,
        DocDepthRole,
        KindRole,
        ViewRole,
        HeadingLevelRole,
        LangRole,
        HasChildrenRole,
        CollapsedRole,
        VisibleRole,
        TaskStatusRole,
        BlockIdRole,
        IsEditingRole,
        IsSelectedRole,
        IsZoomRootRole,
        CalcResultsRole,
        TableDataRole,
        ChartDataRole,
        EmbedsRole,
        OnlyEmbedsRole,
        PropertiesRole,
        QueryRole,
        CalloutTypeRole,
        CalloutFoldedRole,
        LineRole,
        UidRole,
        PlainTextRole,
        TransclusionsRole,
        ListMarkerRole,
        PrevKindRole,
        NoteModeOnlyRole,
        SelectionEdgeRole,
        PropertiesInfoRole,
        TaskDatedRole,
        /// `visible` under a name a delegate can take as a required property (Item has `visible`).
        ShownRole,
    };

    /// Nested embeds rendered inside a transclusion (deeper ones become a "depth" note).
    static constexpr int MaxTransclusionDepth = 3;
    /// Blocks rendered per transclusion (the rest is counted in `more`).
    static constexpr int MaxTransclusionBlocks = 60;
    Q_ENUM(Role)

    explicit BlockListModel(QObject *parent = nullptr);
    ~BlockListModel() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    int visibleCount() const { return m_visibleCount; }
    /// The shown rows before `row` (the scroll bar counts shown rows: RowScroll, src/app/ui/rowscroll.h).
    Q_INVOKABLE int visibleRowsBefore(int row) const;
    /// The row of the `n`-th shown row (from 0); -1 when there is none.
    Q_INVOKABLE int visibleRowAt(int n) const;
    /// A few roles of `row` by name (view, kind, taskStatus, taskDated, depth, hasChildren, collapsed,
    /// visible, blockId, headingLevel, plainText, editText, listMarker, html); empty for an invalid row.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// get(row)'s view, kind and taskStatus typed, for QML compiled ahead of time ("" for an
    /// invalid row).
    Q_INVOKABLE QString viewAt(int row) const;
    /// The file line (0-based) where `row`'s block starts, -1 for an invalid row (an extension's
    /// command on the block under the caret).
    Q_INVOKABLE int lineAt(int row) const;
    Q_INVOKABLE QString kindAt(int row) const;
    Q_INVOKABLE QString taskStatusAt(int row) const;

    // --- Document ----------------------------------------------------------------------------
    const markdown::BlockDocument &document() const { return m_doc; }
    /// The document text (cached; byte-identical to the loaded content when nothing changed).
    QString text() const;
    /// Replace the content with a model reset (a page was loaded).
    void resetContent(const QString &content);
    /// Replace the content keeping rows of unchanged blocks (external changes, whole-text edits):
    /// the blocks kept keep their uids (and rows, folds, zoom, editing, selection), the others
    /// are removed and inserted. Returns uid -> uid (the same) for the blocks that were kept, not
    /// for a leading frontmatter row kept with another text (for the caller that is another
    /// block: editing, a selection or a tracked handle on it end).
    QHash<quint64, quint64> replaceContent(const QString &content);
    /// Run a BlockDocument operation and replay its row changes.
    markdown::EditResult apply(const std::function<markdown::EditResult(markdown::BlockDocument &)> &op);

    /// Row of the block with `uid`, -1 if none.
    int rowOfUid(quint64 uid) const;
    quint64 uidAt(int row) const;
    /// Shape (view, edit mapping) of a row.
    BlockShape shapeAt(int row) const;
    QString editTextAt(int row) const;
    /// Offset in the body of the text given to the renderer (maps `reminder:FROM-TO` links).
    int renderOffsetAt(int row) const;

    // --- View state ----------------------------------------------------------------------------
    quint64 zoomUid() const { return m_zoomUid; }
    void setZoomUid(quint64 uid);
    bool isCollapsed(int row) const;
    void setCollapsed(int row, bool collapsed);
    QSet<quint64> collapsedUids() const { return m_collapsed; }
    void setCollapsedUids(const QSet<quint64> &uids);
    /// Folds hide children (outline mode, default) or are ignored (document mode).
    bool foldingEnabled() const { return m_foldingEnabled; }
    void setFoldingEnabled(bool enabled);
    void setEditingUid(quint64 uid);
    /// The selected blocks (the editor's block selection: rows with their subtrees).
    void setSelectedUids(const QSet<quint64> &uids);
    void setSelectedUid(quint64 uid) { setSelectedUids(uid ? QSet<quint64>{uid} : QSet<quint64>()); }
    const QSet<quint64> &selectedUids() const { return m_selectedUids; }
    bool isRowSelected(int row) const;
    bool isRowVisible(int row) const;
    /// Next / previous visible row, -1 when none.
    int nextVisibleRow(int row) const;
    int previousVisibleRow(int row) const;
    /// First / last visible row, -1 when none.
    int firstVisibleRow() const;
    int lastVisibleRow() const;

    // --- Rendering context ----------------------------------------------------------------------
    void setHost(EditorHost *host);
    /// The host's picture from the internet at `url` changed state ("" — all of them): the
    /// embeds of the blocks naming it are computed again (EditorHost::webPictureChanged).
    void webPictureChanged(const QString &url);
    /// The attachment at vault path `path` changed (a drawing saved again): the embeds showing it
    /// are computed again (a drawing's URL carries its version, EditorHost::drawingUrl).
    void attachmentChanged(const QString &path);
    /// The thumbnail of the PDF at `path` changed (EditorHost::pdfThumbnailChanged): the embeds of
    /// the rows naming it are computed again.
    void pdfThumbnailChanged(const QString &path);
    EditorHost *host() const { return m_host; }
    void setPath(const QString &path) { m_path = path; }
    QString path() const { return m_path; }
    void setPalette(const markdown::Palette &palette);
    const markdown::Palette &palette() const { return m_palette; }
    /// Renderer context for text of another file (query results, transclusions): links resolve
    /// from `path`, `((id))` through the host, inline calculations see the variables of
    /// `content` above `line`; embedded pages / sections / blocks are hidden (shown in frames).
    markdown::RenderContext renderContextFor(const QString &path, const QString &content, int line) const;
    /// The URL a click on the construct at `position` of `text` (a row's body, or a table cell's
    /// text) follows, read with row `row`'s render context (InlineRenderer::linkUrlAt(): part: for
    /// a path part of a link); "" when no link is there. The keyboard's Alt+Enter.
    QString linkUrlAt(int row, const QString &text, int position) const;
    /// The preview of what a link of rendered text written on `fromPath` ("" = this page) opens
    /// (docs/UI.md "Link previews", LinkPreview): for page:Target[#Heading | #^id] and block:id
    /// the transclusion map of `![[Target…]]` / `!((id))` — {kind, target, title, href, path,
    /// found, message, more, blocks} —; {} for other links and for a page link to an attachment
    /// or a drawing.
    QVariantMap linkPreview(const QString &url, const QString &fromPath) const;
    /// The host's vault configuration (cached until the rendering is invalidated).
    const VaultConfig &vaultConfig() const;
    void setEnglish(bool english);
    bool english() const { return m_english; }
    void setToday(QDate today);
    QDate today() const { return m_today; }
    /// Everything rendered may look different (index changed, links resolve differently).
    void invalidateRendering();

    // --- Table filters (view only: nothing is written; docs/SYNTAX.md §9) ------------------------
    /// The filter of column `column` of the table in `row` (calc::tableValueMatches(); "" none).
    void setTableFilter(int row, int column, const QString &text);
    /// Show or hide the table's filter row; hiding it clears the filters.
    void setTableFilterOpen(int row, bool open);

Q_SIGNALS:
    void countChanged();
    void visibleCountChanged();

private:
    struct RowState {
        // Structural state (recomputed after every change, compared to emit dataChanged).
        int displayDepth = 0;
        bool hasChildren = false;
        bool collapsed = false;
        bool visible = true;
        bool editing = false;
        bool selected = false;
        quint8 selectionEdge = 0; ///< SelectionEdge
        bool zoomRoot = false;
        QString prevKind;
        int line = -1; ///< the line number a view was last told (-1: fetched with the row)
        // Content caches.
        BlockShape shape;
        bool shapeValid = false;
        bool noteModeOnly = false; ///< with the shape
        bool htmlValid = false;
        bool embedsValid = false;
        QString html;
        QVariantList embeds;
        bool onlyEmbeds = false;
        QVariantList calcResults;
        QVariantMap tableData;
        /// The table's filters by column and whether its filter row is shown (view state: kept
        /// while the page is open, never written).
        QStringList tableFilters;
        bool tableFiltering = false;
        /// Chart views: the data (chartData role) and what it was computed from (the block's
        /// text; query charts are computed again only when it or the index changes).
        QVariantMap chartData;
        QString chartSource;
        bool chartValid = false;
        /// Rendered table cells by text (until the rendering changes; no inline calculations or
        /// block references: those depend on more than the cell's text).
        QHash<QString, QString> cellHtml;
        bool propertiesValid = false; ///< properties / propertiesInfo describe the current text
        QVariantList properties;
        QVariantMap propertiesInfo;
        bool transclusionsValid = false;
        bool transclusionsLocal = false; ///< embeds this page: follows every change of it
        QVariantList transclusions;
        bool dependsOnRefs = false; ///< `((id))`, `[[…#^id]]`: re-render on any change of the page
        bool dependsOnCalc = false; ///< inline calculations: re-render when the calc scope may change
        /// A task's subtask progress the html shows ("3/5"): re-rendered when its subtasks change.
        markdown::SubtaskProgress htmlProgress;
        int htmlCalcEpoch = -1;     ///< m_calcEpoch the html's inline calculations saw
        int renderOffset = 0;       ///< offset of the rendered text in the body (reminder: ranges)
        // Calculations: calcResults / tableData are current while resultsValid and, when they
        // read calc variables (resultsUseScope: calc blocks, tables with formulas or inline
        // calculations in cells), resultsEpoch == m_calcEpoch (the block's own text and
        // everything the calculations see unchanged). A table with block references in its
        // cells (resultsUseRefs) is recomputed on every change of the page.
        bool resultsValid = false;
        bool resultsUseScope = false;
        bool resultsUseRefs = false;
        int resultsEpoch = -1;
        bool calcTextValid = false; ///< calcInert / calcFences describe the current text
        bool calcInert = true;      ///< the text defines nothing (see analyseCalc())
        bool calcFences = false;    ///< the text has fence characters (``` / ~~~)
        /// Calc variables above the block (at scopeEpoch).
        std::shared_ptr<const calc::Scope> scope;
        int scopeEpoch = -1;
    };
    /// What an edit of a block can do to the page's calculations (see apply()).
    struct CalcFlags {
        bool sensitive = true; ///< defines variables / fences / frontmatter, or lies inside a calc fence
        bool openAfter = true; ///< a calc fence or the frontmatter continues after the block
    };

    int docIndexOfRow(int row) const;
    const RowState *stateOf(int row) const;
    RowState &stateFor(quint64 uid) const;
    const BlockShape &cachedShape(int row) const;
    QString renderHtml(int docIndex) const;
    /// The subtask progress of the list item at `docIndex` (its descendants' tasks, SYNTAX §5).
    markdown::SubtaskProgress subtaskProgressAt(int docIndex) const;
    /// What every rendering of the editor shares: today, the language, the colours, journal
    /// titles, links resolved from `path` through the host (its resolvers never hold the model).
    markdown::RenderContext baseRenderContext(const QString &path) const;
    markdown::RenderContext renderContext(int docIndex) const;
    void ensureEmbeds(int row) const;
    /// The embeds role entry of a picture from the internet (EditorHost::webPicture).
    QVariantMap webEmbed(const Embed &em) const;
    /// The embed map of a PDF attachment at vault path `path` (docs/UI.md "PDF viewer"): {target,
    /// alt, image false, pdf true, page (the fragment's, ≥ 1), viewable (EditorHost::pdfViewable),
    /// state, url (the thumbnail when ready), width, height (its pixels), pages, pageWidth,
    /// pageHeight, error, locked, name, path} — EditorHost::pdfThumbnail(), never assetUrl().
    QVariantMap pdfEmbed(const Embed &em, const QString &path) const;
    void ensureTransclusions(int row) const;
    /// The vault path of the drawing a page embed `![[Plan]]` (no heading, no block) shows: a note
    /// that is an Obsidian Excalidraw plugin drawing (EditorHost::isDrawing) — an attachment
    /// embed then, not a transclusion; "" for any other.
    QString drawingPageOf(const Transclusion &t) const;
    /// The `properties` / `propertiesInfo` roles of a frontmatter row (cached by its text).
    void ensureProperties(int row) const;
    void rebuildIndex();
    /// Replays the row changes; returns whether the edit was calc-neutral (see apply()).
    bool replay(const QList<markdown::BlockChange> &changes, const QList<quint64> &finalUids);
    void invalidateContent(quint64 uid);
    /// Recompute structural state and derived data of all rows; emit dataChanged for rows that
    /// differ (none inside resetContent()'s model reset). `contentChanged` recomputes calc/table
    /// results and page-dependent html.
    void refresh(bool contentChanged);
    QVariantList computeCalcResults(int docIndex) const;
    QVariantMap computeTableData(int docIndex) const;
    QVariantMap computeChartData(int docIndex) const;
    /// The table / the chart (of text `source`) of block `docIndex`, whose state is `st`,
    /// computed again; whether it changed.
    bool updateTable(RowState &st, int docIndex);
    bool updateChart(RowState &st, int docIndex, const QString &source);
    /// The table of `row` computed again (its filters changed) and told to the views.
    void refreshTable(int row);
    /// The rows whose computed embeds hold one that `shows` compute them again when asked for;
    /// the views are told.
    void invalidateEmbeds(const std::function<bool(const QVariantMap &embed)> &shows);
    void emitRowsChanged(const QList<int> &rows, const QList<int> &roles);

    // --- Calculations (docs/SYNTAX.md §9) ---------------------------------------------------------
    // Calc blocks, table formulas and inline calculations see the variables defined above them
    // (calc::PageCalc: calc blocks, `key:: value` properties, numeric frontmatter). The page's
    // PageCalc is built at most once per text revision, and only when a result has to be
    // computed. An edit that cannot change what any calculation sees ("calc-neutral": it only
    // touches blocks that define nothing and lie outside calc fences and the frontmatter) keeps
    // m_calcEpoch, so the other blocks keep their results and every block its scope; any other
    // edit, a new content, or a language change starts a new epoch.
    calc::CalcLocale calcLocale() const;
    /// The variables block `docIndex` sees (cached per block for the current epoch).
    std::shared_ptr<const calc::Scope> scopeFor(int docIndex) const;
    /// Classify every block of the current text (m_calcFlags).
    void analyseCalc();
    /// Start a new calc epoch: every result and scope is recomputed when needed.
    void calcContentChanged();

    markdown::BlockDocument m_doc;
    mutable QString m_text;
    mutable bool m_textValid = false;
    mutable std::shared_ptr<const calc::PageCalc> m_pageCalc; ///< for the current text; null = to build
    int m_calcEpoch = 0;
    QHash<quint64, CalcFlags> m_calcFlags; ///< by uid, for the current text when m_calcFlagsValid
    bool m_calcFlagsValid = false;
    /// Something reads calc variables: calc blocks, tables with formulas, rendered inline calculations.
    mutable bool m_hasCalcConsumers = false;
    QList<quint64> m_rows;              ///< uid per model row (mirrors the document after replays)
    QHash<quint64, int> m_docIndex;      ///< uid -> document index
    mutable QHash<quint64, RowState> m_state;

    quint64 m_zoomUid = 0;
    QSet<quint64> m_collapsed;
    bool m_foldingEnabled = true;
    int m_visibleCount = 0;
    QList<int> m_visibleRows; ///< the shown rows, in order
    bool m_resetting = false; ///< inside resetContent()'s model reset: the views read every row after it
    quint64 m_editingUid = 0;
    QSet<quint64> m_selectedUids;

    QPointer<EditorHost> m_host;
    QString m_path;
    markdown::Palette m_palette;
    bool m_english = false;
    QDate m_today;
    mutable std::optional<VaultConfig> m_config;
};

} // namespace brulion::editor
