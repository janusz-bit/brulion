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

#include "foreignrows.h"
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
class EditStyler;
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
    // cell's typing and selects the table, Ctrl+Z / Ctrl+Y the page's undo (the cell stays edited).
    // Off, and for a table showing its filter row: tables are foreign rows (the QML delegate).
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
        std::u16string fontFamily = u"Brulion Inter";                   // Theme.fontUi
        std::u16string monoFamily = u"Brulion JetBrains Mono";          // Theme.fontMono: a formula edited
        float fontSize = 14;                                            // Theme.textBase
        float lineHeight = 20;                                          // Theme.lineHeightPx(textBase, 1.4)
        float rowHeight = 32;                                           // 40 on touch UIs
        float cellPadding = 12;                                         // Theme.space3
        float radius = 10;                                              // Theme.radius
        float buttonHeight = 28;                                        // Theme.controlHeightSm
        float buttonTextSize = 12;                                      // Theme.textSm
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
};

} // namespace atr
} // namespace brulion::editor
