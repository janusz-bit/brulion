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
//   headingLevel, lang, hasChildren, collapsed, visible (zoom + folds), taskStatus ("" | todo |
//   doing | done | cancelled | deferred), blockId, isEditing, isSelected, isZoomRoot,
//   selectionEdge  where a selected row sits in the band of selected visible rows: "single" |
//                "first" | "middle" | "last" ("" when not selected or hidden) — the band's rounded
//                ends; a selection is a set of rows (setSelectedUids: the editor's block selection
//                with the subtrees of its blocks, folded children too)
//   calcResults  calc views: [{ line, value, error, assigned }] per line of editText
//   tableData    table views: { header: [..], headerHtml: [..], aligns: ["left"|"center"|"right"|""],
//                rows: [[{ text, display, html, plain, error, formula }]] } with table formulas
//                evaluated; `html` is the cell's Markdown rendered by the InlineRenderer (formula
//                results and errors escaped), `plain` its visible text (column widths)
//   embeds       [{ target, alt, image, url, width, height }] attachment embeds (url from
//                EditorHost::assetUrl; width × height: a local picture's pixel size, 0 × 0 unknown)
//   onlyEmbeds   the block consists of embeds only — attachments, pages, sections, blocks (html is "")
//   transclusions  page / section / block embeds (`![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]`,
//                `!((id))`) with their content rendered read-only: [{ kind: "page" | "heading" |
//                "block", target, title, href (page:/block: link of the header), path ("" when not
//                found), found, message ("" | "missingPage" | "missingSection" | "missingBlock" |
//                "empty"), more (blocks left out), blocks: [{ kind: "block" | "embed" | "note", html,
//                depth, view, headingLevel, task, path, line, title, href, message ("cycle" |
//                "depth" | ...) }] }]; nested embeds up to MaxTransclusionDepth levels, cycles cut
//   properties   frontmatter: [{ key, value }]          query: { source, filters, terms }
//   calloutType, calloutFolded, line (first line in the file), uid, plainText (one line, for
//   breadcrumbs and accessibility)
//   listMarker   list items: the marker as written ("-", "*", "+", "3.", "2)"); "" otherwise —
//                the document mode draws bullets and numbers from it
//   prevKind     `kind` of the previous visible row ("" for the first one): the document mode's
//                spacing between blocks (a list item after a list item sits tight)
//   noteModeOnly YAML frontmatter holding nothing but the note mode key `brulion` — such a row is
//                not `visible` (the page header's mode toggle stands for it) unless edited
// Folding can be switched off (setFoldingEnabled(false), the document mode): folded blocks then
// show their children (`collapsed` false) while the folds are kept for the outline mode.
#pragma once

#include "blocktext.h"
#include "calc/calclocale.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"

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
    /// A few roles of `row` by name (view, kind, taskStatus, depth, hasChildren, collapsed,
    /// visible, blockId, headingLevel, plainText); empty for an invalid row.
    Q_INVOKABLE QVariantMap get(int row) const;

    // --- Document ----------------------------------------------------------------------------
    const markdown::BlockDocument &document() const { return m_doc; }
    /// The document text (cached; byte-identical to the loaded content when nothing changed).
    QString text() const;
    /// Replace the content with a model reset (a page was loaded).
    void resetContent(const QString &content);
    /// Replace the content keeping rows of unchanged blocks (undo/redo, external changes).
    /// Returns old uid -> new uid for the blocks that were kept. View state (folds, zoom,
    /// editing, selection) follows the kept blocks.
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
    EditorHost *host() const { return m_host; }
    void setPath(const QString &path) { m_path = path; }
    QString path() const { return m_path; }
    void setPalette(const markdown::Palette &palette);
    const markdown::Palette &palette() const { return m_palette; }
    /// Renderer context for text of another file (query results, transclusions): links resolve
    /// from `path`, `((id))` through the host, inline calculations see the variables of
    /// `content` above `line`; embedded pages / sections / blocks are hidden (shown in frames).
    markdown::RenderContext renderContextFor(const QString &path, const QString &content, int line) const;
    /// The host's vault configuration (cached until the rendering is invalidated).
    const VaultConfig &vaultConfig() const;
    void setEnglish(bool english);
    bool english() const { return m_english; }
    void setToday(QDate today);
    QDate today() const { return m_today; }
    /// Everything rendered may look different (index changed, links resolve differently).
    void invalidateRendering();

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
        /// Rendered table cells by text (until the rendering changes; no inline calculations or
        /// block references: those depend on more than the cell's text).
        QHash<QString, QString> cellHtml;
        bool transclusionsValid = false;
        bool transclusionsLocal = false; ///< embeds this page: follows every change of it
        QVariantList transclusions;
        bool dependsOnRefs = false; ///< `((id))`, `[[…#^id]]`: re-render on any change of the page
        bool dependsOnCalc = false; ///< inline calculations: re-render when the calc scope may change
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
    markdown::RenderContext renderContext(int docIndex) const;
    void ensureEmbeds(int row) const;
    /// The embeds role entry of a picture from the internet (EditorHost::webPicture).
    QVariantMap webEmbed(const Embed &em) const;
    void ensureTransclusions(int row) const;
    void rebuildIndex();
    /// Replays the row changes; returns whether the edit was calc-neutral (see apply()).
    bool replay(const QList<markdown::BlockChange> &changes, const QList<quint64> &finalUids);
    void invalidateContent(quint64 uid);
    /// Recompute structural state and derived data of all rows; emit dataChanged for rows that
    /// differ. `contentChanged` recomputes calc/table results and page-dependent html.
    void refresh(bool contentChanged);
    QVariantList computeCalcResults(int docIndex) const;
    QVariantMap computeTableData(int docIndex) const;
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
