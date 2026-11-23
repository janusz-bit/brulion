#include "textbuffer.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::vim {

TextBuffer::TextBuffer(const QString &text)
{
    setText(text);
}

QString TextBuffer::text() const
{
    return m_lines.join(u'\n');
}

void TextBuffer::setText(const QString &text)
{
    m_lines = text.split(u'\n');
    m_ids.clear();
    for (int i = 0; i < m_lines.size(); ++i)
        m_ids.append(m_nextId++);
    m_viewTop = 0;
    m_cursor = {};
    m_undo.clear();
    m_redo.clear();
}

void TextBuffer::setCursor(Position p)
{
    p.line = std::clamp(p.line, 0, lineCount() - 1);
    p.column = std::clamp(p.column, 0, int(m_lines[p.line].size()));
    m_cursor = p;
    m_shape = SelectionShape::None;
}

void TextBuffer::showSelection(Position anchor, Position cursor, SelectionShape shape)
{
    setCursor(cursor);
    m_anchor = anchor;
    m_shape = shape;
}

void TextBuffer::record(const QStringList &before, const QList<quint64> &idsBefore, Position cursor)
{
    if (m_depth == 0) {
        m_undo.append(Step{before, m_lines, cursor, idsBefore, m_ids});
        m_redo.clear();
    } else {
        m_openChanged = true;
    }
}

QVariant TextBuffer::anchor(Position p) const
{
    if (p.line < 0 || p.line >= m_ids.size())
        return Buffer::anchor(p);
    return QVariant::fromValue(QList<qulonglong>{m_ids[p.line], qulonglong(p.column)});
}

Position TextBuffer::anchorPosition(const QVariant &anchor) const
{
    const QList<qulonglong> l = anchor.value<QList<qulonglong>>();
    if (l.size() != 2)
        return Buffer::anchorPosition(anchor);
    const int line = int(m_ids.indexOf(l[0]));
    if (line < 0)
        return {-1, 0};
    return Position{line, std::min(int(l[1]), int(m_lines[line].size()))};
}

bool TextBuffer::viewLines(int &first, int &last) const
{
    first = std::clamp(m_viewTop, 0, lineCount() - 1);
    last = std::min(lineCount() - 1, first + std::max(1, m_pageLines) - 1);
    return true;
}

bool TextBuffer::replace(Position from, Position to, const QString &text)
{
    if (readOnly)
        return false;
    const QStringList before = m_lines;
    const QList<quint64> idsBefore = m_ids;
    QString head = m_lines[from.line].left(from.column);
    QString tail = m_lines[to.line].mid(to.column);
    QStringList inserted = QString(head + text + tail).split(u'\n');
    const quint64 keptId = m_ids[from.line];
    m_lines.remove(from.line, to.line - from.line + 1);
    m_ids.remove(from.line, to.line - from.line + 1);
    for (int i = 0; i < inserted.size(); ++i) {
        m_lines.insert(from.line + i, inserted[i]);
        m_ids.insert(from.line + i, i == 0 ? keptId : m_nextId++);
    }
    record(before, idsBefore, m_cursor);
    return true;
}

bool TextBuffer::removeLines(int first, int last)
{
    if (readOnly)
        return false;
    const QStringList before = m_lines;
    const QList<quint64> idsBefore = m_ids;
    m_lines.remove(first, last - first + 1);
    m_ids.remove(first, last - first + 1);
    if (m_lines.isEmpty()) {
        m_lines.append(QString());
        m_ids.append(m_nextId++);
    }
    record(before, idsBefore, m_cursor);
    return true;
}

int TextBuffer::putLines(int line, bool below, const Register &reg)
{
    if (readOnly)
        return -1;
    const QStringList before = m_lines;
    const QList<quint64> idsBefore = m_ids;
    const QStringList lines = reg.text.split(u'\n');
    const int at = below ? line + 1 : line;
    for (int i = 0; i < lines.size(); ++i) {
        m_lines.insert(at + i, lines[i]);
        m_ids.insert(at + i, m_nextId++);
    }
    record(before, idsBefore, m_cursor);
    return at;
}

Position TextBuffer::openLine(int line, bool below)
{
    if (readOnly)
        return {-1, 0};
    const QString indent = indentationOf(m_lines[line]);
    const int at = below ? line + 1 : line;
    const QStringList before = m_lines;
    const QList<quint64> idsBefore = m_ids;
    m_lines.insert(at, indent);
    m_ids.insert(at, m_nextId++);
    record(before, idsBefore, m_cursor);
    return Position{at, int(indent.size())};
}

bool TextBuffer::shiftLines(int first, int last, int levels)
{
    if (readOnly)
        return false;
    const QStringList before = m_lines;
    const QList<quint64> idsBefore = m_ids;
    const int width = m_indentUnit == u"\t" ? 8 : int(m_indentUnit.size());
    for (int l = first; l <= last && l < m_lines.size(); ++l) {
        QString &s = m_lines[l];
        if (s.isEmpty())
            continue;
        if (levels > 0) {
            s.prepend(m_indentUnit.repeated(levels));
        } else {
            for (int k = 0; k < -levels; ++k) {
                if (s.startsWith(u'\t')) {
                    s.remove(0, 1);
                    continue;
                }
                int n = 0;
                while (n < width && n < s.size() && s[n] == u' ')
                    ++n;
                s.remove(0, n);
            }
        }
    }
    record(before, idsBefore, m_cursor);
    return true;
}

void TextBuffer::beginChange()
{
    if (m_depth++ > 0)
        return;
    m_openBefore = m_lines;
    m_openIds = m_ids;
    m_openCursor = m_cursor;
    m_openChanged = false;
}

void TextBuffer::endChange()
{
    if (m_depth == 0 || --m_depth > 0)
        return;
    if (m_openChanged && m_openBefore != m_lines) {
        m_undo.append(Step{m_openBefore, m_lines, m_openCursor, m_openIds, m_ids});
        m_redo.clear();
    }
    m_openChanged = false;
}

bool TextBuffer::undo()
{
    if (m_undo.isEmpty())
        return false;
    Step s = m_undo.takeLast();
    m_lines = s.before;
    m_ids = s.idsBefore;
    m_redo.append(s);
    setCursor(s.cursorBefore);
    return true;
}

bool TextBuffer::redo()
{
    if (m_redo.isEmpty())
        return false;
    Step s = m_redo.takeLast();
    m_lines = s.after;
    m_ids = s.idsAfter;
    m_undo.append(s);
    setCursor(s.cursorBefore);
    return true;
}

} // namespace brulion::vim
