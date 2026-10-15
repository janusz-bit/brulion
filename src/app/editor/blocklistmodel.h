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
//   calcResults  calc views: [{ line, value, error, assigned }] per line of editText
//   tableData    table views: { header: [..], headerHtml: [..], aligns: ["left"|"center"|"right"|""],
//                rows: [[{ text, display, html, plain, error, formula }]] } with table formulas
//                evaluated; `html` is the cell's Markdown rendered by the InlineRenderer (formula
//                results and errors escaped), `plain` its visible text (column widths)
//   embeds       [{ target, alt, image, url }] attachment embeds (url from EditorHost::assetUrl)
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
#pragma once

#include "blocktext.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"

#include <QAbstractListModel>
#include <QDate>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>

#include <functional>
#include <optional>

namespace brulion {
class EditorHost;
}

namespace brulion::editor {

class BlockListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

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
    void setEditingUid(quint64 uid);
    void setSelectedUid(quint64 uid);
    bool isRowVisible(int row) const;
    /// Next / previous visible row, -1 when none.
    int nextVisibleRow(int row) const;
    int previousVisibleRow(int row) const;
    /// First / last visible row, -1 when none.
    int firstVisibleRow() const;
    int lastVisibleRow() const;

    // --- Rendering context ----------------------------------------------------------------------
    void setHost(EditorHost *host);
    EditorHost *host() const { return m_host; }
    void setPath(const QString &path) { m_path = path; }
    QString path() const { return m_path; }
    void setPalette(const markdown::Palette &palette);
    const markdown::Palette &palette() const { return m_palette; }
    /// Renderer context for text of another file (query results, transclusions): links resolve
    /// from `path`, `((id))` through the host, inline calculations see the variables of
    /// `content` above `line`; embedded pages / sections / blocks are hidden (shown in frames).
    markdown::RenderContext renderContextFor(const QString &path, const QString &content, int line) const;
    void setEnglish(bool english);
    bool english() const { return m_english; }
    void setToday(QDate today);
    QDate today() const { return m_today; }
    /// Everything rendered may look different (index changed, links resolve differently).
    void invalidateRendering();

Q_SIGNALS:
    void countChanged();

private:
    struct RowState {
        // Structural state (recomputed after every change, compared to emit dataChanged).
        int displayDepth = 0;
        bool hasChildren = false;
        bool collapsed = false;
        bool visible = true;
        bool editing = false;
        bool selected = false;
        bool zoomRoot = false;
        // Content caches.
        BlockShape shape;
        bool shapeValid = false;
        bool htmlValid = false;
        bool embedsValid = false;
        QString html;
        QVariantList embeds;
        bool onlyEmbeds = false;
        QVariantList calcResults;
        QVariantMap tableData;
        QHash<QString, QString> cellHtml; ///< rendered table cells by text (until the rendering changes)
        bool transclusionsValid = false;
        bool transclusionsLocal = false; ///< embeds this page: follows every change of it
        QVariantList transclusions;
        bool dependsOnPage = false; ///< inline calculations, own block refs: re-render on any change
        int renderOffset = 0;       ///< offset of the rendered text in the body (reminder: ranges)
    };

    int docIndexOfRow(int row) const;
    const RowState *stateOf(int row) const;
    RowState &stateFor(quint64 uid) const;
    const BlockShape &cachedShape(int row) const;
    QString renderHtml(int docIndex) const;
    markdown::RenderContext renderContext(int docIndex) const;
    void ensureEmbeds(int row) const;
    void ensureTransclusions(int row) const;
    void rebuildIndex();
    void replay(const QList<markdown::BlockChange> &changes, const QList<quint64> &finalUids);
    void invalidateContent(quint64 uid);
    /// Recompute structural state and derived data of all rows; emit dataChanged for rows that
    /// differ. `contentChanged` recomputes calc/table results and page-dependent html.
    void refresh(bool contentChanged);
    QVariantList computeCalcResults(int docIndex) const;
    QVariantMap computeTableData(int docIndex) const;
    void emitRowsChanged(const QList<int> &rows, const QList<int> &roles);

    markdown::BlockDocument m_doc;
    mutable QString m_text;
    mutable bool m_textValid = false;
    QList<quint64> m_rows;              ///< uid per model row (mirrors the document after replays)
    QHash<quint64, int> m_docIndex;      ///< uid -> document index
    mutable QHash<quint64, RowState> m_state;

    quint64 m_zoomUid = 0;
    QSet<quint64> m_collapsed;
    quint64 m_editingUid = 0;
    quint64 m_selectedUid = 0;

    QPointer<EditorHost> m_host;
    QString m_path;
    markdown::Palette m_palette;
    bool m_english = false;
    QDate m_today;
};

} // namespace brulion::editor
