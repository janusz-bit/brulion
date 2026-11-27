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

#include "editstyles.h"
#include "foreignrows.h"
#include "pagerows.h"
#include "renderedspans.h"
#include "spell/spellwords.h"

#include "ui/atrament.h"

#include <QMetaObject>
#include <QPointer>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace brulion::editor {

class BlockListModel;
class PageController;
class QueryResultModel;
class SpellCheck;
class VimSession;
namespace atr {
class EditStyler;
class PictureCache;
}

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
    // Code blocks drawn and edited by Atrament (docs/ATRAMENT.md M2 step 5, the first kind of
    // "Blocks with views"): CodeBlockView's sunken panel, its language label, the code in the code
    // face coloured by the highlighter's Code mode (Luau for ```luau / ```lua), edited in place
    // (Enter a line break, Tab a tab). Off: code blocks are foreign rows (the QML delegate).
    struct CodeLook {
        atrament::Color background = atrament::rgb(0xf3f1ec); // Theme.bgSunken
        atrament::Color border = atrament::rgba(0x8c959f33);  // Theme.borderSubtle
        atrament::Color label = atrament::rgb(0x9aa0a6);      // Theme.fgFaint
        float radius = 10;                                    // Theme.radius
        float fontSize = 12;                                  // Theme.textSm
        float lineHeight = 18;                                // Theme.lineHeightPx(textSm, 1.5)
        float labelSize = 10;                                 // Theme.textXs
        // Each line numbered in a gutter on the left (the device's setting or the note's
        // `line-numbers`: CodeBlockView's; pagecode.cpp).
        bool lineNumbers = false;
    };
    void setDrawsCode(bool on, const CodeLook &look, atrament::Context<PageView> &cx);
    bool drawsCode() const { return m_drawsCode; }
    // Calculations (```calc, CalcBlockView): the lines in the note's font on a sunken panel under a
    // small "Calculation", each line's result (BlockListModel's calcResults) right-aligned on its
    // line past a thin separator — an assignment's muted, an error smaller in red, a total
    // (`sum`, `razem`…) semibold under a rule —, a frame while edited; a click edits the lines at
    // the character (calcrows.cpp).
    struct CalcLook {
        atrament::Color background = atrament::rgb(0xf3f1ec); // Theme.bgSunken
        atrament::Color frame = atrament::rgba(0x2f6feb80);   // Theme.focusRing at 0.5
        atrament::Color separator = atrament::rgba(0x8c959f33); // Theme.borderSubtle
        atrament::Color rule = atrament::rgb(0x8c959f);       // Theme.borderStrong
        atrament::Color label = atrament::rgb(0x9aa0a6);      // Theme.fgFaint
        atrament::Color muted = atrament::rgb(0x6b6f76);      // Theme.fgMuted
        atrament::Color error = atrament::rgb(0xc4314b);      // Theme.dangerText
        float radius = 10;                                    // Theme.radius
        float labelSize = 10;                                 // Theme.textXs
        float errorSize = 12;                                 // Theme.textSm
        std::u16string uiFamily;                              // Theme.fontUi
        std::u16string title = u"Calculation";
    };
    void setDrawsCalc(bool on, const CalcLook &look, atrament::Context<PageView> &cx);
    bool drawsCalc() const { return m_drawsCalc; }
    // The highlighter's rules for the edited text: the field takes its Inline styler for text
    // rows, its Code one (with the block's language) for code (editstyles.h); replaces
    // setEditStyler's.
    void setEditStyles(std::shared_ptr<const EditStyler> styles, atrament::Context<PageView> &cx);
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
    // A right-click or a finger held on a link of row `row` (a chip's, a tag's, a page's): its
    // menu at the window point, instead of the block's menu or the touch selection mode.
    std::function<void(int row, const QString &href, atrament::Point at)> onLinkMenu;
    std::function<void(int row)> onToggleTask;
    // A right-click or a finger held on row `row`'s task box: its status menu at the window point
    // (BlockEditor.openTaskStatusMenu), instead of the block's menu or the selection mode.
    std::function<void(int row, atrament::Point at)> onTaskMenu;
    // Ctrl+Shift+Enter in the edited block: its status menu under the caret (window point;
    // BlockEditor decides whether the block can have one).
    std::function<void(int row, atrament::Point at)> onTaskMenuAtCaret;
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
    // Scrolls the page the least that shows window rectangle `r` with `margin` around it inside a
    // view of `viewportHeight` (a hosted delegate's caret: BlockEditor.ensureVisible); whether it
    // moved.
    bool reveal(atrament::Rect r, float margin, float viewportHeight, atrament::Context<PageView> &cx);
    // Scrolls the least that shows model row `row` whole (measured first; a row taller than the
    // view at its top) — BlockEditor.ensureRowVisible; false for a hidden row.
    bool revealRow(int row, atrament::Context<PageView> &cx);
    // The rendered text of drawn row `row` as laid out in the last frame (nullptr: not drawn).
    const atrament::TextLayoutHandle *rowText(int row) const;
    // Where drawn row `row`'s first link whose href contains `hrefPart` is, and its task's box (the
    // middle, window px, by the last frame): what the self-tests in the app click (pagepoints.cpp).
    std::optional<atrament::Point> linkPoint(int row, const QString &hrefPart) const;
    std::optional<atrament::Point> taskBoxPoint(int row) const;
    // Where the caret before offset `offset` of the edited field is (its middle, window px; nothing
    // when no block is edited or past its text): the self-tests click a word of the field there.
    std::optional<atrament::Point> editPoint(int offset) const;
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
        // The field holds a block's prose (suggestions, input rules, the toolbar's formatting);
        // false for code, a calculation, a diagram's or a formula's source.
        bool prose = true;
    };
    EditState editState() const;
    // Ends the input method's composition (the word becomes text the controller has).
    void commitComposition(atrament::Context<PageView> &cx);

    // --- Embedded mode (docs/ATRAMENT.md "The journal stream as one list"): the page drawn inside
    // another view's list — a day of the journal stream (journalview.h) — as a column of its header,
    // its rows and its footer, with no list and no scrolling of its own. What would scroll the page's
    // list goes to the view around it (the embedding).
    struct PageEmbedding {
        // Scrolls the view around by `dy` (clamped as the last frame measured it); whether it moved.
        std::function<bool(float dy)> scrollBy;
        // How far the view around scrolled since the last frame.
        std::function<float()> scrolledSinceFrame;
        // ↑ on the first line of the page's first block / ↓ on the last line of its last one, the
        // caret at window x: true when the view around took it (the next day's block edited).
        std::function<bool(bool up, float x)> pastEdge;
    };
    void setEmbedding(std::optional<PageEmbedding> embedding, atrament::Context<PageView> &cx);
    bool embedded() const { return m_embedding.has_value(); }
    // Embedded: the row the page asked to show since the last frame (the edited one after Enter, a
    // zoom, ↑/↓; revealRow; Vim's zt / zz / zb; a table's cell) — the view around scrolls there.
    struct Reveal {
        int row = -1;
        atrament::ListPosition position = atrament::ListPosition::Nearest;
    };
    std::optional<Reveal> takeReveal();
    // Embedded: where row `row` is in the layout just computed for the page — its top from the
    // layout's root (the section the page is drawn in) and its height; nothing for a row the page
    // did not lay out.
    std::optional<atrament::RowPart> embeddedPart(int row, atrament::Window &window) const;
    // Embedded: the block selection's keys (those no focused element took) are this page's — the
    // stream's active day's only.
    void setKeysActive(bool on, atrament::Context<PageView> &cx);
    bool keysActive() const { return m_keysActive; }
    // Embedded: the strip under the last block takes taps `px` tall (a touch target reaching past
    // the footer into the room below): a tap appends an empty block and edits it (onFooterTap
    // instead when set).
    void setFooterTapHeight(float px) { m_footerTap = std::max(0.0f, px); }
    std::function<void()> onFooterTap;
    // A mouse's release the page did not see (the view around had it): the press it may have begun
    // on a block ends.
    void pointerReleased(atrament::Context<PageView> &cx);
    // The block a mouse's press began on that may become a drag selection (−1: none), and the drag
    // having selected blocks elsewhere (another day): its release edits nothing.
    int pressedRow() const;
    void markDragSelected() { m_dragSelected = true; }
    // The find bar's query the rows tint (tests).
    const QString &findQuery() const { return m_find.query; }

