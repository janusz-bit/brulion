// VimSourceBuffer — source mode's TextEdit as a vim::Buffer (docs/UI.md "Vim mode"): its document's
// blocks are Vim's lines, its caret and selection the TextEdit's (`cursorPosition`, select()); edits
// go through a QTextCursor of the document, so SourceTextSync hands them to the controller like any
// typing. One change of Vim is one step of `u` / Ctrl+R: the steps of the document's own history a
// change made are undone and redone together (a step that cannot be one of them — the history
// changed under it — goes alone).
#pragma once

#include "vim/vim.h"

#include <QList>
#include <QPointer>

class QQuickItem;
class QTextDocument;

namespace brulion::editor {

class VimSourceBuffer : public vim::Buffer {
public:
    VimSourceBuffer() = default;

    /// The TextEdit of source mode (its `textDocument`, `cursorPosition`, select()).
    void setEdit(QQuickItem *edit);
    QQuickItem *edit() const { return m_edit; }
    QTextDocument *document() const;

    std::function<void(vim::Mode mode)> onMode;
    std::function<void(bool force)> onQuit;
    std::function<bool()> onSave;
    std::function<void(int position, vim::ScrollTo where)> onScroll;
    void setPageLines(int lines) { m_pageLines = std::max(1, lines); }
    /// What > and < put in or take out: a tab, or the spaces the text indents with.
    QString indentUnit() const;

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
    void beginChange() override;
    void endChange() override;
    bool undo() override;
    bool redo() override;
    void modeChanged(vim::Mode mode) override;
    bool save() override;
    void quit(bool force) override;
    int pageLines() const override { return m_pageLines; }
    void scroll(int line, vim::ScrollTo where) override;
    QVariant anchor(vim::Position p) const override;
    vim::Position anchorPosition(const QVariant &anchor) const override;

private:
    int positionOf(vim::Position p) const;
    vim::Position positionAt(int pos) const;
    int caret() const;
    void setCaret(int pos);
    bool editable() const;

    QPointer<QQuickItem> m_edit;
    vim::Position m_cursor;
    bool m_selectionShown = false;
    int m_depth = 0;
    int m_stepsBefore = 0;
    /// A change's steps of the document's history: its undo count before and after it.
    struct Group {
        int before = 0;
        int after = 0;
    };
    QList<Group> m_undone;
    QList<Group> m_done;
    int m_pageLines = 20;
};

} // namespace brulion::editor
