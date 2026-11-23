// TextBuffer — a vim::Buffer over lines of text in memory, with its own undo history (one step per
// change of Vim). The engine's tests run on it; it is also the reference for what the editors'
// buffers do with plain text (vimsourcebuffer.h does the same over a QTextDocument).
//
//   vim::TextBuffer b(u"jeden\ndwa"_s);
//   b.setCursor({1, 0});
//   b.text();           // "jeden\ndwa"
#pragma once

#include "vim.h"

namespace brulion::vim {

class TextBuffer : public Buffer {
public:
    explicit TextBuffer(const QString &text = {});

    /// The lines joined by "\n".
    QString text() const;
    void setText(const QString &text);
    /// What > and < add or take away per level ("\t" by default).
    void setIndentUnit(const QString &unit) { m_indentUnit = unit; }
    void setPageLines(int lines) { m_pageLines = lines; }
    /// The first line the view shows (H M L): it shows pageLines() lines from it.
    void setViewTop(int line) { m_viewTop = line; }

    int lineCount() const override { return int(m_lines.size()); }
    QString line(int i) const override { return i >= 0 && i < m_lines.size() ? m_lines[i] : QString(); }
    Position cursor() const override { return m_cursor; }
    void setCursor(Position p) override;
    void showSelection(Position anchor, Position cursor, SelectionShape shape) override;
    bool replace(Position from, Position to, const QString &text) override;
    bool removeLines(int first, int last) override;
    int putLines(int line, bool below, const Register &reg) override;
    Position openLine(int line, bool below) override;
    bool shiftLines(int first, int last, int levels) override;
    void beginChange() override;
    void endChange() override;
    bool undo() override;
    bool redo() override;
    void modeChanged(Mode mode) override { m_mode = mode; }
    bool save() override
    {
        ++saves;
        return true;
    }
    void quit(bool force) override
    {
        ++quits;
        forcedQuit = force;
    }
    int pageLines() const override { return m_pageLines; }
    bool viewLines(int &first, int &last) const override;
    /// Marks follow their lines (a line removed: gone), as the editors' buffers do.
    QVariant anchor(Position p) const override;
    Position anchorPosition(const QVariant &anchor) const override;

    // What the engine did (tests).
    Mode mode() const { return m_mode; }
    SelectionShape selectionShape() const { return m_shape; }
    Position selectionAnchor() const { return m_anchor; }
    int undoSteps() const { return int(m_undo.size()); }
    int saves = 0;
    int quits = 0;
    bool forcedQuit = false;
    bool readOnly = false;

private:
    struct Step {
        QStringList before;
        QStringList after;
        Position cursorBefore;
        QList<quint64> idsBefore;
        QList<quint64> idsAfter;
    };
    void record(const QStringList &before, const QList<quint64> &idsBefore, Position cursor);
    QStringList m_lines;
    QList<quint64> m_ids; ///< per line: its identity (anchors)
    quint64 m_nextId = 1;
    int m_viewTop = 0;
    Position m_cursor;
    Position m_anchor;
    SelectionShape m_shape = SelectionShape::None;
    Mode m_mode = Mode::Normal;
    QString m_indentUnit = QStringLiteral("\t");
    int m_pageLines = 20;
    int m_depth = 0;
    QStringList m_openBefore;
    QList<quint64> m_openIds;
    Position m_openCursor;
    bool m_openChanged = false;
    QList<Step> m_undo;
    QList<Step> m_redo;
};

} // namespace brulion::vim
