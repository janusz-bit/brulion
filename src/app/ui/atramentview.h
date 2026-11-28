// AtramentView — an Atrament view inside the app's QML (docs/ATRAMENT.md D12, M2): the way views move
// from QML to Atrament one at a time. `AtramentView { view: "gallery" }` shows the named view of
// the process's one Atrament App (its fonts: the app's bundled ones, ":/fonts/ui" and ":/fonts").
// Built where Atrament is (BRULION_ATRAMENT: with the app on every platform); `brulion --atrament`
// opens qml/dev/AtramentDemo.qml (developer QML: desktop builds, and debuggable APKs — `adb shell am
// start … --es applicationArguments --atrament`).
#pragma once

#include "qthost.h"

#include <QColor>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QRectF>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <unordered_map>

namespace brulion::atrament {
class App;
}

class AtramentView;
namespace brulion::editor {
class PageController;
}
namespace brulion::editor::atr {
class JournalView;
class MediaSource;
class PageView;
}
namespace brulion::atrament {
template<class T>
class Context;
template<class T>
class Entity;
}

// A table drawn by Atrament as BlockEditor reaches a TableBlockView — by name, through QmlCall
// (BlockEditor.tableAt, editingTable, BlockMenu.readPicked, MobileEditToolbar's cell tools): the
// same functions and properties, for the table of one block of an AtramentView's page
// (AtramentView::tableAt). An item that is never shown (no parent item); the view owns it.
class AtramentTableItem : public QQuickItem {
    Q_OBJECT
    // The table's model row (−1: gone), the edited cell (−2: none, −1 the header) and its column,
    // the table's columns and rows, its filter row shown, cells selected, the edited cell's field
    // with the keyboard.
    Q_PROPERTY(int blockIndex READ blockIndex NOTIFY changed FINAL)
    Q_PROPERTY(int editRow READ editRow NOTIFY changed FINAL)
    Q_PROPERTY(int editColumn READ editColumn NOTIFY changed FINAL)
    Q_PROPERTY(int columns READ columns NOTIFY changed FINAL)
    Q_PROPERTY(bool filtering READ filtering NOTIFY changed FINAL)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY changed FINAL)
    Q_PROPERTY(bool selecting READ hasSelection NOTIFY changed FINAL)
    Q_PROPERTY(bool cellHasKeys READ cellHasKeys NOTIFY changed FINAL)
public:
    AtramentTableItem(AtramentView *view, quint64 uid);
    quint64 uid() const { return m_uid; }
    int blockIndex() const;
    int editRow() const;
    int editColumn() const;
    int columns() const;
    bool filtering() const;
    bool hasSelection() const;
    bool cellHasKeys() const;
    // TableBlockView's functions of the same names.
    Q_INVOKABLE void startCell(int r, int c);
    Q_INVOKABLE void focusFirstCell();
    Q_INVOKABLE void openFilter(int c);
    Q_INVOKABLE bool inSelection(int r, int c) const;
    Q_INVOKABLE QList<int> selectedRows() const;
    Q_INVOKABLE int selectionTop() const;
    Q_INVOKABLE int selectionBottom() const;
    Q_INVOKABLE int selectionLeft() const;
    Q_INVOKABLE int selectionRight() const;
    Q_INVOKABLE int selectedCellCount() const;
    Q_INVOKABLE void select(int anchorRow, int anchorColumn, int cursorRow, int cursorColumn);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void nextCell();
    Q_INVOKABLE void undoInCell();
    Q_INVOKABLE void redoInCell();
    Q_INVOKABLE void closeCell();
    Q_INVOKABLE int cellLength() const;
    Q_INVOKABLE int cellRowCount() const;
    Q_INVOKABLE QString cellAlignOf(int c) const;
    Q_INVOKABLE QString rawText(int r, int c) const;
    Q_INVOKABLE void commitComposition();
    // For the self-tests: the edited cell's text and caret; where cell (r, c) was drawn in the
    // view's coordinates (r −1 the header, −3 the filter fields; an empty rectangle: not drawn);
    // the header's texts, the rows shown.
    Q_INVOKABLE QString cellText() const;
    Q_INVOKABLE int cellCursor() const;
    Q_INVOKABLE QRectF cellRect(int r, int c) const;
    Q_INVOKABLE QStringList headerTexts() const;
    Q_INVOKABLE QList<int> shownRows() const;
    Q_INVOKABLE QString headerText(int c) const;
    Q_INVOKABLE bool isRowShown(int r) const;
    Q_INVOKABLE int shownRowCount() const;
    Q_INVOKABLE bool filterFocused(int c) const;
    Q_INVOKABLE QString filterStatusText() const;
    Q_INVOKABLE qreal cellCenterX(int r, int c) const;
    Q_INVOKABLE qreal cellCenterY(int r, int c) const;

Q_SIGNALS:
    void changed();

private:
    // The page drawn by the view (null: none), and a step on it with the table's model row.
    const brulion::editor::atr::PageView *page() const;
    using Step = std::function<void(brulion::editor::atr::PageView &,
                                    brulion::atrament::Context<brulion::editor::atr::PageView> &, int row)>;
    void step(const Step &f);
    QPointer<AtramentView> m_view;
    quint64 m_uid = 0;
};

