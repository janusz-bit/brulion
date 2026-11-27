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
// Editing (step 2, "Editing text rows"; pageediting.cpp): with a PageController
// (setController()), the row the controller edits — when Atrament draws it and it is a paragraph,
// a list item, a task, a heading or a quote — shows a TextInput over the block's `editText`
// instead of its rendered text, styled by `setEditStyler()` (the highlighter's rules). Typing
// goes to PageController::setEditText (the controller's undo stack, input rules, normalisation —
// what it gives back through editTextReset is shown); the editor's keys (Enter splits, Backspace
// at the start merges or leaves a list, Delete at the end joins the next block, Tab / Shift+Tab,
// Alt+↑/↓ move the block, ↑/↓ past the first or last line go to the block above or below at the
// caret's x, Ctrl+Enter the next task status, Ctrl+. folds, Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z the
// page's undo, Esc stops editing) are caught before the field (a capture listener) and become
// controller calls. A click or a tap on a drawn row edits it at the character under the pointer
// (the rendered text's display ↔ source map); an input method's line break in a one-line block
// splits it, a paste with line breaks becomes blocks (PageController::pasteText), as in the QML
// editor (InlineEditor.qml, BlockEditor.handleEditorKeyCode).
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
#include <optional>
#include <unordered_map>
#include <vector>

namespace brulion::editor {

class BlockListModel;
class PageController;

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
    float arrowColumn = 16;    // the fold arrow's column, left of the bullet (an outline)
    bool touch = false;        // a touch UI: fold arrows always shown
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
    // The band under selected blocks (Theme.bgSelected, Theme.focusRing, Theme.radiusSm).
    struct SelectionLook {
        atrament::Color band = atrament::rgba(0x3b5bdb22);
        atrament::Color ring = atrament::rgb(0x3b5bdb);
        float radius = 6;
    };
    void setSelectionLook(const SelectionLook &look);
    // Callouts' frames (BlockPartCallout: Theme.info, success, warning, marginRed, fgFaint; the fill
    // is the tint at `fill` opacity).
    struct CalloutLook {
        atrament::Color info = atrament::rgb(0x3b82f6);
        atrament::Color success = atrament::rgb(0x16a34a);
        atrament::Color warning = atrament::rgb(0xd97706);
        atrament::Color danger = atrament::rgb(0xe2574c);
        atrament::Color quote = atrament::rgb(0x8c959f);
        float fill = 0.10f;
        float radius = 10;
        atrament::Color tint(const QString &type) const;
    };
    void setCalloutLook(const CalloutLook &look);
    // The find bar's matches tinted in the rows, the edited one too (`query` empty: none).
    void setFind(const QString &query, bool matchCase, bool matchDiacritics, bool wholeWord, atrament::Color color,
                 atrament::Context<PageView> &cx);
    // Selects [from, to) of the edited block's text (the find bar's current match), when `row` is
    // the edited one; returns whether it was.
    bool selectInEdited(int row, int from, int to, atrament::Context<PageView> &cx);
    // Types `text` at the edited block's caret in place of its selection (a composition committed
    // first), with a space before it when `separate` and the character before is none; false when
    // no block is edited.
    bool insertAtCaret(std::u16string text, bool separate, atrament::Context<PageView> &cx);
    const PageMetrics &metrics() const { return m_metrics; }
    const PagePalette &palette() const { return m_palette; }
    // What a click asks of the editor: a link of row `row` (its href: `page:`, `tag:`, a URL…)
    // activated, a task's box toggled.
    std::function<void(int row, const QString &href)> onLink;
    // The same with the click's window point (a chip's menu opens there); used instead of onLink
    // for clicks when set.
    std::function<void(int row, const QString &href, atrament::Point at)> onLinkAt;
    std::function<void(int row)> onToggleTask;
    // The outline's bullet clicked (zoom into the block); a right-click on a row at a window point
    // (the block menu). Unset: no bullet click, no menu.
    std::function<void(int row)> onBulletClick;
    std::function<void(int row, atrament::Point at)> onBlockMenu;
    // A finger held on a drawn row's text (not the edited one): the touch selection mode
    // (BlockEditor.longPressText). Unset: a long press is a press.
    std::function<void(int row)> onLongPressRow;
    // A mouse's press on one block dragged into another: the blocks from `anchor` to `focus` are
    // selected (default: PageController::stopEditing + selectRange; the QML editor also takes the
    // keyboard to its list: BlockEditor.leaveEditingForSelection).
    std::function<void(int anchor, int focus)> onDragSelect;
    const PageRows &rows() const { return *m_rows; }
    atrament::ListHandle &handle() { return m_handle; }
    const atrament::ListHandle &handle() const { return m_handle; }