private:
    std::optional<PageEmbedding> m_embedding;
    std::optional<Reveal> m_reveal;
    bool m_keysActive = true;
    float m_footerTap = 0;
    // Embedded: each row's layout node in the frame's last layout of the page (embeddedPart()).
    std::vector<std::pair<int, atrament::NodeId>> m_rowNodes;
    class RowBox;
    // Shows a row: the page's list (scrollToRow), or embedded, the view around (takeReveal()).
    void scrollRowTo(std::size_t row, atrament::ListPosition position);
    // Scrolls the page's list (or embedded, the view around) by `dy`.
    bool scrollPageBy(float dy);
    atrament::AnyElement embeddedColumn();
    atrament::AnyElement embeddedFooter();
    void footerTapped();

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
    void dragOver(atrament::Point p, atrament::Context<PageView> &cx);
    CalloutLook m_calloutLook;
    bool m_drawsCode = false;
    CodeLook m_codeLook;
    std::shared_ptr<const EditStyler> m_styles;
    QString m_inputLanguage; // the language the field's code styler colours ("" for text rows)
    bool m_inputCode = false;
    atrament::AnyElement renderCodeRow(std::size_t row, const QModelIndex &index, float textX, bool edited);
    bool isCode(int row) const;
    atrament::AnyElement renderCalcRow(std::size_t row, const QModelIndex &index, float textX, bool edited);
    bool isCalc(int row) const;
    atrament::AnyElement renderRuleRow(float textX, bool edited);
    bool isRule(int row) const;
    // A code block, a calculation or a rule drawn by Atrament: its text is edited as it is shown.
    bool isPanel(int row) const { return isCode(row) || isCalc(row) || isRule(row); }
    bool m_drawsCalc = false;
    CalcLook m_calcLook;
    // Per calculation: where each of its lines starts (from its text's top), as the last frame
    // laid them out — its results' places; a frame that wraps them anew draws once more.
    std::unordered_map<quint64, std::vector<float>> m_calcTops;
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

    // --- Charts, Mermaid diagrams and math formulas drawn by Atrament (diagramrows.cpp,
    // mathrows.cpp, diagrampaint.h; docs/ATRAMENT.md M2 step 5 "Blocks with views"). Off, they are
    // foreign rows (ChartBlockView, MermaidBlockView, MathBlockView).
public:
    // The panels' and the diagrams' look (the QML views' Theme values): the sunken panel and its
    // border (Theme.focusRing at half opacity while edited), the source's label, the diagram's
    // theme colours (diagram::ThemeColors: Theme.fg, fgMuted, bgSunken, border, accent, dark), its
    // font and sizes, the message of a source that cannot be drawn (Theme.dangerText; a Mermaid
    // type Brulion does not draw: Theme.fgMuted).
    struct ViewLook {
        atrament::Color panel = atrament::rgb(0xf3f1ec);
        atrament::Color panelBorder = atrament::rgba(0x8c959f33);
        atrament::Color editingBorder = atrament::rgba(0x3b5bdb80);
        atrament::Color label = atrament::rgb(0x9aa0a6);
        atrament::Color text = atrament::rgb(0x1f2430);
        atrament::Color muted = atrament::rgb(0x535a6a);
        atrament::Color border = atrament::rgb(0xddd6c6);
        atrament::Color accent = atrament::rgb(0x2c6aa3);
        bool dark = false;
        atrament::Color danger = atrament::rgb(0xa8382d);
        float radius = 10;
        float framedPad = 8;         // an outline's air above and below a framed block (Theme.space2)
        float codeSize = 12;         // the source (Theme.textSm, 1.5 line height)
        float codeLineHeight = 18;
        float labelSize = 10;        // the label and the message (Theme.textXs)
        float chartFontSize = 12;    // ChartView's Theme.textSm
        float mermaidFontSize = 13;  // MermaidView's Theme.textSm + 1
        std::u16string family = u"Brulion Inter"; // Theme.fontUi
        bool english = false;        // a chart's numbers (PageController.locale "en")
    };
    void setViewLook(const ViewLook &look, atrament::Context<PageView> &cx);
    const ViewLook &viewLook() const { return m_viewLook; }
    void setDrawsDiagrams(bool on, atrament::Context<PageView> &cx);
    bool drawsDiagrams() const { return m_drawsDiagrams; }
    void setDrawsMath(bool on, atrament::Context<PageView> &cx);
    bool drawsMath() const { return m_drawsMath; }
    // Whether Atrament draws model row `row` as a chart or a Mermaid diagram / as a formula: a
    // formula only when its rendered text is drawable (mathDrawable), and the one Atrament edits.
    bool isDiagram(int row) const;
    bool isMath(int row) const;
    // What a drawn chart, diagram or formula row shows (tests, the host): "chart" | "mermaid" |
    // "math"; "ok" | "error" | "unsupported"; the translated message; the text alternative (a
    // screen reader's name and description); the diagram's size at the last frame's width.
    struct ViewRowInfo {
        QString kind;
        QString status;
        QString message;
        QString name;
        QString description;
        atrament::Size size;
    };
    ViewRowInfo viewRowInfo(int row) const;
    // A formula Atrament's text draws well: sub- and superscripts, italic and bold, the
    // radicand's overline, \texttt and \textsf; small capitals and anything else keep the QML view.
    static bool mathDrawable(const markdown::RenderedText &text);
    // A formula's runs as spans: scripts at 0.7 of `fontSize`, raised 0.4 em / lowered 0.2 em, the
    // radicand overlined, \texttt in the code face and \textsf in `sansFamily`.
    static std::vector<atrament::TextSpan> mathSpans(const markdown::RenderedText &text, float fontSize,
                                                     const PagePalette &palette, std::u16string_view sansFamily);
    struct ViewCaches; // laid-out diagrams and charts (diagramrows.cpp)

