#include "vimpagebuffer.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using vim::Position;

VimPageBuffer::VimPageBuffer(PageController *controller)
{
    setController(controller);
}

VimPageBuffer::~VimPageBuffer()
{
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        QObject::disconnect(c);
}

void VimPageBuffer::setController(PageController *controller)
{
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        QObject::disconnect(c);
    m_connections.clear();
    m_controller = controller;
    m_dirty = true;
    m_changeDepth = 0;
    m_freedIds.clear();
    if (!controller)
        return;
    // Any change of the rows or their text: the lines are counted again when next asked (a
    // keystroke typed in insert mode costs one flag).
    BlockListModel *model = controller->blocks();
    const auto dirty = [this] { m_dirty = true; };
    m_connections << QObject::connect(model, &QAbstractItemModel::rowsInserted, model, dirty)
                  << QObject::connect(model, &QAbstractItemModel::rowsRemoved, model, dirty)
                  << QObject::connect(model, &QAbstractItemModel::rowsMoved, model, dirty)
                  << QObject::connect(model, &QAbstractItemModel::modelReset, model, dirty)
                  << QObject::connect(model, &QAbstractItemModel::layoutChanged, model, dirty)
                  << QObject::connect(model, &QAbstractItemModel::dataChanged, model,
                                      [this](const QModelIndex &from, const QModelIndex &to) {
                                          // A block's text: its own lines are read again (a
                                          // keystroke costs a block, not the page).
                                          m_textFrom = m_textFrom < 0 ? from.row() : std::min(m_textFrom, from.row());
                                          m_textTo = std::max(m_textTo, to.row());
                                      })
                  << QObject::connect(controller, &PageController::zoomChanged, controller, dirty)
                  << QObject::connect(controller, &PageController::foldStateChanged, controller, dirty)
                  << QObject::connect(controller, &PageController::loadingChanged, controller, dirty)
                  << QObject::connect(controller, &PageController::pathChanged, controller, dirty);
}

bool VimPageBuffer::refreshRows(int from, int to) const
{
    if (to - from > 64)
        return false; // (as costly as reading all)
    for (int r = from; r <= to; ++r) {
        const auto it = m_firstLine.constFind(r);
        const PageController::TextRow row = m_controller->textRow(r);
        if (it == m_firstLine.cend()) {
            if (row.row >= 0)
                return false; // a row shown now that was not
            continue;
        }
        if (row.row < 0)
            return false;
        int first = *it;
        int count = 0;
        while (first + count < m_lines.size() && m_lines.at(first + count).row == r)
            ++count;
        if (row.opaque != m_lines.at(first).opaque || int(row.text.count(u'\n')) + 1 != count)
            return false;
        if (row.opaque)
            continue;
        int start = 0;
        for (int k = 0; k < count; ++k) {
            const qsizetype nl = row.text.indexOf(u'\n', start);
            const int end = nl < 0 ? int(row.text.size()) : int(nl);
            m_lines[first + k].offset = start;
            m_lines[first + k].length = end - start;
            m_texts[first + k] = row.text;
            start = end + 1;
        }
    }
    return true;
}

void VimPageBuffer::rebuild() const
{
    if (!m_dirty && m_textTo >= 0 && m_controller) {
        const int from = m_textFrom;
        const int to = m_textTo;
        m_textFrom = m_textTo = -1;
        if (refreshRows(from, to))
            return;
        m_dirty = true;
    }
    if (!m_dirty)
        return;
    m_dirty = false;
    m_textFrom = m_textTo = -1;
    m_lines.clear();
    m_texts.clear();
    m_firstLine.clear();
    if (m_controller) {
        const QList<PageController::TextRow> rows = m_controller->textRows();
        for (const PageController::TextRow &r : rows) {
            m_firstLine.insert(r.row, int(m_lines.size()));
            if (r.opaque) {
                m_lines.append(Line{r.row, 0, 0, 0, true});
                m_texts.append(QString());
                continue;
            }
            int start = 0;
            int k = 0;
            for (;;) {
                const qsizetype nl = r.text.indexOf(u'\n', start);
                const int end = nl < 0 ? int(r.text.size()) : int(nl);
                m_lines.append(Line{r.row, k++, start, end - start, false});
                m_texts.append(r.text);
                if (nl < 0)
                    break;
                start = int(nl) + 1;
            }
        }
    }
    if (m_lines.isEmpty()) {
        m_lines.append(Line{});
        m_texts.append(QString());
    }
}

