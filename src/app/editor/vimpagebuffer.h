// VimPageBuffer — the block editor's page as a vim::Buffer (docs/UI.md "Vim mode"): Vim's lines
// are the lines of the visible blocks' edit texts, in order (a block with a line break has two; a
// table is one line of no text; the frontmatter is not there); the caret is the edited block's
// (PageController::editingRow / cursorPosition). Edits map onto PageController's block operations:
//
//   * charwise text within a block is text; across blocks the blocks between go and the two are
//     joined (J joins blocks); a line break typed or put into a text block makes a new block,
//     into code, quotes, callouts a line;
//   * whole lines are whole blocks: dd removes the block (a folded one with its children, an open
//     one's children move up), yy keeps its Markdown, p puts blocks after the block and its
//     children (P before it), o / O open a new block (Enter at its end / start), > < indent and
//     outdent like Tab and Shift+Tab — a line of a code block, quote… is a line of its text;
//   * one change of Vim is one undo step (PageController::joinUndoSteps()).
//
// The visual selection shows in the edited block's text while it stays in one block; once it reaches
// others the caret's block stays edited and the selection's blocks are drawn as selected (a band;
// the editor's block selection — with its subtrees and tools — is not made).
#pragma once

#include "vim/vim.h"

#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <functional>

namespace brulion::editor {

class PageController;

class VimPageBuffer : public vim::Buffer {
public:
    explicit VimPageBuffer(PageController *controller = nullptr);
    ~VimPageBuffer() override;
    VimPageBuffer(const VimPageBuffer &) = delete;
    VimPageBuffer &operator=(const VimPageBuffer &) = delete;

    void setController(PageController *controller);
    PageController *controller() const { return m_controller; }

    /// Where line `line` is: its row, its line in the block's edit text, the offset of the line's
    /// start in that text (row -1: an empty page).
    struct Place {
        int row = -1;
        int lineInBlock = 0;
        int offset = 0;
        int length = 0;
        bool opaque = false;
    };
    Place place(int line) const;
    /// The line of `offset` in `row`'s edit text (-1: not a line of the buffer).
    int lineOf(int row, int offset = 0) const;

    // What the editor shows beyond the controller's state.
    /// The visual selection in the edited block's text: row, anchor and caret offsets (the end
    /// they include already counted; row -1: none).
    std::function<void(int row, int anchor, int cursor)> onTextSelection;
    std::function<void(vim::Mode mode)> onMode;
    std::function<void(int row, vim::ScrollTo where)> onScroll;
    /// The rows at the view's top and bottom (H M L); false when not known.
    std::function<bool(int &firstRow, int &lastRow)> onView;
    std::function<void(bool force)> onQuit;
    void setPageLines(int lines) { m_pageLines = std::max(1, lines); }
    /// The text selection shown (tests).
    int shownSelectionRow() const { return m_selectionRow; }
    int shownSelectionAnchor() const { return m_selectionAnchor; }
    int shownSelectionCursor() const { return m_selectionCursor; }
    bool showsBlocks() const { return m_showingBlocks; }
    /// The editor's block selection is the caret on a table (setCursor()), not one to take over.
    bool ownsBlockSelection() const;

    // vim::Buffer
    int lineCount() const override;
    QString line(int i) const override;
    vim::Position cursor() const override;
    void setCursor(vim::Position p) override;
    void showSelection(vim::Position anchor, vim::Position cursor, vim::SelectionShape shape) override;
    bool replace(vim::Position from, vim::Position to, const QString &text) override;
    bool removeLines(int first, int last) override;
    int putLines(int line, bool below, const vim::Register &reg) override;
    vim::Position openLine(int line, bool below) override;
    bool shiftLines(int first, int last, int levels) override;
    vim::Register linesRegister(int first, int last) const override;
    void beginChange() override;
    void endChange() override;
    bool undo() override;
    bool redo() override;
    void modeChanged(vim::Mode mode) override;
    bool save() override;
    void quit(bool force) override;
    int pageLines() const override { return m_pageLines; }
    bool fold(int line, vim::FoldAction action) override;
    void scroll(int line, vim::ScrollTo where) override;
    bool viewLines(int &first, int &last) const override;
    QVariant anchor(vim::Position p) const override;
    vim::Position anchorPosition(const QVariant &anchor) const override;

private:
    struct Line {
        int row = -1;
        int lineInBlock = 0;
        int offset = 0;
        int length = 0;
        bool opaque = false;
    };
    void rebuild() const;
    void invalidate() { m_dirty = true; }
    /// rebuild() for rows whose text changed and whose lines did not come or go; false when the
    /// lines must all be read again.
    bool refreshRows(int from, int to) const;
    void clearTextSelection();
    void clearBand();
    /// An empty page gets a block to edit (insert, put): true when there is one now.
    bool ensureBlock();
    vim::Position clamped(vim::Position p) const;

    QPointer<PageController> m_controller;
    QList<QMetaObject::Connection> m_connections;
    mutable bool m_dirty = true; ///< rows came or went: every line is read again
    mutable int m_textFrom = -1; ///< rows [m_textFrom, m_textTo] changed their data: their lines only
    mutable int m_textTo = -1;
    mutable QList<Line> m_lines;
    mutable QList<QString> m_texts;      ///< per line: its row's edit text (shared)
    mutable QHash<int, int> m_firstLine; ///< row → its first line
    vim::Position m_cursor;
    bool m_selectionShown = false; ///< a visual selection: the caret is m_cursor
    bool m_showingBlocks = false;  ///< … shown as a block selection
    int m_selectionRow = -1;
    int m_selectionAnchor = -1;
    int m_selectionCursor = -1;
    quint64 m_tableCaretUid = 0; ///< the table the caret is on (selected for it)
    int m_changeDepth = 0;
    QString m_changeToken;
    QSet<QString> m_freedIds; ///< block ids deleted by Vim on this page (a put keeps them)
    /// Blanks an edit left at the start of a list item's text, which its marker took (replace()).
    struct LostLead {
        quint64 uid = 0;
        QString lead;
        QString tail; ///< the text after them
    };
    LostLead m_lostLead;
    int m_pageLines = 20;
};

} // namespace brulion::editor