private:
    // The row's panel (renderRow's branch for them), its click and the edited field's keys.
    atrament::AnyElement renderViewRow(std::size_t row, const QModelIndex &index, float textX, atrament::Div line);
    atrament::AnyElement renderDiagramPanel(std::size_t row, const QModelIndex &index, float textX, bool edited);
    atrament::AnyElement renderMathPanel(std::size_t row, const QModelIndex &index, float textX, bool edited);
    // Whether the field shows the row's source as code (a diagram's; code's is isCode) and whether
    // its Enter is a line break and Tab a tab (code, diagrams, formulas).
    bool editsAsCode(int row) const;
    bool editsSource(int row) const;
    // A formula the field may edit: drawable when the editing starts, then Atrament's until it ends.
    bool mathEditable(int row) const;
    void viewRowClicked(std::size_t row, const atrament::ClickEvent &click, atrament::Context<PageView> &cx);
    std::shared_ptr<ViewCaches> m_viewCaches;
    ViewLook m_viewLook;
    bool m_drawsDiagrams = false;
    bool m_drawsMath = false;
    // --- Tables drawn and edited by Atrament (tablerows.cpp, tablelayout.h; docs/ATRAMENT.md M2
    // step 5 "Blocks with views", tables): TableBlockView's grid — the header on its sunken band,
    // the rows' and the columns' lines, formula cells tinted and right-aligned, each cell's
    // Markdown rendered as the QML table renders it (links and chips clickable), the columns'
    // alignments and widths (scrolled sideways when wider than the page), "+ Row" / "+ Column"
    // under it while it is edited or selected —, and a click on a cell editing it at the character
    // under the pointer: PageController::setTableCell as it is typed (one undo step per cell),
    // Tab / Shift+Tab the next / previous cell (a new row after the last), Enter the cell below,
    // ↑/↓ across rows and out of the table to the blocks above and below, ←/→ at the text's edge
    // the next cell, Ctrl+Enter a row below, Alt+arrows move the row or the column, Esc cancels the
    // cell's typing and selects the table, Ctrl+Z / Ctrl+Y the page's undo (the cell stays edited);
    // the cell selection (tableselection.cpp) and the filter row (tablefilters.cpp) below. Off:
    // tables are foreign rows (the QML delegate).
