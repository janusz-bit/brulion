#include "vimsourcebuffer.h"

#include <QAbstractUndoItem>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using vim::Position;

namespace {

/// A step of the document's history that changes nothing: a change of Vim starts with one, so the
/// typing of an insert never merges into the step of the one before it (QTextDocument joins
/// adjacent insertions).
class Barrier : public QAbstractUndoItem {
public:
    void undo() override { }
    void redo() override { }
};

} // namespace

void VimSourceBuffer::setEdit(QQuickItem *edit)
{
    m_edit = edit;
    m_depth = 0;
    m_done.clear();
    m_undone.clear();
    m_selectionShown = false;
}

QTextDocument *VimSourceBuffer::document() const
{
    if (!m_edit)
        return nullptr;
    auto *doc = m_edit->property("textDocument").value<QQuickTextDocument *>();
    return doc ? doc->textDocument() : nullptr;
}

bool VimSourceBuffer::editable() const
{
    return document() != nullptr;
}

int VimSourceBuffer::lineCount() const
{
    const QTextDocument *doc = document();
    return doc ? std::max(1, doc->blockCount()) : 1;
}

QString VimSourceBuffer::line(int i) const
{
    const QTextDocument *doc = document();
    if (!doc)
        return {};
    const QTextBlock b = doc->findBlockByNumber(i);
    return b.isValid() ? b.text() : QString();
}

int VimSourceBuffer::positionOf(Position p) const
{
    const QTextDocument *doc = document();
    if (!doc)
        return 0;
    QTextBlock b = doc->findBlockByNumber(std::clamp(p.line, 0, std::max(0, doc->blockCount() - 1)));
    if (!b.isValid())
        b = doc->lastBlock();
    return b.position() + std::clamp(p.column, 0, std::max(0, b.length() - 1));
}

Position VimSourceBuffer::positionAt(int pos) const
{
    const QTextDocument *doc = document();
    if (!doc)
        return {};
    pos = std::clamp(pos, 0, std::max(0, doc->characterCount() - 1));
    const QTextBlock b = doc->findBlock(pos);
    if (!b.isValid())
        return {};
    return Position{b.blockNumber(), pos - b.position()};
}

int VimSourceBuffer::caret() const
{
    return m_edit ? m_edit->property("cursorPosition").toInt() : 0;
}

void VimSourceBuffer::setCaret(int pos)
{
    if (!m_edit)
        return;
    QMetaObject::invokeMethod(m_edit, "deselect");
    m_edit->setProperty("cursorPosition", pos);
}

Position VimSourceBuffer::cursor() const
{
    if (m_selectionShown)
        return m_cursor;
    return positionAt(caret());
}

void VimSourceBuffer::setCursor(Position p)
{
    m_cursor = p;
    m_selectionShown = false;
    setCaret(positionOf(p));
}

void VimSourceBuffer::showSelection(Position anchor, Position cursor, vim::SelectionShape shape)
{
    if (shape == vim::SelectionShape::None || !m_edit) {
        setCursor(cursor);
        return;
    }
    m_cursor = cursor;
    m_selectionShown = true;
    int start = 0, end = 0;
    if (shape == vim::SelectionShape::Lines) {
        const int first = std::min(anchor.line, cursor.line);
        const int last = std::max(anchor.line, cursor.line);
        const int from = positionOf(Position{first, 0});
        const int to = positionOf(Position{last, int(line(last).size())});
        start = cursor.line >= anchor.line ? from : to;
        end = cursor.line >= anchor.line ? to : from;
    } else {
        const int a = positionOf(anchor);
        const int c = positionOf(cursor);
        // Both ends included: the later one's character too.
        const auto after = [&](Position p, int pos) {
            const QString text = line(p.line);
            return p.column >= text.size() ? pos + 1 : pos + (vim::nextBoundary(text, p.column) - p.column);
        };
        if (c >= a) {
            start = a;
            end = std::min(after(cursor, c), document() ? document()->characterCount() - 1 : c);
        } else {
            start = std::min(after(anchor, a), document() ? document()->characterCount() - 1 : a);
            end = c;
        }
    }
    QMetaObject::invokeMethod(m_edit, "deselect");
    m_edit->setProperty("cursorPosition", start);
    QMetaObject::invokeMethod(m_edit, "moveCursorSelection", Q_ARG(int, end));
}