VimPageBuffer::Place VimPageBuffer::place(int line) const
{
    rebuild();
    if (line < 0 || line >= m_lines.size())
        return {};
    const Line &l = m_lines.at(line);
    return Place{l.row, l.lineInBlock, l.offset, l.length, l.opaque};
}

int VimPageBuffer::lineOf(int row, int offset) const
{
    rebuild();
    const auto it = m_firstLine.constFind(row);
    if (it == m_firstLine.cend())
        return -1;
    int line = *it;
    while (line + 1 < m_lines.size() && m_lines.at(line + 1).row == row && m_lines.at(line + 1).offset <= offset)
        ++line;
    return line;
}

int VimPageBuffer::lineCount() const
{
    rebuild();
    return int(m_lines.size());
}

QString VimPageBuffer::line(int i) const
{
    rebuild();
    if (i < 0 || i >= m_lines.size())
        return {};
    const Line &l = m_lines.at(i);
    return m_texts.at(i).mid(l.offset, l.length);
}

Position VimPageBuffer::clamped(Position p) const
{
    rebuild();
    p.line = std::clamp(p.line, 0, int(m_lines.size()) - 1);
    p.column = std::clamp(p.column, 0, m_lines.at(p.line).length);
    return p;
}

Position VimPageBuffer::cursor() const
{
    rebuild();
    if (!m_controller)
        return {};
    const int editing = m_controller->editingRow();
    // A visual selection: the caret is Vim's (the text's caret stands after the selected end),
    // unless the user went to another block meanwhile.
    if (m_selectionShown && (m_showingBlocks || editing == m_selectionRow))
        return clamped(m_cursor);
    if (editing >= 0) {
        const int line = lineOf(editing, m_controller->cursorPosition());
        if (line >= 0) {
            const Line &l = m_lines.at(line);
            return Position{line, std::clamp(m_controller->cursorPosition() - l.offset, 0, l.length)};
        }
    }
    const int selected = m_controller->selectedRow();
    if (selected >= 0) {
        const int line = lineOf(selected);
        if (line >= 0) {
            if (m_cursor.line >= line && m_cursor.line < m_lines.size() && m_lines.at(m_cursor.line).row == selected)
                return clamped(m_cursor);
            return Position{line, 0};
        }
    }
    return clamped(m_cursor);
}

void VimPageBuffer::clearTextSelection()
{
    if (m_selectionRow < 0)
        return;
    m_selectionRow = m_selectionAnchor = m_selectionCursor = -1;
    if (onTextSelection)
        onTextSelection(-1, -1, -1);
}

void VimPageBuffer::clearBand()
{
    if (!m_showingBlocks)
        return;
    m_showingBlocks = false;
    if (m_controller)
        m_controller->showLineSelection({});
}

void VimPageBuffer::setCursor(Position p)
{
    if (!m_controller)
        return;
    p = clamped(p);
    m_cursor = p;
    m_selectionShown = false;
    clearBand();
    clearTextSelection();
    const Line &l = m_lines.at(p.line);
    if (l.row < 0)
        return;
    if (l.opaque) {
        // A table: the block is selected (its cells are edited in the table).
        m_controller->stopEditing();
        m_controller->selectRange(l.row, l.row);
        return;
    }
    m_controller->placeCaret(l.row, l.offset + p.column);
}