public:
    struct TableLook {
        atrament::Color background = atrament::rgb(0xffffff);           // Theme.bgElevated: the grid
        atrament::Color headerBackground = atrament::rgb(0xf3f1ec);     // Theme.bgSunken
        atrament::Color border = atrament::rgba(0x8c959f59);            // Theme.border: the frame
        atrament::Color borderStrong = atrament::rgba(0x8c959f80);      // Theme.borderStrong: under the header
        atrament::Color borderSubtle = atrament::rgba(0x8c959f33);      // Theme.borderSubtle: rows, columns
        atrament::Color focusRing = atrament::rgb(0x3b5bdb);            // the frame (½) and the edited cell
        atrament::Color formula = atrament::rgba(0x3b82f60f);           // Theme.info at 6 %: a formula cell
        atrament::Color error = atrament::rgb(0xa8382d);                // Theme.dangerText: #ERR, #CYCLE
        atrament::Color input = atrament::rgb(0xffffff);                // Theme.bgElevated2: the edited cell
        atrament::Color hover = atrament::rgba(0x0000000e);             // Theme.bgHover: a button under the mouse
        atrament::Color picked = atrament::rgba(0x3b5bdb29);            // Theme.accent at 16 %: a selected cell
        atrament::Color accent = atrament::rgb(0x3b5bdb);               // Theme.accent: a filtered column's funnel
        atrament::Color muted = atrament::rgb(0x6b6f76);                // Theme.fgMuted: the rows shown
        std::u16string fontFamily = u"Brulion Inter";                   // Theme.fontUi
        std::u16string monoFamily = u"Brulion JetBrains Mono";          // Theme.fontMono: a formula edited
        float fontSize = 14;                                            // Theme.textBase
        float lineHeight = 20;                                          // Theme.lineHeightPx(textBase, 1.4)
        float rowHeight = 32;                                           // 40 on touch UIs
        float cellPadding = 12;                                         // Theme.space3
        float radius = 10;                                              // Theme.radius
        float buttonHeight = 28;                                        // Theme.controlHeightSm (a filter field too)
        float buttonTextSize = 12;                                      // Theme.textSm
        float filterHeight = 36;                                        // the filter row (48 on touch UIs)
        float fieldTextSize = 13;                                       // Theme.textControl: a filter field
        float statusTextSize = 11;                                      // Theme.textXs: the rows shown
    };
    void setDrawsTables(bool on, const TableLook &look, atrament::Context<PageView> &cx);
    bool drawsTables() const { return m_drawsTables; }
    // The cell being edited: its table's model row (−1: none), its row in the table (−1: the
    // header) and its column.
    struct TableCell {
        int row = -1;
        int tableRow = -1;
        int column = -1;
    };
    TableCell editedCell() const;
    // Edits cell (`tableRow`, `column`) of the table in model row `row` with the caret at `cursor`
    // of its text (−1: its end); false when Atrament does not draw that table, the page is read
    // only, or there is no such cell.
    bool editCell(int row, int tableRow, int column, int cursor, atrament::Context<PageView> &cx);
    // Ends the editing of the cell (what was typed stays).
    void stopEditingCell(atrament::Context<PageView> &cx);
    // Enter on a selected table (TableBlockView.focusFirstCell): its first row's first cell, the
    // header's when it has no rows.
    bool editFirstCell(int row, atrament::Context<PageView> &cx);
    // The touch toolbar's cell tools (TableBlockView's nextCell(), undoInCell(), redoInCell(),
    // closeCell()): the next cell (Tab), the page's undo / redo with the same cell edited on, the
    // cell's editing ended (its text kept, the composition committed).
    void nextCell(atrament::Context<PageView> &cx);
    void undoInCell(bool redo, atrament::Context<PageView> &cx);
    void closeCell(atrament::Context<PageView> &cx);
    // A cell started or stopped being edited: its table's model row (−1: none is edited) — the
    // editor's editingTable, the touch toolbar's cell tools.
    std::function<void(int row)> onCellEditingChanged;
    // A right-click or a finger held on a link or a chip of a cell's text (TableBlockView.cellMenu:
    // a date chip's menu, a link's) at window point `at`; in the touch selection mode a finger held
    // there is the row's long press (onLongPressRow).
    std::function<void(int row, int tableRow, int column, const QString &href, atrament::Point at)> onCellLinkMenu;
    // The edited cell's field (null before the first cell is edited).
    const atrament::Entity<atrament::TextInput> *cellInput() const { return m_cellInput ? &*m_cellInput : nullptr; }
    // Where the last frame drew cell (`tableRow`, `column`) of the table in `row` (window px), and
    // its text as laid out (nullptr: not drawn, or edited).
    std::optional<atrament::Rect> cellBounds(int row, int tableRow, int column) const;
    const atrament::TextLayoutHandle *cellText(int row, int tableRow, int column) const;
    // Where a click on drawn cell (`tableRow`, `column`) at window point `p` puts the caret: an
    // offset of the cell's text (−1: its end — a formula, a cell of several lines, not drawn).
    int cellCursorAt(int row, int tableRow, int column, atrament::Point p) const;
    // A click or a tap on a cell asks to edit it at `cursor` (the host decides: Shift / Ctrl+click
    // select blocks, the touch selection mode) — true when the host handled it; unset: editCell().
    std::function<bool(int row, int tableRow, int column, int cursor, const atrament::Modifiers &modifiers)>
            onCellEditRequest;
    // A link of a cell's text clicked at window point `at` (a date chip's dialog, a page, a tag);
    // unset: onLink.
    std::function<void(int row, int tableRow, int column, const QString &href, atrament::Point at)> onCellLink;
    // A right-click on a cell at window point `at`: the block menu with the cell's Row and Column
    // submenus (BlockEditor.openCellBlockMenuAt); unset: onBlockMenu.
    std::function<void(int row, int tableRow, int column, atrament::Point at)> onCellMenu;

    // --- The cell selection (tableselection.cpp; TableBlockView's): a rectangle of one table's
    // cells from an anchor to a cursor cell (−1 the header). Shift+arrows from the edited cell (a
    // horizontal one at its text's edge) and Shift+click on a cell of its table make it,
    // Shift+click moves its end; while it is there no cell is edited and its keys are the page's
    // (cellRangeKey: the arrows, Home / End, Tab, Enter / F2 or a typed character editing the
    // cursor cell, Delete, Ctrl+C / X / V / A / Z / Y, Esc). It ends when a cell or a block is
    // edited, blocks are selected, or with Esc.
    struct CellSelection {
        int row = -1; // the table's model row (−1: none)
        int anchorRow = -2;
        int anchorColumn = -1;
        int cursorRow = -2;
        int cursorColumn = -1;
        int top() const { return std::min(anchorRow, cursorRow); }
        int bottom() const { return std::max(anchorRow, cursorRow); }
        int left() const { return std::min(anchorColumn, cursorColumn); }
        int right() const { return std::max(anchorColumn, cursorColumn); }
    };
    CellSelection cellSelection() const;
    // Selects cells (anchor)…(cursor) of the table in `row` — the edited cell left, no block
    // selected or edited, the keys the selection's; false when there is no such cell.
    bool selectCells(int row, int anchorRow, int anchorColumn, int cursorRow, int cursorColumn,
                     atrament::Context<PageView> &cx);
    void clearCellSelection(atrament::Context<PageView> &cx);
    bool cellSelected(int row, int tableRow, int column) const;
    // The selection's rows as shown, from its top one down.
    QList<int> selectedTableRows() const;
    // Ctrl+C / Ctrl+X (TSV), Delete, Ctrl+V over the selection (its toasts: onNotice).
    void copyCells(bool cut, atrament::Context<PageView> &cx);
    void clearCells(atrament::Context<PageView> &cx);
    void pasteCells(atrament::Context<PageView> &cx);
    // --- The filter row (tablefilters.cpp): while a table filters (PageController::
    // setTableFilterOpen) a field per column under its header — typing filters its rows
    // (setTableFilter: nothing written), Esc empties a field and in an empty one hides the row and
    // selects the table, Enter leaves it —, the funnel of a filtered column in the header, and
    // under the grid how many rows show with "Clear filters". focusFilter() gives column
    // `column`'s field the keys (false: no such table, or it does not filter).
    bool focusFilter(int row, int column, atrament::Context<PageView> &cx);
    // cellBounds(row, kFilterRow, c): where the last frame drew column c's filter field;
    // cellBounds(row, kClearFiltersRow, 0): its "Clear filters".
    static constexpr int kFilterRow = -3;
    static constexpr int kClearFiltersRow = -4;
    // Column `column`'s filter field has the focus.
    bool filterFocused(int row, int column) const;
    // "2 of 3 rows shown" (TableBlockView's filterStatus).
    static QString filterStatus(const struct TableGrid &grid);
    // A toast for the host (BlockEditor.notice): the text and "success", "info" or "error".
    std::function<void(const QString &text, const QString &level)> onNotice;
    // The cell selection was made or moved (`selected`), or it ended.
    std::function<void(bool selected)> onCellSelectionChanged;