bool VimSourceBuffer::replace(Position from, Position to, const QString &text)
{
    QTextDocument *doc = document();
    if (!doc)
        return false;
    QTextCursor c(doc);
    c.beginEditBlock();
    c.setPosition(positionOf(from));
    c.setPosition(positionOf(to), QTextCursor::KeepAnchor);
    c.insertText(text);
    c.endEditBlock();
    return true;
}

bool VimSourceBuffer::removeLines(int first, int last)
{
    QTextDocument *doc = document();
    if (!doc)
        return false;
    const QTextBlock a = doc->findBlockByNumber(first);
    const QTextBlock b = doc->findBlockByNumber(last);
    if (!a.isValid() || !b.isValid())
        return false;
    int start = a.position();
    int end = b.position() + b.length() - 1;
    if (b.next().isValid())
        end += 1; // the line break after the last line
    else if (a.previous().isValid())
        start -= 1; // the last lines: the line break before them
    QTextCursor c(doc);
    c.beginEditBlock();
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.endEditBlock();
    return true;
}

int VimSourceBuffer::putLines(int lineIndex, bool below, const vim::Register &reg)
{
    QTextDocument *doc = document();
    if (!doc)
        return -1;
    const QTextBlock b = doc->findBlockByNumber(lineIndex);
    if (!b.isValid())
        return -1;
    QTextCursor c(doc);
    c.beginEditBlock();
    if (below) {
        c.setPosition(b.position() + b.length() - 1);
        c.insertText(u'\n' + reg.text);
    } else {
        c.setPosition(b.position());
        c.insertText(reg.text + u'\n');
    }
    c.endEditBlock();
    return below ? lineIndex + 1 : lineIndex;
}

Position VimSourceBuffer::openLine(int lineIndex, bool below)
{
    QTextDocument *doc = document();
    if (!doc)
        return {-1, 0};
    const QTextBlock b = doc->findBlockByNumber(lineIndex);
    if (!b.isValid())
        return {-1, 0};
    const QString indent = vim::indentationOf(b.text());
    QTextCursor c(doc);
    c.beginEditBlock();
    if (below) {
        c.setPosition(b.position() + b.length() - 1);
        c.insertText(u'\n' + indent);
    } else {
        c.setPosition(b.position());
        c.insertText(indent + u'\n');
    }
    c.endEditBlock();
    return Position{below ? lineIndex + 1 : lineIndex, int(indent.size())};
}

QString VimSourceBuffer::indentUnit() const
{
    const QTextDocument *doc = document();
    if (!doc)
        return u"\t"_s;
    // The text's own: a tab, or the fewest spaces a list item is indented with.
    int spaces = 0;
    int n = 0;
    for (QTextBlock b = doc->begin(); b.isValid() && n < 2000; b = b.next(), ++n) {
        const QString t = b.text();
        if (t.startsWith(u'\t'))
            return u"\t"_s;
        int k = 0;
        while (k < t.size() && t[k] == u' ')
            ++k;
        if (k >= 2 && k < t.size()) {
            const QString rest = t.mid(k);
            const bool marker = rest.startsWith(u"- "_s) || rest.startsWith(u"* "_s) || rest.startsWith(u"+ "_s)
                                || (rest.size() > 1 && rest[0].isDigit());
            if (marker && (spaces == 0 || k < spaces))
                spaces = k;
        }
    }
    return spaces >= 2 && spaces <= 8 ? QString(spaces, u' ') : u"\t"_s;
}

bool VimSourceBuffer::shiftLines(int first, int last, int levels)
{
    QTextDocument *doc = document();
    if (!doc || levels == 0)
        return false;
    const QString unit = indentUnit();
    const int width = unit == u"\t" ? 4 : int(unit.size());
    QTextCursor c(doc);
    c.beginEditBlock();
    for (int i = last; i >= first; --i) {
        const QTextBlock b = doc->findBlockByNumber(i);
        if (!b.isValid())
            continue;
        const QString t = b.text();
        if (t.isEmpty())
            continue;
        QString lead = vim::indentationOf(t);
        QString out = lead;
        if (levels > 0) {
            out = unit.repeated(levels) + lead;
        } else {
            for (int k = 0; k < -levels; ++k) {
                if (out.startsWith(u'\t')) {
                    out.remove(0, 1);
                    continue;
                }
                int s = 0;
                while (s < width && s < out.size() && out[s] == u' ')
                    ++s;
                out.remove(0, s);
            }
        }
        if (out == lead)
            continue;
        c.setPosition(b.position());
        c.setPosition(b.position() + int(lead.size()), QTextCursor::KeepAnchor);
        c.insertText(out);
    }
    c.endEditBlock();
    return true;
}

