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
#include <QVariantMap>

#include <memory>

namespace brulion::atrament {
class App;
}

class AtramentView : public brulion::atrament::AtramentItem {
    Q_OBJECT
    QML_ELEMENT
    // The view to show: "gallery" (the framework's gallery) or "page" (the blocks of `pageModel`
    // drawn by Atrament: editor/atrament/pageview.h, AppSettings.editorAtrament). Set once.
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged FINAL)
    // "page": the editor's BlockListModel (a PageController's `blocks`), set before the view.
    Q_PROPERTY(QObject *pageModel READ pageModel WRITE setPageModel NOTIFY pageModelChanged FINAL)
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
    Q_PROPERTY(QRectF editCaretRect READ editCaretRect NOTIFY editStateChanged FINAL)

    // "page": the rows Atrament does not draw yet (foreign rows: code, tables, math, pictures,
    // queries, the properties…) are `delegate` (the editor's BlockDelegate) placed over them, and
    // `header` / `footer` are the items above the blocks and below them (the title, the
    // properties, the zoom's breadcrumb; the room to tap under the last block) — the list's rows
    // too, scrolling with it (editor/atrament/foreignhost.h; qthost.h "Hosted children").
    Q_PROPERTY(QQmlComponent *delegate READ delegate WRITE setDelegate NOTIFY delegateChanged FINAL)
    Q_PROPERTY(QQuickItem *header READ header WRITE setHeader NOTIFY headerChanged FINAL)
    Q_PROPERTY(QQuickItem *footer READ footer WRITE setFooter NOTIFY footerChanged FINAL)

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

public:
    explicit AtramentView(QQuickItem *parent = nullptr);
    ~AtramentView() override;

    QString view() const { return m_view; }
    void setView(const QString &view);
    QObject *pageModel() const { return m_pageModel.data(); }
    void setPageModel(QObject *model);
    QObject *pageController() const { return m_pageController.data(); }
    void setPageController(QObject *controller);
    qreal contentHeight() const { return m_contentHeight; }
    int editRow() const;
    QString editText() const;
    int editCursor() const;
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

    // The process's Atrament App, made on first use.
    static brulion::atrament::App &atramentApp();

    QQmlComponent *delegate() const { return m_delegate.data(); }
    void setDelegate(QQmlComponent *delegate);
    QQuickItem *header() const { return m_header.data(); }
    void setHeader(QQuickItem *item);
    QQuickItem *footer() const { return m_footer.data(); }
    void setFooter(QQuickItem *item);
    // "page": the delegate over model row `row` (null: the row is Atrament's, or not near the view).
    Q_INVOKABLE QQuickItem *rowItem(int row) const;

    QObject *spellCheck() const { return m_spellCheck.data(); }
    void setSpellCheck(QObject *spell);
    QObject *vimSession() const { return m_vimSession.data(); }
    void setVimSession(QObject *vim);
    int editSelectionStart() const;
    int editSelectionEnd() const;

Q_SIGNALS:
    void viewChanged();
    void pageModelChanged();
    // "page": a link of row `row` clicked or tapped (BlockEditor.activateLink), a task's box.
    void linkActivated(int row, const QString &link);
    // The same with the click's place in this item (a date chip's menu opens there).
    void linkClicked(int row, const QString &link, qreal x, qreal y);
    void taskToggled(int row);
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
    void spellCheckChanged();
    void vimSessionChanged();
    // A right-click on a misspelled word (a tap on one in the edited text on touch UIs) at (x, y)
    // of this item: `spelling` {from, to, word (as checked), text (as written)} — of the edited
    // text when `edited` (PageController.replaceRange), else of the block's body
    // (PageController.replaceInBody; BlockEditor.openRenderedSpellingMenu).
    void spellingMenuRequested(int row, bool edited, const QVariantMap &spelling, qreal x, qreal y);
    // A reminder chip of the edited text clicked (`menu` false: BlockEditor.editReminderInText) or
    // right-clicked / held (`menu` true: its menu) at (x, y): the token's editText range.
    void reminderChipActivated(int row, int from, int to, bool menu, qreal x, qreal y);
    // Vim took a key of the edited block (BlockEditor.afterVimKey).
    void vimKeyHandled();

protected:
    void componentComplete() override;
    void placeHostedChildren() override;
    // Vim's own Ctrl keys win over the window's shortcuts in its command modes (InlineEditor's
    // Keys.onShortcutOverride, BlockEditor.vimOwnsShortcut).
    bool event(QEvent *event) override;

private:
    void attachView();
    void applyStyle();

    QString m_view;
    bool m_attached = false;
    QPointer<QObject> m_pageModel;
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
    // The edited text's extras.
    void applyExtras();
    QPointer<QObject> m_spellCheck;
    QColor m_spellColor{0xc0, 0x39, 0x2b};
    QPointer<QObject> m_vimSession;
};