private:
    bool isTable(int row) const;
    atrament::AnyElement renderTableRow(std::size_t row, const QModelIndex &index, float textX, atrament::Div line);
    atrament::AnyElement renderTableCell(std::size_t row, quint64 uid, const struct TableGrid &grid, int r, int c,
                                         bool firstBodyRow);
    // The table of model row `row` as its `tableData` gives it now (cached while the role's map is
    // the same) and its columns' natural widths.
    struct TableEntry;
    const TableEntry *tableEntry(int row) const;
    void tableEditingRowChanged(atrament::Context<PageView> &cx);
    void cellInputChanged(atrament::Context<PageView> &cx);
    void commitCellComposition(atrament::Context<PageView> &cx);
    bool tableKey(const atrament::Keystroke &key, atrament::Window &window, atrament::Context<PageView> &cx);
    void cellClicked(std::size_t row, int tableRow, int column, const atrament::ClickEvent &click,
                     atrament::Context<PageView> &cx);
    void cellLinkClicked(std::size_t row, int tableRow, int column, const QString &href, atrament::Point at,
                         atrament::Context<PageView> &cx);
    // Cell (`tableRow`, `column`) of the table edited (its text written as typed), or a step the
    // keys take: the cell below, the next one, the row above or below the table.
    void goToCell(int row, int tableRow, int column, int cursor, atrament::Context<PageView> &cx);
    void leaveTable(int row, bool up, float x, atrament::Context<PageView> &cx);
    int columnAtX(quint64 uid, int columns, float x) const;
    void focusCellInput(atrament::Context<PageView> &cx);
    // The cell selection's keys (tableselection.cpp), and its steps.
    bool cellRangeKey(const atrament::Keystroke &key, atrament::Context<PageView> &cx);
    void extendCellsFromEdited(int tableRow, int column, atrament::Context<PageView> &cx);
    static std::pair<int, int> neighbourCell(const struct TableGrid &grid, int r, int c, std::u16string_view key);
    void pasteCellsAt(int row, int tableRow, int column, int rowCount, int columnCount, atrament::Context<PageView> &cx);
    bool editCellWith(int row, int tableRow, int column, const QString &typed, atrament::Context<PageView> &cx);
    // The filter row's fields (tablefilters.cpp).
    struct FilterField {
        atrament::Entity<atrament::TextInput> input;
        atrament::Subscription changed;
    };
    atrament::AnyElement renderFilterRow(std::size_t row, quint64 uid, const TableEntry &entry);
    std::shared_ptr<FilterField> filterField(quint64 uid, int column, const struct TableGrid &grid);
    void filterChanged(quint64 uid, int column);
    bool filterKey(quint64 uid, int column, const atrament::Keystroke &k, atrament::Window &w,
                   atrament::Context<PageView> &cx);
    // Keeps the controller's signals while the view lives (setController's).
    void connectTables(atrament::Context<PageView> &cx);
    struct Connections {
        std::vector<QMetaObject::Connection> list;
        ~Connections();
    } m_tableConnections;

    bool m_drawsTables = false;
    TableLook m_tableLook;
    std::optional<atrament::Entity<atrament::TextInput>> m_cellInput;
    std::vector<atrament::Subscription> m_cellSubscriptions;
    quint64 m_cellUid = 0; // the edited cell's table (0: none)
    int m_cellRow = -1;    // its row in the table (−1: the header)
    int m_cellColumn = -1;
    QString m_cellLastText; // what the field last gave the controller
    // ↑/↓ out of a cell into another table: the caret's x and the way it went.
    std::optional<float> m_tableNavX;
    bool m_tableNavUp = false;
    struct CellKey {
        quint64 uid = 0;
        int r = 0;
        int c = 0;
        bool operator==(const CellKey &) const = default;
    };
    struct CellKeyHash {
        std::size_t operator()(const CellKey &k) const noexcept;
    };
    struct CellFrame {
        atrament::TextLayoutHandle text;
        atrament::Rect bounds;
        std::uint64_t frame = 0;
    };
    mutable std::unordered_map<CellKey, CellFrame, CellKeyHash> m_cellFrames;
    mutable std::unordered_map<quint64, std::shared_ptr<TableEntry>> m_tables;
    std::uint64_t m_tablesPurged = 0;
    struct CellSelectionState {
        quint64 uid = 0; // the table's block (0: nothing selected)
        CellSelection cells;
    } m_cellSel;
    bool m_keepCellSelection = false;    // (an undo of the selection's own: blocks it selects end nothing)
    std::optional<CellKey> m_revealCell; // the cursor cell to bring into view in the next frame
    std::unordered_map<CellKey, std::shared_ptr<FilterField>, CellKeyHash> m_filterFields; // (uid, kFilterRow, c)
    // --- Pictures, files, embedded blocks and query results drawn by Atrament (docs/ATRAMENT.md
    // M2 step 5; mediarows.cpp, queryrows.cpp, picturecache.h). A block's pictures and files below
    // its text (EmbedView, PdfEmbedTile), its embedded pages, sections and blocks (TransclusionView)
    // and a ```query / ```dataview block's results (QueryBlockView) — a row is Atrament's when every
    // part of it is of a kind switched on and drawable, else the whole row stays the QML delegate's.