    // The last frame (tests, the host): the model rows drawn by Atrament and the foreign ones.
    // (A row laid out twice in a frame — measured, then shown — counts once.)
    int drawnRows() const;
    std::vector<std::size_t> foreignRows() const;
    // Whether a model row is Atrament's to draw (its roles as the model gives them).
    bool drawsRow(int row) const;

    // --- editing (pageediting.cpp)
    // The page's controller (its `blocks()` must be the view's model); none: read only.
    void setController(PageController *controller, atrament::Context<PageView> &cx);
    PageController *controller() const;
    // Whether Atrament edits model row `row` when it is the controller's editing row.
    bool editsRow(int row) const;
    // The row the field edits now (−1: none) and the field (null before the first edit).
    int editedRow() const;
    const atrament::Entity<atrament::TextInput> *input() const { return m_input ? &*m_input : nullptr; }
    // The edited text's spans (the editor's highlighting of the block's Markdown); none: plain.
    void setEditStyler(atrament::TextInput::Styler styler, atrament::Context<PageView> &cx);
    // The field's look: its caret and selection colours, the placeholder of an empty block, its
    // name for screen readers ("Block text", InlineEditor's).
    void setEditLook(atrament::Color caret, atrament::Color selection, std::u16string placeholder,
                     std::u16string label, atrament::Context<PageView> &cx);
    // Where a click or a tap on drawn row `row` at window point `p` puts the caret: an offset of
    // the block's `editText` (−1: not known — the row was not drawn in the last frame).
    int editCursorAt(int row, atrament::Point p) const;
    // The editText offset on the first (`lastLine` false) or last line of drawn row `row` nearest
    // to window x `x` (↑/↓ into the row); −1 when not known.
    int editCursorOnLine(int row, float x, bool lastLine) const;
    // The page's height as the list knows it (measured rows exact, the others estimated), and a
    // callback whenever a frame changed it (a journal day as tall as its blocks: the host sizes
    // its item to it).
    float contentHeight() const;
    std::function<void(float height)> onContentHeight;
    // The rendered text of drawn row `row` as laid out in the last frame (nullptr: not drawn).
    const atrament::TextLayoutHandle *rowText(int row) const;
    // What the host does instead of the view's own handling (each unset: the view's default).
    // A click or a tap asking to edit `row` at `cursor` (modifiers: Shift / Ctrl+click select
    // blocks in the QML editor) — true when the host handled it.
    std::function<bool(int row, int cursor, const atrament::Modifiers &modifiers)> onEditRequest;
    // A row Atrament edits took the editing (the host gives the item the keyboard).
    std::function<void()> onEditingStarted;
    // Esc in the field (default: PageController::stopEditing).
    std::function<void()> onEscape;
    // Ctrl+Z / Ctrl+Y (default: PageController::undo / redo).
    std::function<void(bool redo)> onUndo;
    // Alt+Enter: the link at the caret (default: onLink with PageController::linkAtCursor).
    std::function<void(int row, int cursor, bool newPane)> onFollowLink;
    // Shift+↑ / Shift+↓ past the block's text: the blocks are selected from row `row` (default:
    // PageController::extendSelection); Ctrl+A on a block's whole text: every block (selectAll).
    std::function<void(int row, int step)> onExtendSelection;
    std::function<void()> onSelectAllBlocks;
    // The field's text, caret or composition changed (the editor's suggestions follow them).
    std::function<void()> onEditStateChanged;
    // A key the suggestion list takes while it is open ("up", "down", "enter", "tab", "escape";
    // no modifiers) — true when it took it, before the editor's own meaning.
    std::function<bool(std::u16string_view key)> onSuggestionKey;
    // The field's state for the host (suggestions, the toolbar): its text (a composition
    // included), the caret, the caret's rectangle in window px; nothing while no row is edited.
    struct EditState {
        int row = -1;
        std::u16string text;
        int cursor = 0;
        int anchor = 0;
        std::optional<atrament::Range> composition;
        atrament::Rect caret;
    };
    EditState editState() const;
    // Ends the input method's composition (the word becomes text the controller has).
    void commitComposition(atrament::Context<PageView> &cx);

private:
    // The fold arrow's names for screen readers ("Expand", "Collapse": BlockDelegate's texts).
    QString m_foldNames[2];
    atrament::AnyElement renderRow(std::size_t row);
    // Editing (pageediting.cpp).
    void controllerEditingChanged(atrament::Context<PageView> &cx);
    void controllerTextReset(int row, const QString &text, int cursor, atrament::Context<PageView> &cx);
    void loadInput(int row, int cursor, atrament::Context<PageView> &cx);
    void inputChanged(atrament::Context<PageView> &cx);
    void inputSelectionChanged(atrament::Context<PageView> &cx);
    bool editorKey(const atrament::Keystroke &key, atrament::Window &window, atrament::Context<PageView> &cx);
    void rowClicked(std::size_t row, const atrament::ClickEvent &click, atrament::Window &window,
                    atrament::Context<PageView> &cx);
    bool editableView(const QString &view, bool drawable) const;