void VimPageBuffer::showSelection(Position anchor, Position cursor, vim::SelectionShape shape)
{
    if (!m_controller)
        return;
    if (shape == vim::SelectionShape::None) {
        setCursor(cursor);
        return;
    }
    anchor = clamped(anchor);
    cursor = clamped(cursor);
    const Line &a = m_lines.at(anchor.line);
    const Line &c = m_lines.at(cursor.line);
    m_cursor = cursor;
    m_selectionShown = true;
    if (a.row == c.row && !a.opaque && a.row >= 0) {
        // In the edited block's text.
        clearBand();
        const QString text = m_texts.at(cursor.line);
        int from = 0, to = 0;
        if (shape == vim::SelectionShape::Lines) {
            const Line &first = m_lines.at(std::min(anchor.line, cursor.line));
            const Line &last = m_lines.at(std::max(anchor.line, cursor.line));
            from = first.offset;
            to = last.offset + last.length;
            if (cursor < anchor)
                std::swap(from, to);
        } else {
            from = a.offset + anchor.column;
            to = c.offset + cursor.column;
            // Both ends included: the later one's character too.
            if (to >= from)
                to = std::min(int(text.size()), vim::nextBoundary(text, to));
            else
                from = std::min(int(text.size()), vim::nextBoundary(text, from));
        }
        m_controller->placeCaret(c.row, c.offset + cursor.column);
        m_selectionRow = c.row;
        m_selectionAnchor = from;
        m_selectionCursor = to;
        if (onTextSelection)
            onTextSelection(c.row, from, to);
        return;
    }
    // Over several blocks: the caret's block stays edited, the blocks of the selection are drawn
    // as selected (a band, not the editor's block selection: no subtrees, no tools).
    clearTextSelection();
    if (a.row < 0 || c.row < 0)
        return;
    if (c.opaque) {
        m_controller->stopEditing();
    } else {
        m_controller->placeCaret(c.row, c.offset + std::min(cursor.column, c.length));
    }
    QList<int> rows;
    for (int i = std::min(anchor.line, cursor.line); i <= std::max(anchor.line, cursor.line); ++i)
        if (rows.isEmpty() || rows.last() != m_lines.at(i).row)
            rows.append(m_lines.at(i).row);
    m_showingBlocks = true;
    m_controller->showLineSelection(rows);
}

bool VimPageBuffer::ensureBlock()
{
    rebuild();
    if (!m_controller)
        return false;
    if (m_lines.at(0).row >= 0)
        return true;
    const int row = m_controller->appendEmptyBlock();
    m_dirty = true;
    return row >= 0;
}

bool VimPageBuffer::replace(Position from, Position to, const QString &text)
{
    if (!m_controller || !ensureBlock())
        return false;
    from = clamped(from);
    to = clamped(to);
    const Line a = m_lines.at(from.line);
    const Line b = m_lines.at(to.line);
    if (a.row < 0 || b.row < 0)
        return false;
    if (from.line != to.line) {
        // The block ids a delete takes (a put on this page keeps them: dd p is a move).
        for (int i = from.line + 1; i <= to.line; ++i)
            if (m_lines.at(i).lineInBlock == 0 && i < to.line)
                m_freedIds.unite(m_controller->rowsBlockIds(m_lines.at(i).row, m_lines.at(i).row));
    }
    const quint64 uid = m_controller->blocks()->uidAt(a.row);
    const QString head = line(from.line).left(from.column);
    const QString tail = line(to.line).mid(to.column);
    const bool ok = m_controller->replaceAcross(a.row, a.offset + from.column, b.row, b.offset + to.column, text);
    if (from.line != to.line || text.contains(u'\n'))
        m_dirty = true; // (within a line the model's own signals tell the changed row)
    // A list item's text cannot start with blanks (they belong to its marker): what an edit left
    // there is gone from the text. Inside a change that types next (cw on the first word), the
    // blanks go back after what is typed (endChange()).
    m_lostLead = {};
    if (ok && m_changeDepth > 0 && a.lineInBlock == 0 && head.isEmpty() && !text.contains(u'\n')) {
        const QString expected = text + tail;
        const QString lead = vim::indentationOf(expected);
        const int row = m_controller->blocks()->rowOfUid(uid);
        const QString now = m_controller->editTextOf(row);
        if (!lead.isEmpty() && now == expected.mid(lead.size()))
            m_lostLead = LostLead{uid, lead, now};
    }
    return ok;
}