void VimSourceBuffer::beginChange()
{
    if (m_depth++ > 0)
        return;
    QTextDocument *doc = document();
    if (!doc)
        return;
    m_stepsBefore = doc->availableUndoSteps();
    doc->appendUndoItem(new Barrier);
}

void VimSourceBuffer::endChange()
{
    if (m_depth == 0 || --m_depth > 0)
        return;
    QTextDocument *doc = document();
    if (!doc)
        return;
    const int after = doc->availableUndoSteps();
    if (after > m_stepsBefore) {
        m_done.append(Group{m_stepsBefore, after});
        m_undone.clear();
    }
    while (m_done.size() > 500)
        m_done.removeFirst();
}

bool VimSourceBuffer::undo()
{
    QTextDocument *doc = document();
    if (!doc || !doc->isUndoAvailable())
        return false;
    // (The document counts the items of its steps, an undo takes a whole step: undone until the
    // count is the one before the change.)
    int target = -1;
    const int now = doc->availableUndoSteps();
    if (!m_done.isEmpty() && m_done.last().after == now) {
        target = m_done.last().before;
        m_undone.append(m_done.takeLast());
    } else {
        // The history went its own way (the text's undo, a reload): one step, no groups.
        m_done.clear();
        m_undone.clear();
    }
    QTextCursor c(doc);
    c.setPosition(std::min(caret(), doc->characterCount() - 1));
    int pos = -1;
    do {
        const int before = c.position();
        doc->undo(&c);
        if (c.position() != before)
            pos = c.position();
    } while (target >= 0 && doc->isUndoAvailable() && doc->availableUndoSteps() > target);
    m_selectionShown = false;
    setCaret(pos >= 0 ? pos : c.position());
    return true;
}

bool VimSourceBuffer::redo()
{
    QTextDocument *doc = document();
    if (!doc || !doc->isRedoAvailable())
        return false;
    int target = -1;
    const int now = doc->availableUndoSteps();
    if (!m_undone.isEmpty() && m_undone.last().before == now) {
        target = m_undone.last().after;
        m_done.append(m_undone.takeLast());
    } else {
        m_done.clear();
        m_undone.clear();
    }
    QTextCursor c(doc);
    c.setPosition(std::min(caret(), doc->characterCount() - 1));
    do {
        doc->redo(&c);
    } while (target >= 0 && doc->isRedoAvailable() && doc->availableUndoSteps() < target);
    m_selectionShown = false;
    setCaret(c.position());
    return true;
}

void VimSourceBuffer::modeChanged(vim::Mode mode)
{
    if (mode != vim::Mode::Visual && mode != vim::Mode::VisualLine && m_selectionShown) {
        m_selectionShown = false;
        if (m_edit)
            QMetaObject::invokeMethod(m_edit, "deselect");
    }
    if (onMode)
        onMode(mode);
}

bool VimSourceBuffer::save()
{
    return onSave && onSave();
}

void VimSourceBuffer::quit(bool force)
{
    if (onSave)
        onSave();
    if (onQuit)
        onQuit(force);
}

void VimSourceBuffer::scroll(int lineIndex, vim::ScrollTo where)
{
    if (onScroll)
        onScroll(positionOf(Position{lineIndex, 0}), where);
}

QVariant VimSourceBuffer::anchor(Position p) const
{
    return positionOf(p);
}

Position VimSourceBuffer::anchorPosition(const QVariant &anchor) const
{
    bool ok = false;
    const int pos = anchor.toInt(&ok);
    if (!ok)
        return {-1, 0};
    return positionAt(pos);
}

} // namespace brulion::editor