public:
    enum MediaKind : unsigned {
        Pictures = 1,      // attachments' pictures, pictures from the internet once cached, PDF thumbnails
        Files = 2,         // other attachments, PDFs without a thumbnail: their chips
        Transclusions = 4, // ![[Page]], ![[Page#Heading]], ![[Page#^id]], !((id))
        Queries = 8,       // ```query and ```dataview results (not database views: `view:`, ```base)
    };
    // The look of those parts (EmbedView's, TransclusionView's, QueryBlockView's Theme values).
    struct MediaLook {
        atrament::Color frame = atrament::rgba(0x8c959f33); // Theme.borderSubtle
        atrament::Color border = atrament::rgba(0x8c959f59); // Theme.border
        atrament::Color borderStrong = atrament::rgb(0xb5bcc4);
        atrament::Color faint = atrament::rgb(0x9aa0a6);    // Theme.fgFaint
        atrament::Color linkMissing = atrament::rgb(0x7a5ea3);
        atrament::Color danger = atrament::rgb(0xa8382d);    // Theme.dangerText
        atrament::Color backing = atrament::rgb(0xe9e5dc);   // Theme.pictureBacking
        atrament::Color chipBackground = atrament::rgba(0x8c959f1f);
        atrament::Color chipText = atrament::rgb(0x4b5563);
        atrament::Color embedFill = atrament::rgba(0xf3f1ecb3); // Theme.bgSunken at 0.7
        atrament::Color queryBackground = atrament::rgb(0xffffff); // Theme.bgElevated (also files' chips)
        atrament::Color hover = atrament::rgba(0x8c959f1a);     // Theme.bgHover
        atrament::Color focusRing = atrament::rgb(0x3b5bdb);
        float radius = 10;       // Theme.radius
        float radiusSm = 7;      // Theme.radiusSm
        float spacing = 8;       // Theme.space2
        float smallSize = 12;    // Theme.textSm
        float tinySize = 11;     // Theme.textXs
        float bodySize = 15;     // Theme.textEditor
        float h1Size = 17;       // Theme.textLg
        float h2Size = 15;       // Theme.textMd
        float headerHeight = 28; // TransclusionView's header (36 on touch)
        float indentStep = 20;   // an embedded block's indent per depth (16 compact)
        float taskBoxWidth = 24; // BlockEditor.taskBoxWidth
        int queryItems = 50;     // QueryResultModel.maxItems (20 on touch)
        bool frames = false;     // pictures' hairline frames (AppSettings.pictureFrames, `picture-frames`)
        bool lightBacking = false; // a light backing under pictures (a dark theme, `picture-backing`)
        bool dark = false;       // the app's theme: a drawing's dark preview (its embed's `darkUrl`)
        std::u16string uiFamily; // Theme.fontUi ("": the page's)
        // Words of the views (translated by the host; English by default).
        QString embeddedName = QStringLiteral("Embedded: %1");
    };
    void setDrawsMedia(unsigned kinds, const MediaLook &look, atrament::Context<PageView> &cx);
    unsigned drawsMedia() const { return m_mediaKinds; }
    // The decoded pictures (default: PictureCache::shared(), the process's).
    void setPictureCache(std::shared_ptr<PictureCache> cache, atrament::Context<PageView> &cx);
    PictureCache &pictureCache();
    // Whether row `row` has such parts (nullopt: none — the other rules decide) and is drawn
    // by Atrament with them (false: a foreign row, the QML delegate's — also while a query's source
    // is edited, which its InlineEditor does).
    std::optional<bool> drawsMediaRow(int row) const;
    // A click on an embed of row `row` (BlockListModel's `embeds` entry: a picture, a PDF, a file):
    // what EmbedView's signals open (the viewer, the PDF viewer, the attachment) — the host decides.
    // A right-click or a finger held on a picture from the internet: its menu at a window point.
    std::function<void(int row, const QVariantMap &embed)> onEmbedClick;
    std::function<void(int row, const QVariantMap &embed, atrament::Point at)> onEmbedMenu;
    // A picture from the internet not shown yet (EmbedView's placeholder: its site, "Show" — "Try
    // again" after a failure — and "Save to notebook"): `save` false asks to show it
    // (PageController::showWebPicture), true to store it in the notebook
    // (BlockEditor.downloadWebPictures); the embed's `target` is its address.
    std::function<void(int row, const QVariantMap &embed, bool save)> onWebPicture;
    // A link of an embedded block's text clicked: its file, line and Markdown (a date chip's dialog,
    // a task's place: TransclusionView.activate). Unset: onLink with the link.
    std::function<void(int row, const QString &link, const QString &path, int line, const QString &raw)>
            onEmbeddedLink;
    // A link of a query result's text clicked (QueryBlockView: a date chip's dialog, else
    // QueryResultModel::activateLink — activateQueryLink()); the model's answer to a click on an
    // item ({ action: "scrollTo", row } on this page: BlockEditor.revealRow; "notFound").
    std::function<void(int row, int item, const QString &link, const QString &path, int line, const QString &raw)>
            onQueryLink;
    std::function<void(int row, const QVariantMap &answer)> onQueryAnswer;
    // A right-click or a finger held on a link of an embedded block's text (a chip, a tag, a page) /
    // of a query result's text (not the item's quiet page link "open:"): its menu at window point
    // `at` — the token's in the block's own file, else the link's (TransclusionView.linkMenu,
    // QueryBlockView.resultMenu) — instead of the block's menu or the touch selection mode.
    // Unset: those links have no menu of their own.
    std::function<void(int row, const QString &link, const QString &path, int line, const QString &raw, atrament::Point at)>
            onEmbeddedLinkMenu;
    std::function<void(int row, int item, const QString &link, const QString &path, int line, const QString &raw,
                       atrament::Point at)>
            onQueryLinkMenu;
    // The texts of embedded blocks and query results as the last frame laid them out — the row, a
    // result's item (−1: an embedded block's text), the links given to it (their ranges and
    // hrefs) and its layout: where their links are (linkPoint(), the self-tests' gestures; tests).
    struct PartText {
        int row = -1;
        int item = -1;
        std::vector<std::pair<atrament::Range, QString>> links;
        atrament::TextLayoutHandle handle;
    };
    const std::vector<PartText> &partTexts() const { return m_partTexts; }
    // What QueryResultModel::activateLink answers for item `item` of query row `row`'s results.
    QVariantMap activateQueryLink(int row, int item, const QString &link);
    // The results of query row `row` drawn in the last frames (nullptr: none).
    QueryResultModel *queryResults(int row) const;
    // Where the last frame drew pictures: row, the embed's index, window bounds, whether decoded.
    // Its spinner turning (the picture loading: decoded, or fetched from the internet), the frame
    // of an animation drawn (−1: a still picture).
    struct PictureFrame {
        int row = -1;
        int embed = -1;
        atrament::Rect bounds;
        bool decoded = false;
        bool loading = false;
        int frame = -1;
    };
    const std::vector<PictureFrame> &pictureFrames() const { return m_pictureFrames; }

private:
    struct Media; // mediarows.cpp: the frames' state (pictures held, query models, parsed embeds)
    std::optional<atrament::AnyElement> mediaRow(std::size_t row, const QModelIndex &index);
    atrament::AnyElement embedsPart(std::size_t row, const QVariantList &embeds, float width);
    atrament::AnyElement webPending(std::size_t row, const QVariantMap &embed, int index);
    atrament::AnyElement transclusionsPart(std::size_t row, quint64 uid, const QVariantList &transclusions);
    atrament::AnyElement queryRow(std::size_t row, const QModelIndex &index, float textX);
    void mediaClicked(std::size_t row, const atrament::ClickEvent &click);
    void mediaFrame();
    unsigned m_mediaKinds = 0;
    MediaLook m_mediaLook;
    bool m_mediaInner = false; // renderRow() draws a row's text for mediaRow()
    std::shared_ptr<Media> m_media;
    std::vector<PictureFrame> m_pictureFrames;
    std::uint64_t m_pictureFrame = 0;
    std::vector<PartText> m_partTexts;
    // A text of an embedded block or a query result recorded for the frame (partTexts()).
    PartText &partText(std::size_t row, int item);
    // --- The edited text's spell marks, reminder chips and Vim keys; code blocks' line numbers
    // (pagespell.cpp, pagechips.cpp, pagevim.cpp, pagecode.cpp; docs/ATRAMENT.md M2 step 2's and
    // step 5's entries — what InlineEditor, MarkdownHighlighter, spellmarks.h and CodeBlockView do).