bool VimPageBuffer::removeLines(int first, int last)
{
    if (!m_controller)
        return false;
    rebuild();
    first = std::clamp(first, 0, int(m_lines.size()) - 1);
    last = std::clamp(last, first, int(m_lines.size()) - 1);
    const Line a = m_lines.at(first);
    const Line b = m_lines.at(last);
    if (a.row < 0)
        return true; // an empty page: nothing to remove
    m_freedIds.unite(m_controller->rowsBlockIds(a.row, b.row));
    const bool ok = m_controller->removeLines(a.row, a.lineInBlock, b.row, b.lineInBlock);
    m_dirty = true;
    return ok;
}

int VimPageBuffer::putLines(int line, bool below, const vim::Register &reg)
{
    if (!m_controller || !ensureBlock())
        return -1;
    rebuild();
    line = std::clamp(line, 0, int(m_lines.size()) - 1);
    const Line l = m_lines.at(line);
    const quint64 uid = m_controller->blocks()->uidAt(l.row);
    const QStringList lines = reg.text.split(u'\n');
    const int row = m_controller->putLines(l.row, l.lineInBlock, below, lines, reg.blocks, m_freedIds);
    m_dirty = true;
    if (row < 0)
        return -1;
    rebuild();
    if (m_controller->blocks()->uidAt(row) == uid) // into the block's text
        return std::min(int(m_lines.size()) - 1, below ? line + 1 : line);
    return std::max(0, lineOf(row));
}

Position VimPageBuffer::openLine(int line, bool below)
{
    if (!m_controller || !ensureBlock())
        return {-1, 0};
    rebuild();
    line = std::clamp(line, 0, int(m_lines.size()) - 1);
    const Line l = m_lines.at(line);
    const QVariantMap r = m_controller->openLine(l.row, l.lineInBlock, below);
    const int row = r.value(u"row"_s, -1).toInt();
    if (row < 0)
        return {-1, 0};
    m_dirty = true;
    const int cursor = r.value(u"cursor"_s, 0).toInt();
    const int at = lineOf(row, cursor);
    if (at < 0)
        return {-1, 0};
    return Position{at, std::clamp(cursor - m_lines.at(at).offset, 0, m_lines.at(at).length)};
}

bool VimPageBuffer::shiftLines(int first, int last, int levels)
{
    if (!m_controller)
        return false;
    rebuild();
    const Line a = m_lines.at(std::clamp(first, 0, int(m_lines.size()) - 1));
    const Line b = m_lines.at(std::clamp(last, 0, int(m_lines.size()) - 1));
    if (a.row < 0)
        return false;
    if (m_controller->readOnly())
        return false;
    // (A block that cannot go deeper is no refusal: Tab does nothing either.)
    m_controller->shiftLines(a.row, a.lineInBlock, b.row, b.lineInBlock, levels);
    m_dirty = true;
    return true;
}

vim::Register VimPageBuffer::linesRegister(int first, int last) const
{
    vim::Register r = Buffer::linesRegister(first, last);
    rebuild();
    if (!m_controller || first < 0 || last >= m_lines.size())
        return r;
    const Line &a = m_lines.at(first);
    const Line &b = m_lines.at(last);
    const bool lastWhole = last + 1 >= m_lines.size() || m_lines.at(last + 1).row != b.row;
    if (a.row >= 0 && a.lineInBlock == 0 && lastWhole)
        r.blocks = m_controller->rowsMarkdown(a.row, b.row);
    return r;
}

void VimPageBuffer::beginChange()
{
    if (m_changeDepth++ == 0 && m_controller)
        m_changeToken = m_controller->undoToken();
}