    QPointer<BlockListModel> m_model;
    std::unique_ptr<PageRows> m_rows;
    PageExtents m_extents;
    atrament::ListHandle m_handle;
    PageMetrics m_metrics;
    PagePalette m_palette;
    std::vector<QMetaObject::Connection> m_connections;
    std::vector<std::size_t> m_drawn;
    std::vector<std::size_t> m_foreign;

    // Editing (pageediting.cpp).
    QPointer<PageController> m_controller;
    std::vector<QMetaObject::Connection> m_controllerConnections;
    std::optional<atrament::Entity<atrament::TextInput>> m_input;
    std::vector<atrament::Subscription> m_inputSubscriptions;
    quint64 m_editUid = 0;     // the block the field shows (0: none)
    std::u16string m_lastText; // the text last given to or taken from the controller
    bool m_softBreak = false;  // the next line break in the field is Shift+Enter's
    bool m_focusInput = false; // the field takes the focus in the next frame
    bool m_revealEdited = false; // the edited row is scrolled into view in the next frame
    atrament::TextInput::Styler m_editStyler;
    // The text of each drawn row in the last frame, by block uid (clicks, ↑/↓ into a row).
    std::unordered_map<quint64, atrament::TextLayoutHandle> m_rowText;
    std::unordered_map<quint64, std::uint64_t> m_rowTextFrame;
    float m_reportedHeight = -1;
    atrament::Color m_editCaret = atrament::rgb(0x3b5bdb);
    atrament::Color m_editSelection = atrament::rgba(0x3b5bdb40);
    std::u16string m_editPlaceholder;
    std::u16string m_editLabel = u"Block text";
    std::uint64_t m_frame = 0;
    atrament::EntityId m_selfId = 0;
    atrament::App *m_app = nullptr;
    SelectionLook m_selectionLook;
    quint64 m_hoveredUid = 0; // the row under the mouse (its fold arrow shows)
    // A mouse's press on a block (its uid; 0: none), whether it selected blocks by dragging (its
    // release then edits nothing).
    quint64 m_dragUid = 0;
    bool m_dragSelected = false;
    int m_dragFocus = -1;
    float m_listTop = 0; // the list's top in the window (the last frame)
    void dragOver(atrament::Point p, atrament::Context<PageView> &cx);
    CalloutLook m_calloutLook;
    struct Find {
        QString query;
        bool matchCase = false;
        bool matchDiacritics = false;
        bool wholeWord = false;
        atrament::Color color;
    } m_find;
    // The spans of the find bar's matches in `text`.
    void appendFindSpans(QStringView text, std::vector<atrament::TextSpan> &spans) const;
    void applyEditStyler(atrament::Context<PageView> &cx);
};

} // namespace atr
} // namespace brulion::editor