public:
    // Spell checking (docs/UI.md "Spell checking"): misspelled words underlined with a wavy line in
    // `color` in the edited text's prose (not in code) and, while the checker's `wholePage` is on,
    // in the drawn rows (each word shown as it is written: spellmarks.h's rule). Null: none.
    void setSpellCheck(SpellCheck *spell, atrament::Color color, atrament::Context<PageView> &cx);
    struct Misspelling {
        int row = -1;
        bool edited = false; // in the edited text: from / to of its editText; else of the block's body
        int from = 0;
        int to = 0;
        QString word; // as checked ("np." with its dot)
        QString text; // as written
    };
    // The misspelled word at window point `p` of row `row` (the edited one, or a drawn one).
    std::optional<Misspelling> misspelledAt(int row, atrament::Point p) const;
    // The display ranges of a drawn row's text marked as misspelled now (tests).
    std::vector<atrament::Range> spellMarks(int row) const;
    // A right-click on a misspelled word (on touch UIs a tap on one in the edited text): its menu
    // at window point `at` (BlockEditor's spelling items). Unset: the block menu as elsewhere.
    std::function<void(const Misspelling &, atrament::Point at)> onSpellingMenu;

    // The edited text's own menu (pagetextmenu.cpp; InlineEditor's right-click, BlockEditor.
    // openTextMenu: Cut, Copy, Paste, Highlight…, the AI's, Select all): a right-click inside the
    // edited block's field — the input method's word committed, the caret moved to the pointer
    // unless it is inside the selection (the menu acts on it) — at window point `at`, with the
    // misspelled word there if any (its corrections first). Beside the field (the bullet, the
    // margin) a right-click is the block's menu. Unset: the edited text's right-click is the
    // spelling's menu on a misspelled word, else the block's, as on a drawn row.
    std::function<void(int row, const std::optional<Misspelling> &spelling, atrament::Point at)> onTextMenu;
    // The menu's commands on the edited text, as its keys do them (editorKey's Ctrl+X / C / V / A):
    // the controller's exact copy and cut (no-break spaces kept; the field's own when it cannot),
    // its paste (Markdown into blocks, HTML as Markdown, a copied picture as an attachment; a
    // source — code, a diagram's, a formula's — takes the text as it is: the field's paste), the
    // field's whole text selected. False when nothing was done (no block edited, nothing selected
    // to copy, reading mode, nothing to paste); `result` gets the controller's paste answer (its
    // row, pictures from the internet it brought: BlockEditor.offerPastedPictures).
    enum class TextCommand : std::uint8_t { Cut, Copy, Paste, SelectAll };
    bool textCommand(TextCommand command, atrament::Context<PageView> &cx, QVariantMap *result = nullptr);

    // Reminder chips while editing (InlineEditor's, markdownstyles.h's LineChip): each ⏰ / ⏰!
    // token of the edited text is laid out as the pill of its words ("⏰ jutro 18:00"), one unit for
    // the caret (←/→ step over it, Backspace / Delete remove it whole, a caret put inside goes to
    // its end) while the text keeps the token as written. A click on one (`menu` false: the
    // reminder's dialog), a right-click or a finger held on it (`menu` true: its menu) — the
    // token's editText range, the window point. On by default.
    void setReminderChips(bool on, atrament::Context<PageView> &cx);
    std::function<void(int row, int from, int to, bool menu, atrament::Point at)> onReminderChip;
    // The edited text's chips as last styled (editText ranges; tests).
    const std::vector<LineChip> &editChips() const { return m_editChips; }

    // Vim (docs/UI.md "Vim mode"): the editor's VimSession takes the edited block's keys first, by
    // BlockEditor.handleEditorKey's rules; in its normal, visual and replace modes the field takes
    // no typing (no input method either) and its caret is a steady block (an underline in replace
    // mode), Vim's visual selection in the block is the field's selection, zt / zz / zb scroll the
    // list, H M L are told the rows in view. `onVimKey` after each key Vim took
    // (BlockEditor.afterVimKey: the keyboard to the list when Vim selected blocks).
    void setVim(VimSession *vim, atrament::Context<PageView> &cx);
    VimSession *vim() const;
    std::function<void()> onVimKey;

private:
    // Spell checking (pagespell.cpp).
    QPointer<SpellCheck> m_spell;
    atrament::Color m_spellColor = atrament::rgb(0xc0392b);
    std::vector<QMetaObject::Connection> m_spellConnections;
    struct EditSpell {
        std::vector<spell::WordSpan> wrong; // the edited text's misspelled words when last styled
        int suppressed = -1;                // the end of the one left unmarked (the caret after it)
        int caret = -1;                     // the caret styling sees
    } m_editSpell;
    bool editSpellActive() const;
    void appendEditSpelling(std::u16string_view text, const std::vector<EditStyler::Extras::Prose> &prose,
                            std::vector<atrament::TextSpan> &spans);
    void appendRowSpelling(int row, const markdown::RenderedText &text, std::vector<atrament::TextSpan> &spans) const;
    void editCaretMoved(int caret, atrament::Context<PageView> &cx);
    bool spellingMenuAt(int row, atrament::Point p);
    // The edited text's menu (pagetextmenu.cpp): a right-click at `p` inside the field of `row`;
    // the controller's copy (cut) of the field's selection and its paste there — false when it
    // did not (nothing selected, the clipboard without text).
    bool textMenuAt(int row, atrament::Point p, atrament::Context<PageView> &cx);
    bool copyEdited(bool cut, atrament::Context<PageView> &cx);
    bool pasteEdited(atrament::Context<PageView> &cx, QVariantMap *result);
    void editedRowTapped(int row, const atrament::MouseUpEvent &e);
    void restyleInput(atrament::Context<PageView> &cx);
    // The edited text's spans: the highlighter's, the find bar's, the spelling's, the chips'.
    std::vector<atrament::TextSpan> editSpans(std::u16string_view text, bool code, bool luau);
    // Reminder chips (pagechips.cpp).
    bool m_reminderChips = true;
    std::vector<LineChip> m_editChips;
    std::u16string m_editChipsText; // the text they were found in
    void appendEditChips(const std::vector<LineChip> &chips, std::vector<atrament::TextSpan> &spans);
    void chipActivated(int from, int to, bool menu, atrament::Point at, atrament::Context<PageView> &cx);
    // A caret strictly inside a chip goes to its end, a selection's moving end over the whole chip.
    bool snapCaretOutOfChips(atrament::Context<PageView> &cx);
    // Vim (pagevim.cpp).
    QPointer<VimSession> m_vim;
    std::vector<QMetaObject::Connection> m_vimConnections;
    struct VimSelection {
        int row = -1;
        int anchor = 0;
        int cursor = 0;
    } m_vimSelection;
    bool vimKey(const atrament::Keystroke &key, atrament::Context<PageView> &cx);
    void applyVimLook(atrament::Context<PageView> &cx);
    void vimSelectionRequested(int row, int anchor, int cursor, atrament::Context<PageView> &cx);
    void answerVimView();
    // Code blocks' line numbers (pagecode.cpp): CodeBlockView's gutter, each line of the code
    // numbered on its first visual line.
    atrament::AnyElement codeLineNumbers(std::size_t row, const QString &code, bool edited);
    // --- Block selection: its keys, its band and grips, drags that select blocks or move them
    // (pageselection.cpp, pagedrag.cpp; docs/ATRAMENT.md "The block editor in Atrament (M2)", steps
    // 3 "Selection" and 4 "Structure": drag to move). The QML editor's counterparts:
    // BlockEditor.handleSelectionKeyCode, BlockPartSelectionBand, BlockSelectionGrip, its drag.