void VimPageBuffer::endChange()
{
    if (m_changeDepth == 0 || --m_changeDepth > 0)
        return;
    if (m_lostLead.uid && m_controller) {
        // What was typed where the blanks were: they follow it again ("cwŻółw" in "- Zażółć gęślą").
        const int row = m_controller->blocks()->rowOfUid(m_lostLead.uid);
        const QString now = m_controller->editTextOf(row);
        if (row >= 0 && now.size() > m_lostLead.tail.size() && now.endsWith(m_lostLead.tail)) {
            const int at = int(now.size() - m_lostLead.tail.size());
            if (!now.left(at).endsWith(u' ') && !now.left(at).endsWith(u'\t')) {
                const bool editing = m_controller->editingRow() == row;
                const int caret = m_controller->cursorPosition();
                m_controller->replaceAcross(row, at, row, at, m_lostLead.lead);
                if (editing) // (the caret stays after what was typed)
                    m_controller->placeCaret(row, caret);
            }
        }
        m_lostLead = {};
    }
    if (m_controller)
        m_controller->joinUndoSteps(m_changeToken);
    m_changeToken.clear();
}

bool VimPageBuffer::undo()
{
    if (!m_controller || !m_controller->canUndo())
        return false;
    m_selectionShown = false;
    clearBand();
    clearTextSelection();
    m_controller->undo();
    m_dirty = true;
    return true;
}

bool VimPageBuffer::redo()
{
    if (!m_controller || !m_controller->canRedo())
        return false;
    m_selectionShown = false;
    clearBand();
    clearTextSelection();
    m_controller->redo();
    m_dirty = true;
    return true;
}

void VimPageBuffer::modeChanged(vim::Mode mode)
{
    if (mode == vim::Mode::Insert || mode == vim::Mode::Replace)
        ensureBlock();
    if (mode == vim::Mode::Normal || mode == vim::Mode::Insert || mode == vim::Mode::Replace) {
        m_selectionShown = false;
        clearBand();
        clearTextSelection();
    }
    if (onMode)
        onMode(mode);
}

bool VimPageBuffer::save()
{
    return m_controller && m_controller->save();
}

void VimPageBuffer::quit(bool force)
{
    if (m_controller)
        m_controller->save(); // :q! loses no text either: notes are saved as they are typed
    if (onQuit)
        onQuit(force);
}

bool VimPageBuffer::fold(int line, vim::FoldAction action)
{
    if (!m_controller)
        return false;
    const Place p = place(line);
    switch (action) {
    case vim::FoldAction::OpenAll: m_controller->foldAll(false); break;
    case vim::FoldAction::CloseAll: m_controller->foldAll(true); break;
    case vim::FoldAction::Toggle:
        if (p.row < 0)
            return false;
        m_controller->toggleFold(p.row);
        break;
    case vim::FoldAction::Open:
    case vim::FoldAction::Close:
        if (p.row < 0)
            return false;
        m_controller->setFolded(p.row, action == vim::FoldAction::Close);
        break;
    }
    m_dirty = true;
    return true;
}

void VimPageBuffer::scroll(int line, vim::ScrollTo where)
{
    const Place p = place(line);
    if (p.row >= 0 && onScroll)
        onScroll(p.row, where);
}

QVariant VimPageBuffer::anchor(Position p) const
{
    const Place pl = place(p.line);
    if (!m_controller || pl.row < 0)
        return Buffer::anchor(p);
    return QVariant::fromValue(
            QList<qulonglong>{m_controller->blocks()->uidAt(pl.row), qulonglong(pl.lineInBlock), qulonglong(p.column)});
}

Position VimPageBuffer::anchorPosition(const QVariant &anchor) const
{
    const QList<qulonglong> l = anchor.value<QList<qulonglong>>();
    if (l.size() != 3 || !m_controller)
        return {-1, 0};
    const int row = m_controller->blocks()->rowOfUid(l[0]);
    int line = row >= 0 ? lineOf(row) : -1;
    if (line < 0)
        return {-1, 0};
    for (qulonglong k = 0; k < l[1] && line + 1 < m_lines.size() && m_lines.at(line + 1).row == row; ++k)
        ++line;
    return Position{line, std::min(int(l[2]), m_lines.at(line).length)};
}

} // namespace brulion::editor