class AtramentView : public brulion::atrament::AtramentItem {
    Q_OBJECT
    QML_ELEMENT
    // The view to show: "gallery" (the framework's gallery) or "page" (the blocks of `pageModel`
    // drawn by Atrament: editor/atrament/pageview.h, AppSettings.editorAtrament). Set once.
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged FINAL)
    // "page": the editor's BlockListModel (a PageController's `blocks`), set before the view.
    Q_PROPERTY(QObject *pageModel READ pageModel WRITE setPageModel NOTIFY pageModelChanged FINAL)
    // A view other than the editor's ("tasks": a TaskListModel, "search": a SearchResultModel,
    // "agenda": a CalendarModel; docs/ATRAMENT.md "The views in Atrament (M3)"), set before the view
    // is shown; its look (TasksLook's, SearchLook's, AgendaLook's fields by name — colours, sizes,
    // uiFamily).
    Q_PROPERTY(QObject *viewModel READ viewModel WRITE setViewModel NOTIFY viewModelChanged FINAL)
    Q_PROPERTY(QVariantMap viewLook READ viewLook WRITE setViewLook NOTIFY viewLookChanged FINAL)
    // "page": the editor's look (BlockEditor's metrics and Theme colours).
    Q_PROPERTY(qreal fontSize MEMBER m_fontSize NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal lineHeight MEMBER m_lineHeight NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal indentStep MEMBER m_indentStep NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal gutter MEMBER m_gutter NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal bulletColumn MEMBER m_bulletColumn NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool documentMode MEMBER m_documentMode NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal listIndent MEMBER m_listIndent NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal contentX MEMBER m_contentX NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal contentWidth MEMBER m_contentWidth NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal arrowColumn MEMBER m_arrowColumn NOTIFY styleChanged FINAL)
    // Callouts' frames: the colours of their types, the fill's opacity, the corner radius.
    Q_PROPERTY(QColor calloutInfo MEMBER m_calloutInfo NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor calloutSuccess MEMBER m_calloutSuccess NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor calloutWarning MEMBER m_calloutWarning NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor calloutDanger MEMBER m_calloutDanger NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor calloutQuote MEMBER m_calloutQuote NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal calloutFill MEMBER m_calloutFill NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal calloutRadius MEMBER m_calloutRadius NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool touchUi MEMBER m_touchUi NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor textColor MEMBER m_textColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor mutedColor MEMBER m_mutedColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor linkColor MEMBER m_linkColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor guideColor MEMBER m_guideColor NOTIFY styleChanged FINAL)
    // A thematic break's line (BlockPartRule: Theme.border).
    Q_PROPERTY(QColor ruleColor MEMBER m_ruleColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor backgroundColor MEMBER m_backgroundColor NOTIFY styleChanged FINAL)
    // "page", editing (docs/ATRAMENT.md M2 step 2): the editor's PageController (its `blocks` is the
    // page model, which it sets when none is given); without one the page is read only. The edited
    // text's caret and selection colours, an empty block's placeholder.
    Q_PROPERTY(QObject *pageController READ pageController WRITE setPageController NOTIFY pageControllerChanged FINAL)
    Q_PROPERTY(QColor caretColor MEMBER m_caretColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor selectionColor MEMBER m_selectionColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QString placeholderText MEMBER m_placeholderText NOTIFY styleChanged FINAL)
    Q_PROPERTY(QString editLabel MEMBER m_editLabel NOTIFY styleChanged FINAL)
    // The edited block's Markdown colours (BlockEditor.highlighterPalette: MarkdownHighlighter's
    // keys) and the code face; the field is coloured by the highlighter's rules (editstyles.h).
    Q_PROPERTY(QVariantMap highlighterPalette MEMBER m_highlighterPalette NOTIFY styleChanged FINAL)
    // Code blocks drawn and edited by Atrament (off: the QML delegate hosts them), and their look:
    // { background, border, label (colours), radius, fontSize, lineHeight, labelSize } (CodeBlockView's
    // Theme values; pageview.h CodeLook).
    Q_PROPERTY(bool drawsCode MEMBER m_drawsCode NOTIFY styleChanged FINAL)
    Q_PROPERTY(QVariantMap codeLook MEMBER m_codeLook NOTIFY styleChanged FINAL)
    // Calculations drawn and edited by Atrament (CalcBlockView's look: PageView::CalcLook's keys —
    // background, frame, separator, rule, label, muted, error colours; radius, labelSize,
    // errorSize; uiFamily, title).
    Q_PROPERTY(bool drawsCalc MEMBER m_drawsCalc NOTIFY styleChanged FINAL)
    Q_PROPERTY(QVariantMap calcLook MEMBER m_calcLook NOTIFY styleChanged FINAL)
    // Charts and Mermaid diagrams, math formulas drawn and edited by Atrament (on by default; off:
    // the QML views host them), and their look: { panel, panelBorder, editingBorder, label, text,
    // muted, border, accent, danger (colours), dark, radius, framedPad, codeSize, codeLineHeight, labelSize,
    // chartFontSize, mermaidFontSize, family, english } (the QML views' Theme values; pageview.h
    // ViewLook).
    Q_PROPERTY(bool drawsDiagrams MEMBER m_drawsDiagrams NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool drawsMath MEMBER m_drawsMath NOTIFY styleChanged FINAL)
    Q_PROPERTY(QVariantMap diagramLook MEMBER m_diagramLook NOTIFY styleChanged FINAL)
    // Pictures, files, embedded blocks and query results drawn by Atrament (docs/ATRAMENT.md M2 step
    // 5; off: the rows with them are the QML delegate's): pictures (attachments, cached ones from the
    // internet, PDF thumbnails), files' chips, embedded pages / sections / blocks, ```query and
    // ```dataview results; their look { frame, border, borderStrong, faint, linkMissing, danger,
    // backing, chipBackground, chipText, embedFill, queryBackground, hover, focusRing (colours),
    // radius, radiusSm, spacing, smallSize, tinySize, bodySize, h1Size, h2Size, headerHeight,
    // indentStep, taskBoxWidth, queryItems (numbers), frames, lightBacking, dark (bools), uiFamily }
    // — a drawing's preview comes from the app's "drawing" image provider (DrawingImageProvider,
    // registered with the page's PictureCache), in the dark theme its dark version
    // (EmbedView's, TransclusionView's, QueryBlockView's Theme values; pageview.h MediaLook).
    Q_PROPERTY(bool drawsPictures MEMBER m_drawsPictures NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool drawsFiles MEMBER m_drawsFiles NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool drawsTransclusions MEMBER m_drawsTransclusions NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool drawsQueries MEMBER m_drawsQueries NOTIFY styleChanged FINAL)
    // Sounds and videos as cards drawn by Atrament over the app's player (MediaPlayback; else the
    // QML MediaEmbedTile's rows).
    Q_PROPERTY(bool drawsSounds MEMBER m_drawsSounds NOTIFY styleChanged FINAL)
    Q_PROPERTY(QVariantMap mediaLook MEMBER m_mediaLook NOTIFY styleChanged FINAL)
    // The find bar's query and options (NoteFind): its matches tinted with findColor.
    Q_PROPERTY(QString findQuery MEMBER m_findQuery NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchCase MEMBER m_findMatchCase NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findMatchDiacritics MEMBER m_findMatchDiacritics NOTIFY findChanged FINAL)
    Q_PROPERTY(bool findWholeWord MEMBER m_findWholeWord NOTIFY findChanged FINAL)
    Q_PROPERTY(QColor findColor MEMBER m_findColor NOTIFY findChanged FINAL)
    // Selected blocks' band (Theme.bgSelected, Theme.focusRing, Theme.radiusSm).
    Q_PROPERTY(QColor bandColor MEMBER m_bandColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor ringColor MEMBER m_ringColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal bandRadius MEMBER m_bandRadius NOTIFY styleChanged FINAL)
    // The editor's suggestion list is open: its keys (↑, ↓, Enter, Tab, Esc) go to it
    // (suggestionKeyPressed) before the field's or the editor's meaning.
    Q_PROPERTY(bool suggestionsOpen MEMBER m_suggestionsOpen NOTIFY suggestionsOpenChanged FINAL)
    // The edited block's field (−1, "" while none): its row, text (a composition included), caret,
    // and the caret's rectangle in this item.
    // "page": the blocks' height (measured rows exact, the others estimated): a journal day is as
    // tall as its blocks (BlockEditor.autoHeight).
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY contentHeightChanged FINAL)
    Q_PROPERTY(int editRow READ editRow NOTIFY editStateChanged FINAL)
    Q_PROPERTY(QString editText READ editText NOTIFY editStateChanged FINAL)
    Q_PROPERTY(int editCursor READ editCursor NOTIFY editStateChanged FINAL)
    // The edited field holds a block's prose (the suggestions follow it); false for code, a
    // calculation, a diagram's or a formula's source.
    Q_PROPERTY(bool editProse READ editProse NOTIFY editStateChanged FINAL)
    Q_PROPERTY(QRectF editCaretRect READ editCaretRect NOTIFY editStateChanged FINAL)

    // "page": the rows Atrament does not draw yet (foreign rows: code, tables, math, pictures,
    // queries, the properties…) are `delegate` (the editor's BlockDelegate) placed over them, and
    // `header` / `footer` are the items above the blocks and below them (the title, the
    // properties, the zoom's breadcrumb; the room to tap under the last block) — the list's rows
    // too, scrolling with it (editor/atrament/foreignhost.h; qthost.h "Hosted children").
    Q_PROPERTY(QQmlComponent *delegate READ delegate WRITE setDelegate NOTIFY delegateChanged FINAL)
    Q_PROPERTY(QQuickItem *header READ header WRITE setHeader NOTIFY headerChanged FINAL)
    Q_PROPERTY(QQuickItem *footer READ footer WRITE setFooter NOTIFY footerChanged FINAL)
    // "journal": a day zoomed into a block gets one of these above its blocks — the zoom's
    // breadcrumb (ContentJournalDay's, in its editor's header), a required property `controller` the
    // day's PageController — made while the zoom lasts, placed over its page's header as a page's
    // header is.
    Q_PROPERTY(QQmlComponent *dayHeader READ dayHeader WRITE setDayHeader NOTIFY dayHeaderChanged FINAL)
    // "page": tables drawn and edited by Atrament (editor/atrament/tablerows.cpp, their cell
    // selection and filter row too; off: the QML delegate hosts them), and their look: { background,
    // header, border, borderStrong, borderSubtle, focusRing, formula, error, input, hover, picked,
    // accent, muted (colours), fontFamily, monoFamily, fontSize, lineHeight, rowHeight, cellPadding,
    // radius, buttonHeight, buttonTextSize, filterHeight, fieldTextSize, statusTextSize }
    // (TableBlockView's Theme values; pageview.h TableLook).
    Q_PROPERTY(bool drawsTables MEMBER m_drawsTables NOTIFY styleChanged FINAL)
    Q_PROPERTY(QVariantMap tableLook MEMBER m_tableLook NOTIFY styleChanged FINAL)
    // "page": cells of a table drawn by Atrament are selected (their keys are the page's: its Ctrl
    // keys win over the application's shortcuts).
    Q_PROPERTY(bool cellsSelected READ cellsSelected NOTIFY cellSelectionChanged FINAL)
    // "page": the model row of the table whose cell is edited (−1: none).
    Q_PROPERTY(int editTableRow READ editTableRow NOTIFY tableEditingChanged FINAL)

    // "page", the edited text's extras (docs/ATRAMENT.md M2 step 2: spell marks, reminder chips,
    // Vim): the app's SpellCheck (the QML singleton; null: no marks) and the marks' colour
    // (Theme.danger) — the edited text's prose, and the drawn rows while SpellCheck.wholePage; the
    // editor's VimSession (BlockEditor.vimSession; its keys before the field's, the caret's shape).
    Q_PROPERTY(QObject *spellCheck READ spellCheck WRITE setSpellCheck NOTIFY spellCheckChanged FINAL)
    Q_PROPERTY(QColor spellColor MEMBER m_spellColor NOTIFY spellCheckChanged FINAL)
    Q_PROPERTY(QObject *vimSession READ vimSession WRITE setVimSession NOTIFY vimSessionChanged FINAL)
    // The edited block's selection [editSelectionStart, editSelectionEnd) (the touch toolbar's
    // highlight).
    Q_PROPERTY(int editSelectionStart READ editSelectionStart NOTIFY editStateChanged FINAL)
    Q_PROPERTY(int editSelectionEnd READ editSelectionEnd NOTIFY editStateChanged FINAL)
    // "page", the block selection (docs/ATRAMENT.md M2 steps 3 and 4: pageselection.cpp,
    // pagedrag.cpp): the touch selection mode's grips shown (BlockEditor.selectionGrips) and its
    // "One by one" (BlockEditor.pickMode); the room the touch toolbar covers at the bottom (a drag
    // scrolls the page above it); the selection's band in this item (its rows shown; empty when
    // none is) for the selection's tools.
    Q_PROPERTY(bool selectionGrips MEMBER m_selectionGrips NOTIFY touchSelectionChanged FINAL)
    Q_PROPERTY(bool selectionPicking MEMBER m_selectionPicking NOTIFY touchSelectionChanged FINAL)
    Q_PROPERTY(qreal dragBottomInset MEMBER m_dragBottomInset NOTIFY touchSelectionChanged FINAL)
    Q_PROPERTY(QRectF selectionRect READ selectionRect NOTIFY selectionRectChanged FINAL)

    // "journal" (docs/ATRAMENT.md "The journal stream as one list"; editor/atrament/journalview.h):
    // the journal stream as one list. Its days are `daysModel`'s (JournalListModel); each day near the
    // view gets a PageController made here — `editorHost` (Workspace), `daysActive`, `dayReadOnly`,
    // `dayAllowNewBlockIds`, `dayPalette` (BlockEditor.rendererPalette), `dayLocale`, `dayNow`:
    // ContentJournalDay's — and a PageView of the looks above; the stream's `streamSelection`
    // (StreamSelection) and `streamFind` (StreamFind) have the days made attached. A day without a
    // note starts from `journalTemplate`; a document day's text starts `documentGutter` into the
    // column. The headings' look and words: `journalLook` (JournalView::Look's keys: text, muted,
    // faint, accentText, accent, onAccent, neutralBadge, separator, hover, pressed, background
    // (colours), dateFamily, uiFamily, dateSize, todaySize, compactDateSize, captionSize,
    // buttonSize, badgeSize, lineHeightTight, gap, space2, space3, touchTarget, radius, iconSize,
    // compact, touch, readOnly, today, yesterday, openAsPage, promptToday, promptDay, readOnlyToday,
    // readOnlyDay). `streamHeader` / `streamFooter`: the QML items above and below the days. The
    // room the docked touch toolbar covers: `bottomInset`.
    // The page API above (editRow, editText, revealRow, rowTop, linkPoint, selectInEdited…) and the
    // page's signals are the active day's: a day's signal makes it the active one first
    // (`activeController` changes before the signal is emitted).
    Q_PROPERTY(QObject *daysModel READ daysModel WRITE setDaysModel NOTIFY journalChanged FINAL)
    Q_PROPERTY(QObject *editorHost MEMBER m_editorHost NOTIFY journalChanged FINAL)
    Q_PROPERTY(bool daysActive MEMBER m_daysActive NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(bool dayReadOnly MEMBER m_dayReadOnly NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(bool dayAllowNewBlockIds MEMBER m_dayAllowNewBlockIds NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(QVariantMap dayPalette MEMBER m_dayPalette NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(QString dayLocale MEMBER m_dayLocale NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(QString dayNow MEMBER m_dayNow NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(QString journalTemplate MEMBER m_journalTemplate NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(qreal documentGutter MEMBER m_documentGutter NOTIFY styleChanged FINAL)
    Q_PROPERTY(qreal bottomInset MEMBER m_bottomInset NOTIFY dayOptionsChanged FINAL)
    Q_PROPERTY(QObject *streamSelection MEMBER m_streamSelection NOTIFY journalChanged FINAL)
    Q_PROPERTY(QObject *streamFind MEMBER m_streamFind NOTIFY journalChanged FINAL)
    Q_PROPERTY(QVariantMap journalLook MEMBER m_journalLook NOTIFY journalLookChanged FINAL)
    Q_PROPERTY(QQuickItem *streamHeader READ streamHeader WRITE setStreamHeader NOTIFY journalChanged FINAL)
    Q_PROPERTY(QQuickItem *streamFooter READ streamFooter WRITE setStreamFooter NOTIFY journalChanged FINAL)
    Q_PROPERTY(QObject *activeController READ activeController NOTIFY activeControllerChanged FINAL)
    Q_PROPERTY(int activeDay READ activeDay NOTIFY activeControllerChanged FINAL)

public:
    explicit AtramentView(QQuickItem *parent = nullptr);
    ~AtramentView() override;

    QString view() const { return m_view; }
    void setView(const QString &view);
    QObject *pageModel() const { return m_pageModel.data(); }
    void setPageModel(QObject *model);
    QObject *viewModel() const { return m_viewModel.data(); }
    void setViewModel(QObject *model);
    QVariantMap viewLook() const { return m_viewLook; }
    void setViewLook(const QVariantMap &look);
    // Where row `row`'s part ("row", "box", "text") of a view other than the editor's was drawn, in
    // this item (empty: not drawn) — self-tests click there; the row its keys made current (−1:
    // none); the next frame showing a row (a search result: the current one); the next frame
    // showing a row at the view's top (the agenda's selected day; a view without: as viewRevealRow).
    Q_INVOKABLE QRectF viewPartRect(int row, const QString &part) const;
    Q_INVOKABLE int viewCurrentRow() const;
    Q_INVOKABLE void viewRevealRow(int row);
    Q_INVOKABLE void viewShowRow(int row);
    QObject *pageController() const { return m_pageController.data(); }
    void setPageController(QObject *controller);
    qreal contentHeight() const { return m_contentHeight; }
    int editRow() const;
    QString editText() const;
    int editCursor() const;
    bool editProse() const;
    QRectF editCaretRect() const;
    // Ends the input method's composition in the edited block (the word becomes text).
    Q_INVOKABLE void commitComposition();
    // Selects [from, to) of the edited block's text when `row` is edited here (the find bar's match).
    Q_INVOKABLE bool selectInEdited(int row, int from, int to);
    // Types `text` at the edited block's caret (in place of its selection), a space before it when
    // `separate` and the character before the caret is no space (the touch toolbar's "#", "/").
    Q_INVOKABLE bool insertAtCaret(const QString &text, bool separate);
    // Scrolls the page the least that shows [x, y, w, h] of this item with `margin` around it (a
    // hosted delegate's caret: BlockEditor.ensureVisible).
    Q_INVOKABLE bool revealArea(qreal x, qreal y, qreal w, qreal h, qreal margin);
    // Scrolls the page the least that shows model row `row` whole (BlockEditor.ensureRowVisible).
    Q_INVOKABLE bool revealRow(int row);
    // Where model row `row` is in this item by the last frame (its estimated height until it is
    // measured), −1 for a row not laid out (hidden, or before the first frame): the self-tests'
    // "the row is in view" (qml/dev/AtramentSelfTest.qml).
    Q_INVOKABLE qreal rowTop(int row) const;
    Q_INVOKABLE qreal rowHeight(int row) const;
    // Where drawn row `row`'s first link whose href contains `hrefPart` is ("page:", "tag:",
    // "date:"…) and its task's box (the middle, in this item), (−1, −1) when not drawn there: what
    // the self-tests click.
    Q_INVOKABLE QPointF linkPoint(int row, const QString &hrefPart) const;
    Q_INVOKABLE QPointF taskBoxPoint(int row) const;
    // Where the caret before `offset` of the edited field is ((−1, −1): none), and the misspelled
    // word at a point of this item in the edited text or a drawn row ("" when none): the self-tests'
    // clicks on a word, their look at the spell marks.
    Q_INVOKABLE QPointF editPoint(int offset) const;
    Q_INVOKABLE QString misspelledWordAt(qreal x, qreal y) const;

    // The process's Atrament App, made on first use.
    static brulion::atrament::App &atramentApp();

    QQmlComponent *delegate() const { return m_delegate.data(); }
    void setDelegate(QQmlComponent *delegate);
    QQuickItem *header() const { return m_header.data(); }
    void setHeader(QQuickItem *item);
    QQuickItem *footer() const { return m_footer.data(); }
    void setFooter(QQuickItem *item);
    QQmlComponent *dayHeader() const { return m_dayHeader.data(); }
    void setDayHeader(QQmlComponent *component);
    // "page": the delegate over model row `row` (null: the row is Atrament's, or not near the view).
    Q_INVOKABLE QQuickItem *rowItem(int row) const;
    // "page": edits cell (`tableRow`, `column`) (−1: the header) of the table in `row` with the caret
    // at `cursor` of its text (−1: its end), when Atrament draws that table.
    Q_INVOKABLE bool editTableCell(int row, int tableRow, int column, int cursor);
    bool cellsSelected() const { return m_cellsSelected; }
    int editTableRow() const { return m_editedTableRow; }
    // "page": Enter on a selected table: its first cell edited (false: Atrament does not draw it).
    Q_INVOKABLE bool editFirstTableCell(int row);
    // "page": the table Atrament draws in `row` as BlockEditor reaches a TableBlockView (null:
    // none there), and the one whose cell is edited (null: none).
    Q_INVOKABLE QQuickItem *tableAt(int row);
    Q_INVOKABLE QQuickItem *editedTable();
    // (For AtramentTableItem: the model row of a block.)
    int rowOfUid(quint64 uid) const;
    // "page": what a query block's results answer for a link of item `item` ("open:": the item)
    // — QueryResultModel::activateLink / open: { action: "scrollTo", row } …
    Q_INVOKABLE QVariantMap activateQueryLink(int row, int item, const QString &link, bool newPane = false);

    QObject *spellCheck() const { return m_spellCheck.data(); }
    void setSpellCheck(QObject *spell);
    QObject *vimSession() const { return m_vimSession.data(); }
    void setVimSession(QObject *vim);
    int editSelectionStart() const;
    int editSelectionEnd() const;
    // In a selectionCommand handler: the editor took the command (PageView's own default is skipped).
    Q_INVOKABLE void acceptSelectionCommand() { m_commandTaken = true; }
    // "page": the edited text's menu commands, as its keys do them (PageView::textCommand): "cut",
    // "copy", "paste", "selectAll" — { done } and, for a paste, the controller's answer (its row,
    // pictures from the internet it brought: BlockEditor.offerPastedPictures).
    Q_INVOKABLE QVariantMap textCommand(const QString &command);
    QRectF selectionRect() const { return m_selectionRect; }

    // "journal": the stream (see above).
    QObject *daysModel() const { return m_daysModel.data(); }
    void setDaysModel(QObject *model);
    QQuickItem *streamHeader() const { return m_streamHeader.data(); }
    void setStreamHeader(QQuickItem *item);
    QQuickItem *streamFooter() const { return m_streamFooter.data(); }
    void setStreamFooter(QQuickItem *item);
    QObject *activeController() const;
    int activeDay() const;
    // The days listed, the days made (near the view), day `row`'s controller (null: not made).
    Q_INVOKABLE int dayCount() const;
    Q_INVOKABLE int madeDayCount() const;
    Q_INVOKABLE QObject *dayController(int row) const;
    Q_INVOKABLE int dayOfController(QObject *controller) const;
    // Scrolls to day `row` (its top at the view's top; `contain`: the least that shows it), to the
    // day of `date` or the nearest earlier one (false: none), by `dy` px.
    Q_INVOKABLE void scrollToDay(int row, bool contain);
    Q_INVOKABLE bool scrollToDate(const QString &date);
    Q_INVOKABLE void scrollBy(qreal dy);
    // Day `row` made, shown and the one written in (the stream's find goes on there); the day of
    // `controller` the one written in.
    Q_INVOKABLE void activateDay(int row);
    Q_INVOKABLE void activateController(QObject *controller);
    // Starts writing in day `row` (its last block, or the journal template's first empty block).
    Q_INVOKABLE bool beginDay(int row);
    // { date, offset }: the day at the view's top and how far into it (px); {} at the very top.
    Q_INVOKABLE QVariantMap viewState() const;
    Q_INVOKABLE void restoreViewState(const QVariantMap &state);
    // Where day `row`'s section, its block `block` and its parts ("date", "open", "prompt") are in
    // this item by the last frame (−1 / (−1, −1): not drawn).
    Q_INVOKABLE qreal dayTop(int row) const;
    Q_INVOKABLE qreal dayHeight(int row) const;
    Q_INVOKABLE qreal dayRowTop(int row, int block) const;
    Q_INVOKABLE qreal dayRowHeight(int row, int block) const;
    Q_INVOKABLE QPointF dayPoint(int row, const QString &part) const;
    // A file renamed (the days follow it), the notebook's journal mode changed (each day's mode).
    Q_INVOKABLE void followRename(const QString &oldPath, const QString &newPath);
    Q_INVOKABLE void refreshModes();
    // The day's folds were restored from the notebook's (its changes are told from now on:
    // dayFoldStateChanged).
    Q_INVOKABLE void markFoldsRestored(QObject *controller);

Q_SIGNALS:
    void viewChanged();
    void pageModelChanged();
    void viewModelChanged();
    void viewLookChanged();
    // What a row of a view other than the editor's asks ("tasks": views/atrament/tasksview.h's
    // actions, "search": searchview.h's, "agenda": agendaview.h's; x, y in this item).
    void viewAction(const QString &action, const QVariantMap &args);
    // "page": a link of row `row` clicked or tapped (BlockEditor.activateLink), a task's box.
    void linkActivated(int row, const QString &link);
    // The same with the click's place in this item (a date chip's menu opens there).
    void linkClicked(int row, const QString &link, qreal x, qreal y);
    // A right-click or a finger held on a link of row `row` (a chip, a tag, a page): its menu at
    // (x, y) of this item (BlockEditor.openDateMenu / openBlockLinkMenu), not the block's.
    void linkMenuRequested(int row, const QString &link, qreal x, qreal y);
    // The mouse pointer came onto a link of row `row` at (x, y) of this item: its preview
    // (LinkPreviews.hover) — `link` previewed (a day chip's: its day's page) and the file it is
    // written in; "" for both: it left the link.
    void linkHovered(int row, const QString &link, const QString &fromPath, qreal x, qreal y);
    // The pointer rests on a table cell whose formula failed or whose text is cut short: `text`
    // (the error, else the whole text) as a tooltip over the cell's (x, y, width, height) of this
    // item; "": none (TableBlockView's BToolTip).
    void toolTipRequested(const QString &text, qreal x, qreal y, qreal width, qreal height);
    void taskToggled(int row);
    // A right-click or a finger held on a task's box at (x, y) of this item: its status menu
    // (BlockEditor.openTaskStatusMenu).
    void taskMenuRequested(int row, qreal x, qreal y);
    // Ctrl+Shift+Enter in the edited block: its status menu under the caret at (x, y) of this item,
    // when the block can have one (BlockEditor.canHaveTaskStatus).
    void taskMenuAtCaretRequested(int row, qreal x, qreal y);
    void styleChanged();
    void pageControllerChanged();
    // "page", editing: a click or a tap asks to edit `row` with the caret at `cursor` (−1: not
    // known) — the editor decides (BlockEditor.blockTappedWith for Shift / Ctrl, else
    // startEditing); `modifiers` are Qt::KeyboardModifiers.
    void editRequested(int row, int cursor, int modifiers);
    // Esc in the edited block (BlockEditor.stopEditing(true)); Ctrl+Z / Ctrl+Y (undoStep);
    // Alt+Enter on a link (followLinkAtCursor).
    void escapePressed();
    void undoRequested(bool redo);
    void followLinkRequested(int row, int cursor, bool newPane);
    void suggestionsOpenChanged();
    void findChanged();
    void contentHeightChanged();
    // A block Atrament edits started being edited (a click, Enter, the QML around): the item takes
    // the keyboard unless the editor keeps it elsewhere (the find bar).
    void editingStarted();
    void editStateChanged();
    // A key for the open suggestion list: "up", "down", "enter", "tab", "escape" (the editor's
    // suggestionKey()).
    void suggestionKeyPressed(const QString &key);
    // Shift+↑ / Shift+↓ past the edited block's text (BlockEditor.extendBlocksFrom), Ctrl+A on its
    // whole text (selectAllBlocks).
    void selectionExtendRequested(int row, int step);
    void selectAllBlocksRequested();
    // "page": the outline's bullet of `row` clicked (zoom into it); a right-click on `row` at a
    // point of the scene (the block menu).
    void bulletClicked(int row);
    void blockMenuRequested(int row, qreal sceneX, qreal sceneY);
    // A finger held on a block's text: the touch selection mode (BlockEditor.longPressText).
    void rowLongPressed(int row);
    // A mouse's press on one block dragged into another: select blocks `anchor` to `focus`.
    void dragSelectRequested(int anchor, int focus);
    void delegateChanged();
    void headerChanged();
    void footerChanged();
    void dayHeaderChanged();
    // "page", tables drawn by Atrament: a click or a tap on a cell asks to edit it at `cursor` (the
    // editor decides: BlockEditor.blockTappedWith, then editTableCell()); a link of a cell clicked
    // at a point of this item (TableBlockView's: a date chip's dialog, a link); a right-click on a
    // cell at a point of the scene (BlockEditor.openCellBlockMenuAt: the table's size, the
    // column's alignment).
    void tableCellTapped(int row, int tableRow, int column, int cursor, int modifiers);
    void tableLinkClicked(int row, int tableRow, int column, const QString &link, qreal x, qreal y);
    void tableCellMenuRequested(int row, int tableRow, int column, int rowCount, int columnCount,
                                const QString &align, qreal sceneX, qreal sceneY);
    // The cell selection was made, moved or ended; a toast its commands ask for (BlockEditor.notice:
    // "success", "info", "error").
    void cellSelectionChanged();
    void noticeRequested(const QString &text, const QString &level);
    // A cell of a table Atrament draws started or stopped being edited (BlockEditor.editingTable:
    // editedTable()); a right-click or a finger held on a cell's link or chip at (x, y) of this
    // item (TableBlockView.cellMenu: the token's menu, else the link's).
    void tableEditingChanged();
    void tableLinkMenuRequested(int row, int tableRow, int column, const QString &link, qreal x, qreal y);
    // "page", pictures and files (EmbedView's signals): an embed of `row` clicked — BlockListModel's
    // `embeds` entry: a picture (the viewer; one from the internet: WebPictureViewer), a PDF (the
    // PDF viewer), a file; a picture from the internet's menu asked for at a point of this item.
    void embedClicked(int row, const QVariantMap &embed);
    void embedMenuRequested(int row, const QVariantMap &embed, qreal x, qreal y);
    // A picture from the internet not shown yet (EmbedView's placeholder): "Show" / "Try again"
    // (`save` false: PageController.showWebPicture) or "Save to notebook" (`save`:
    // BlockEditor.downloadWebPictures) for its address `url`.
    void webPictureRequested(int row, const QString &url, bool save);
    // A sound's or a video's card drawn by Atrament: its "More" (MediaEmbedMenu at x, y of this
    // item), a video's "Full screen" (MediaFullscreen), "Open in another app" where nothing plays.
    void mediaMenuRequested(int row, const QVariantMap &embed, qreal x, qreal y);
    void mediaFullscreenRequested(int row, const QVariantMap &embed);
    void openElsewhereRequested(const QString &path);
    // A link of an embedded block's text (TransclusionView.activate: its file, line and Markdown).
    void embeddedLinkActivated(int row, const QString &link, const QString &path, int line, const QString &raw);
    // A link of a query result's text (QueryBlockView: a date chip's dialog, else
    // activateQueryLink()); the results' answer to a click on an item (BlockEditor.revealRow…).
    void queryLinkActivated(int row, int item, const QString &link, const QString &path, int line, const QString &raw);
    void queryAnswered(int row, const QVariantMap &answer);
    void spellCheckChanged();
    void vimSessionChanged();
    // A right-click on a misspelled word (a tap on one in the edited text on touch UIs) at (x, y)
    // of this item: `spelling` {from, to, word (as checked), text (as written)} — of the edited
    // text when `edited` (PageController.replaceRange), else of the block's body
    // (PageController.replaceInBody; BlockEditor.openRenderedSpellingMenu).
    void spellingMenuRequested(int row, bool edited, const QVariantMap &spelling, qreal x, qreal y);
    // A right-click in the edited block's text (BlockEditor.openAtramentTextMenu: Cut, Copy, Paste,
    // Highlight…, the AI, Select all) at (x, y) of this item, the caret moved there unless inside
    // the selection; `spelling` the misspelled word there ({from, to, word, text} of the editText;
    // {} for none): its corrections first.
    void textMenuRequested(int row, const QVariantMap &spelling, qreal x, qreal y);
    // A right-click or a finger held on a link of an embedded block's text (TransclusionView's) or of
    // a query result's (QueryBlockView's, item `item`): its menu at (x, y) of this item — the
    // token's in its own file (BlockEditor.openTokenMenuIn), else the link's.
    void embeddedLinkMenuRequested(int row, const QString &link, const QString &path, int line, const QString &raw,
                                   qreal x, qreal y);
    void queryLinkMenuRequested(int row, int item, const QString &link, const QString &path, int line,
                                const QString &raw, qreal x, qreal y);
    // A reminder chip of the edited text clicked (`menu` false: BlockEditor.editReminderInText) or
    // right-clicked / held (`menu` true: its menu) at (x, y): the token's editText range.
    void reminderChipActivated(int row, int from, int to, bool menu, qreal x, qreal y);
    // Vim took a key of the edited block (BlockEditor.afterVimKey).
    void vimKeyHandled();
    // "page": a key of the block selection asks the editor first (pageview.h SelectionCommand:
    // "copy", "cut", "delete", "paste", "escape", "back", "edit", "taskMenu", "sourceMode",
    // "copyReference"; "indent", "outdent", "moveUp", "moveDown", "cycleTasks", "extendUp",
    // "extendDown" only to refuse them or do them its way; "extendPastTop", "extendPastBottom" at
    // the page's edge; "extended" after the selection's end moved) — the handler calls
    // acceptSelectionCommand() when it took it. `row` is the selection's focus end.
    void selectionCommand(const QString &command, int row);
    void touchSelectionChanged();
    void selectionRectChanged();
    // "journal".
    void journalChanged();
    void dayOptionsChanged();
    void journalLookChanged();
    // The day written in changed (before the signal of the day that made it so).
    void activeControllerChanged();
    // A day's date or "Open as page" (`source`: Ctrl+E in it — its Markdown source).
    void openAsPageRequested(const QString &date, bool source);
    // The prompt of a day without a note: the editor decides (reading mode), then beginDay().
    void beginRequested(int row);
    // A day's controller made (near the view), loaded, about to go (far away); its folds changed
    // (after markFoldsRestored), a change from elsewhere merged into it.
    void dayMade(QObject *controller);
    void dayLoaded(QObject *controller);
    void dayReleased(QObject *controller);
    void dayFoldStateChanged(QObject *controller);
    void dayExternallyChanged(QObject *controller, bool merged);

protected:
    void componentComplete() override;
    void placeHostedChildren() override;
    // "journal": a press on a day's hosted row makes that day the active one first.
    bool childMouseEventFilter(QQuickItem *child, QEvent *event) override;
    // Vim's own Ctrl keys win over the window's shortcuts in its command modes (InlineEditor's
    // Keys.onShortcutOverride, BlockEditor.vimOwnsShortcut).
    bool event(QEvent *event) override;

private:
    void attachView();
    void applyStyle();

    QString m_view;
    bool m_attached = false;
    QPointer<QObject> m_pageModel;
public:
    // The views other than the editor's (atramentviews.cpp; shared: Views is defined there).
    struct Views;

private:
    std::shared_ptr<Views> m_views;
    QPointer<QObject> m_viewModel;
    QVariantMap m_viewLook;
    bool attachOtherView();
    bool setBeforeView(QPointer<QObject> &slot, QObject *model, const char *what);
    void applyViewLook();
    // The "page" view's entity (editor/atrament/pageview.h), released after the window closes.
    struct Page;
    std::unique_ptr<Page> m_page;
    qreal m_fontSize = 16;
    qreal m_lineHeight = 24;
    qreal m_indentStep = 26;
    qreal m_gutter = 16;
    qreal m_bulletColumn = 20;
    bool m_documentMode = false;
    qreal m_listIndent = 27;
    qreal m_contentX = 0;
    qreal m_contentWidth = 0;
    QColor m_textColor{0x1f, 0x23, 0x28};
    QColor m_mutedColor{0x6e, 0x77, 0x81};
    QColor m_linkColor{0x3b, 0x5b, 0xdb};
    QColor m_guideColor{0x8c, 0x95, 0x9f, 0x59};
    QColor m_ruleColor;
    QColor m_backgroundColor{Qt::white};
    // Editing.
    void connectController();
    QPointer<QObject> m_pageController;
    QColor m_caretColor{0x3b, 0x5b, 0xdb};
    QColor m_selectionColor{0x3b, 0x5b, 0xdb, 0x40};
    QString m_placeholderText;
    QString m_editLabel;
    QVariantMap m_highlighterPalette;
    bool m_drawsCode = false;
    QVariantMap m_codeLook;
    bool m_drawsCalc = false;
    QVariantMap m_calcLook;
    bool m_drawsDiagrams = true;
    bool m_drawsMath = true;
    QVariantMap m_diagramLook;
    bool m_drawsPictures = true;
    bool m_drawsFiles = true;
    bool m_drawsTransclusions = true;
    bool m_drawsQueries = true;
    bool m_drawsSounds = true;
    QVariantMap m_mediaLook;
    // The app's player as the pages' cards see it (atramentmedia.h), made with the first page.
    std::shared_ptr<brulion::editor::atr::MediaSource> m_mediaSource;
    std::shared_ptr<brulion::editor::atr::MediaSource> mediaSource();
    void applyFind();
    qreal m_contentHeight = 0;
    QString m_findQuery;
    bool m_findMatchCase = false;
    bool m_findMatchDiacritics = false;
    bool m_findWholeWord = false;
    QColor m_findColor{0x3b, 0x5b, 0xdb, 0x47};
    bool m_suggestionsOpen = false;
    QColor m_bandColor{0x3b, 0x5b, 0xdb, 0x22};
    QColor m_ringColor{0x3b, 0x5b, 0xdb};
    qreal m_bandRadius = 6;
    qreal m_arrowColumn = 16;
    bool m_touchUi = false;
    QColor m_calloutInfo{0x3b, 0x82, 0xf6};
    QColor m_calloutSuccess{0x16, 0xa3, 0x4a};
    QColor m_calloutWarning{0xd9, 0x77, 0x06};
    QColor m_calloutDanger{0xe2, 0x57, 0x4c};
    QColor m_calloutQuote{0x8c, 0x95, 0x9f};
    qreal m_calloutFill = 0.10;
    qreal m_calloutRadius = 10;
    QPointer<QQmlComponent> m_delegate;
    QPointer<QQuickItem> m_header;
    QPointer<QQuickItem> m_footer;
    QPointer<QQmlComponent> m_dayHeader;
    // Tables drawn by Atrament.
    void applyTableStyle();
    void connectTables();
    bool m_drawsTables = true;
    QVariantMap m_tableLook;
    bool m_cellsSelected = false;
    int m_editedTableRow = -1;
    std::unordered_map<quint64, QPointer<AtramentTableItem>> m_tableItems;
    friend class AtramentTableItem;
    // The edited text's extras.
    void applyExtras();
    QPointer<QObject> m_spellCheck;
    QColor m_spellColor{0xc0, 0x39, 0x2b};
    QPointer<QObject> m_vimSession;
    // The block selection.
    void applyTouchSelection();
    bool m_selectionGrips = false;
    bool m_selectionPicking = false;
    qreal m_dragBottomInset = 0;
    bool m_commandTaken = false;
    QRectF m_selectionRect;

    // A page drawn here: the "page" view's, or in "journal" the active day's (none before one is).
    bool hasPage() const;
    // The pages the looks and the callbacks go to: the page, or every day made.
    template<class F>
    void forEachPage(F &&f);
    // The page's (or a day's) part of the setup: its signals, its editing callbacks, its tables',
    // its looks (styleOne: without the geometry in "journal", which is the stream's per day).
    struct PageHandle;
    void connectPageSignals(const PageHandle &page);
    void connectHoverSignals(const PageHandle &page);
    void connectEditing(const PageHandle &page);
    void connectTablesOf(const PageHandle &page);
    void styleOne(const PageHandle &page);
    void styleTablesOf(const PageHandle &page);
    void extrasOf(const PageHandle &page);
    void touchSelectionOf(const PageHandle &page);
    // "journal".
    struct Journal;
    std::unique_ptr<Journal> m_journal;
    void attachJournal();
    // The stream's parts that are this view's: the days' controllers and pages, the dates, its signals.
    void hookJournal(brulion::editor::atr::JournalView &journal);
    // A day's controller as ContentJournalDay makes it; its page set up as a page is (its signals
    // through the day written in, looks, hosted rows, the zoom's breadcrumb).
    std::unique_ptr<brulion::editor::PageController> makeDayController(const QString &path);
    bool dayFoldsRestored(QObject *controller) const;
    void releaseDayItems(QObject *controller);
    QString dayLongDate(const QString &date) const;
    void setUpDay(const brulion::atrament::Entity<brulion::editor::atr::PageView> &page,
                  brulion::editor::PageController *controller);
    void applyJournal();
    void applyJournalLook();
    void applyDayOptions();
    void activatePage(std::uint64_t pageId);
    void journalActiveChanged(int day);
    // A day zoomed into a block gets its dayHeader item; zoomed out, it goes.
    void updateDayHeader(QObject *controller);
    QQuickItem *makeDayHeader(QObject *controller);
    void placeStreamEdges();
    void tellStreamEdges();
    QPointer<QObject> m_daysModel;
    QPointer<QObject> m_editorHost;
    bool m_daysActive = true;
    bool m_dayReadOnly = false;
    bool m_dayAllowNewBlockIds = true;
    QVariantMap m_dayPalette;
    QString m_dayLocale;
    QString m_dayNow;
    QString m_journalTemplate;
    qreal m_documentGutter = 36;
    qreal m_bottomInset = 0;
    QPointer<QObject> m_streamSelection;
    QPointer<QObject> m_streamFind;
    QVariantMap m_journalLook;
    QPointer<QQuickItem> m_streamHeader;
    QPointer<QQuickItem> m_streamFooter;
};