public:
    // What a selection key asks of the host first (the QML editor's own parts: toasts, menus, the
    // journal stream's selection over several days, gaps it explains); its name is the QML's.
    enum class SelectionCommand : std::uint8_t {
        Copy,           // Ctrl+C, Ctrl+Insert (default: PageController::copySelection)
        Cut,            // Ctrl+X, Shift+Delete (cutSelection)
        Delete,         // Delete, Backspace (deleteSelection)
        Paste,          // Ctrl+V, Shift+Insert (pasteOverSelection)
        Escape,         // Esc (clearSelection)
        Back,           // Android's Back (nothing: the shell's)
        Edit,           // Enter (startEditing the selected block)
        TaskMenu,       // Ctrl+Shift+Enter (nothing)
        SourceMode,     // Ctrl+E (nothing)
        CopyReference,  // Ctrl+Shift+C (nothing)
        // Asked before the view does them itself: the host takes them only to refuse (a selection
        // with gaps, over several days) or to do them its way (the journal stream's days).
        Indent, Outdent, MoveUp, MoveDown, CycleTasks, ExtendUp, ExtendDown,
        ExtendPastTop,  // Shift+↑ / Shift+↓ at the page's first / last block (the day above / below)
        ExtendPastBottom,
        Extended,       // after Shift+↑/↓ moved the selection's end (touch: the selection's tools)
    };
    static std::u16string_view commandName(SelectionCommand command);
    // The host's part of a selection key: true when it took the command (did it, or refused it with
    // a word); unset or false: the view's default (above).
    std::function<bool(SelectionCommand command, int row)> onSelectionCommand;
    // A key while no block is edited (blocks selected, or none yet): Shift+↑/↓ extend or shrink the
    // selection (past the page's edge: the host's), ↑/↓ move it (with none: the first / last
    // block), Alt+↑/↓ move the blocks, Tab / Shift+Tab indent them, Ctrl+Enter their tasks' next
    // status, Ctrl+. folds, Enter edits, Delete / Backspace delete, Ctrl+C / X / V, Ctrl+A every
    // block, Ctrl+Z / Ctrl+Y undo (onUndo), Esc. True when it took the key.
    bool selectionKey(const atrament::Keystroke &key, atrament::Context<PageView> &cx);
    // The touch selection mode: its grips at the band's ends (BlockEditor.selectionGrips) and its
    // "One by one" (`picking`: a long press picks a block, no drag follows).
    void setTouchSelection(bool grips, bool picking, atrament::Context<PageView> &cx);
    bool touchGrips() const { return m_touchGrips; }
    // The grips' look (Theme.link, Theme.bg) and the drop line's (Theme.focusRing).
    void setDragLook(atrament::Color grip, atrament::Color gripBorder, atrament::Color dropLine,
                     atrament::Context<PageView> &cx);
    // The room an overlay covers at the view's bottom (the touch toolbar): a drag scrolls the page
    // above it.
    void setDragBottomInset(float px) { m_dragBottomInset = std::max(0.0f, px); }
    // The selection's band in the window by the last frame: its first and last rows shown (nothing
    // when none is) — where the host may place the selection's tools.
    std::optional<atrament::Rect> selectionBounds() const;
    // A drag of blocks in progress: selecting (a mouse's press dragged across blocks, a finger's
    // after a long press, a grip) or moving them (a bullet's, a selected block's long press).
    enum class DragKind : std::uint8_t { None, Select, Extend, Move };
    DragKind dragKind() const { return m_drag.active ? m_drag.kind : DragKind::None; }
    // Where the blocks being moved would go: before or after sibling `sibling` (its subtree), the
    // line drawn there (window px, by the last frame). Nothing when there is no such place (the
    // pointer over the blocks' own place).
    struct DropTarget {
        int sibling = -1;
        bool after = false;
        atrament::Rect line;
    };
    std::optional<DropTarget> dropTarget() const;
    // The edge scrolling's speed now (px per 16 ms; 0: none).
    float dragScrollSpeed() const { return m_edgeScroll.speed(); }

private:
    // The band under a selected row (BlockPartSelectionBand's geometry), its grips in the touch
    // selection mode; null when the row is not selected.
    std::optional<atrament::AnyElement> selectionBand(int row);
    atrament::AnyElement selectionGrip(bool atStart, float left, float right);
    // A long press on a drawn row: the touch selection mode (onLongPressRow) and a drag that
    // extends it, or — on a block of the selection while its grips show — a drag that moves it.
    void rowLongPressed(int row, atrament::Point p, atrament::Window &window, atrament::Context<PageView> &cx);
    // A press on an outline's bullet (a list item's marker): a drag past a few px moves the block.
    void bulletPressed(int row, atrament::Point p, atrament::Context<PageView> &cx);
    void beginDrag(DragKind kind, int fixedRow, atrament::Point p, bool active, atrament::Context<PageView> &cx);
    void dragMoved(atrament::Point p, atrament::Context<PageView> &cx);
    void dragReleased(bool cancelled, atrament::Context<PageView> &cx);
    void dragUpdate(atrament::Context<PageView> &cx);
    // A move of this pointer belongs to the drag (a mouse's to a mouse's, a finger's or a pen's to
    // theirs).
    bool followsPointer(atrament::PointerKind pointer) const;
    void updateDropTarget();
    // The row under window point `p` for a drag (the nearest above or below the rows, the pointer
    // held into the view), as the rows are now (scrolled since the last frame: by that much).
    int dragRowAt(atrament::Point p) const;
    float scrolledSinceFrame() const;
    std::optional<atrament::Rect> listViewport() const;
    // What the frame paints for drags: the drop line, the listeners that follow a drag (capture:
    // the rows under it see none of its moves, nothing is clicked by its release).
    atrament::AnyElement dragLayer();
    // The selection's keys: a listener on the window's root dispatch node, which every key reaches
    // (nothing focused, or after the focused element and the rows around it passed it on).
    atrament::AnyElement keyLayer();
    struct BlockDrag {
        DragKind kind = DragKind::None;
        bool active = false;  // a bullet's press becomes a drag only past a few px
        quint64 uid = 0;      // Move: the pressed block; Extend: the fixed end's block
        atrament::Point start;
        atrament::Point last;
        atrament::PointerKind pointer = atrament::PointerKind::Mouse;
        std::optional<DropTarget> drop;
    } m_drag;
    atrament::EdgeScroller m_edgeScroll;
    bool m_touchGrips = false;
    bool m_touchPicking = false;
    float m_dragBottomInset = 0;
    float m_viewWidth = 0;
    atrament::Color m_gripColor = atrament::rgb(0x3b5bdb);
    atrament::Color m_gripBorder = atrament::rgb(0xffffff);
    atrament::Color m_dropColor = atrament::rgb(0x3b5bdb);
    QString m_gripNames[2];
};

} // namespace atr
} // namespace brulion::editor
