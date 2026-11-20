// PageController — the block operations Vim's keys need (docs/UI.md "Vim mode"; VimPageBuffer,
// vimpagebuffer.h, maps the engine's lines onto them). Each is one undo step; VimPageBuffer joins
// the steps of one change of Vim (joinUndoSteps(), pagecontroller.cpp).
#include "blocktext.h"
#include "editorhost.h"
#include "markdown/blockclipboard.h"
#include "markdown/blockdocument.h"
#include "pagecontroller.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using markdown::BlockDocument;
using markdown::EditResult;

namespace {

/// A block whose lines are lines of its own text (Enter makes a line break in it): code and the
/// fenced views, quotes, callouts, HTML.
bool linesInText(BlockView view)
{
    switch (view) {
    case BlockView::Text:
    case BlockView::Heading:
    case BlockView::Rule:
    case BlockView::Table:
    case BlockView::Frontmatter: return false;
    default: return true;
    }
}

/// Vim edits no text of tables (cells) or the frontmatter (the properties panel).
bool refusesText(BlockView view)
{
    return view == BlockView::Table || view == BlockView::Frontmatter;
}

} // namespace

void PageController::placeCaret(int row, int cursor)
{
    if (!validRow(row))
        return;
    if (editingRow() == row) {
        const QString edit = m_model->editTextAt(row);
        m_cursor = snapToCharacter(edit, std::clamp(cursor, 0, int(edit.size())));
        Q_EMIT cursorPositionChanged();
        Q_EMIT editTextReset(row, edit, m_cursor);
        return;
    }
    if (m_selectedUid || m_anchorUid)
        clearSelection();
    startEditing(row, std::max(0, cursor));
}

bool PageController::replaceAcross(int fromRow, int from, int toRow, int to, const QString &text)
{
    if (refusesEdit() || m_sourceMode || !validRow(fromRow) || !validRow(toRow) || toRow < fromRow)
        return false;
    const BlockShape a = m_model->shapeAt(fromRow);
    const BlockShape b = m_model->shapeAt(toRow);
    if (refusesText(a.view) || refusesText(b.view))
        return false;
    from = std::clamp(from, 0, int(a.editText.size()));
    to = std::clamp(to, 0, int(b.editText.size()));
    if (fromRow == toRow && to < from)
        return false;
    QString inserted = text;
    inserted.replace(u"\r\n"_s, u"\n"_s).replace(u'\r', u'\n');
    const QString head = a.editText.left(from);
    const QString tail = b.editText.mid(to);
    // In a text block a line break starts a new block; code, quotes… keep it as a line.
    QStringList parts = linesInText(a.view) ? QStringList{inserted} : inserted.split(u'\n');
    parts.first().prepend(head);
    parts.last().append(tail);
    // A list item's text never starts with blanks (they would join its marker: "-  text").
    if (head.isEmpty() && m_model->document().at(fromRow).kind == BlockKind::ListItem) {
        qsizetype lead = 0;
        while (lead < parts.first().size() && (parts.first()[lead] == u' ' || parts.first()[lead] == u'\t'))
            ++lead;
        if (lead > 0 && (parts.size() > 1 || lead < parts.first().size()))
            parts.first().remove(0, lead);
    }
    const quint64 uid = m_model->uidAt(fromRow);
    const quint64 lastUid = m_model->uidAt(toRow);
    const bool editingThis = editingRow() == fromRow;
    if (fromRow == toRow && parts.size() == 1) {
        // Text within one block: typing's way (x x x in a block costs what keystrokes cost; the
        // change's steps are joined into one by joinUndoSteps()).
        setEditText(fromRow, parts.first(), editingThis ? int(parts.first().size() - tail.size()) : -1);
        if (editingThis && editingRow() == fromRow)
            emitEditTextReset();
        return true;
    }
    beginCommand();
    // The rows between the two go, the last one first: a folded one with what it hides.
    for (int r = toRow - 1; r > fromRow; --r) {
        if (!m_model->isRowVisible(r))
            continue;
        const bool withChildren = m_model->isCollapsed(r);
        applyOp([&](BlockDocument &d) { return d.remove(r, withChildren); });
    }
    int row = m_model->rowOfUid(uid);
    if (row < 0) {
        endCommand(currentFocus());
        return false;
    }
    if (fromRow != toRow && parts.size() == 1) {
        // Two blocks joined as Backspace at the start of the second joins them (its children
        // follow the first; the blank lines between go): the first keeps what is before the range
        // with the text, the second what is after it.
        const QString firstText = parts.first().left(parts.first().size() - tail.size());
        applyOp([&](BlockDocument &d) { return d.setBody(row, bodyWithEditText(d.at(row), firstText)); });
        const int second = m_model->rowOfUid(lastUid);
        if (second >= 0) {
            applyOp([&](BlockDocument &d) { return d.setBody(second, bodyWithEditText(d.at(second), tail)); });
            const EditResult merged = applyOp([&](BlockDocument &d) { return d.mergeWithPrevious(second); });
            if (!merged.changed() && m_model->rowOfUid(lastUid) >= 0) {
                // Not a text block before it (code…): the text goes into the first, the block goes.
                row = m_model->rowOfUid(uid);
                const QString joined = m_model->editTextAt(row) + tail;
                applyOp([&](BlockDocument &d) { return d.setBody(row, bodyWithEditText(d.at(row), joined)); });
                const int gone = m_model->rowOfUid(lastUid);
                if (gone >= 0)
                    applyOp([&](BlockDocument &d) { return d.remove(gone, false); });
            }
        }
        endCommand(Focus{m_model->rowOfUid(uid), int(firstText.size())});
        if (editingThis && editingRow() == m_model->rowOfUid(uid))
            emitEditTextReset();
        return true;
    }
    if (fromRow != toRow) {
        // The last row goes too (its children move up: they follow its text).
        const int last = m_model->rowOfUid(lastUid);
        if (last >= 0)
            applyOp([&](BlockDocument &d) { return d.remove(last, false); });
        row = m_model->rowOfUid(uid);
    }
    const QString first = parts.first();
    applyOp([&](BlockDocument &d) { return d.setBody(row, bodyWithEditText(d.at(row), first)); });
    row = m_model->rowOfUid(uid);
    const markdown::BlockEntry &entry = m_model->document().at(row);
    const int depth = entry.depth;
    const bool listItem = entry.kind == BlockKind::ListItem || !documentMode();
    int last = row;
    for (int k = 1; k < parts.size(); ++k) {
        const QString body = parts.at(k);
        const EditResult r = applyOp([&](BlockDocument &d) { return d.insertAfter(last, body, depth, listItem); });
        if (r.focus >= 0)
            last = r.focus;
    }
    endCommand(Focus{last, -1});
    if (editingThis && editingRow() == m_model->rowOfUid(uid))
        emitEditTextReset();
    return true;
}

PageController::TextRow PageController::textRow(int row) const
{
    if (!validRow(row) || !m_model->isRowVisible(row))
        return {};
    const BlockShape shape = m_model->shapeAt(row);
    if (shape.view == BlockView::Frontmatter)
        return {};
    const bool opaque = refusesText(shape.view);
    return TextRow{row, opaque ? QString() : shape.editText, opaque, m_model->isCollapsed(row)};
}

void PageController::showLineSelection(const QList<int> &rows)
{
    if (!rows.isEmpty() && (m_selectedUid || m_anchorUid))
        clearSelection();
    if (m_selectedUid || m_anchorUid)
        return; // (a block selection of the controller's own draws its rows)
    QSet<quint64> uids;
    for (const int r : rows)
        if (validRow(r))
            uids.insert(m_model->uidAt(r));
    m_model->setSelectedUids(uids);
}

QString PageController::editTextOf(int row) const
{
    return validRow(row) ? m_model->editTextAt(row) : QString();
}

bool PageController::removeLines(int firstRow, int firstLine, int lastRow, int lastLine)
{
    if (refusesEdit() || m_sourceMode || !validRow(firstRow) || !validRow(lastRow) || lastRow < firstRow)
        return false;
    const int zoom = zoomBlock();
    bool zoomGone = false;
    beginCommand();
    for (int r = lastRow; r >= firstRow; --r) {
        if (!m_model->isRowVisible(r) && r != firstRow && r != lastRow)
            continue;
        const BlockShape shape = m_model->shapeAt(r);
        const QStringList lines = shape.editText.split(u'\n');
        const int from = r == firstRow ? std::clamp(firstLine, 0, int(lines.size()) - 1) : 0;
        const int to = r == lastRow ? std::clamp(lastLine, 0, int(lines.size()) - 1) : int(lines.size()) - 1;
        // (A table is one line of Vim's: the whole of it.)
        if ((from == 0 && to == lines.size() - 1) || refusesText(shape.view)) {
            // The whole block (a folded one with its children; an open one's children move up).
            if (r == zoom)
                zoomGone = true;
            const bool withChildren = m_model->isCollapsed(r) || r == zoom;
            applyOp([&](BlockDocument &d) { return d.remove(r, withChildren); });
            continue;
        }
        if (refusesText(shape.view))
            continue;
        QStringList kept = lines;
        kept.remove(from, to - from + 1);
        const QString edit = kept.join(u'\n');
        applyOp([&](BlockDocument &d) { return d.setBody(r, bodyWithEditText(d.at(r), edit)); });
    }
    // Vim keeps one empty line: a page (or a zoom) without blocks gets an empty one.
    if (m_model->document().isEmpty() || (!zoomGone && m_model->firstVisibleRow() < 0)) {
        const int after = m_model->document().count() - 1;
        applyOp([&](BlockDocument &d) { return d.insertAfter(after, QString(), 0, !documentMode()); });
    }
    endCommand(Focus{std::min(firstRow, m_model->count() - 1), 0});
    if (zoomGone)
        zoomTo(-1);
    if (editingRow() >= 0)
        emitEditTextReset();
    return true;
}

int PageController::putLines(int row, int line, bool below, const QStringList &lines, const QString &markdown,
                             const QSet<QString> &freedIds)
{
    if (refusesEdit() || m_sourceMode || !validRow(row))
        return -1;
    const BlockShape shape = m_model->shapeAt(row);
    if (markdown.isEmpty() && linesInText(shape.view)) {
        // Into the text: below / above that line.
        QStringList own = shape.editText.split(u'\n');
        const int at = std::clamp(below ? line + 1 : line, 0, int(own.size()));
        for (int i = 0; i < lines.size(); ++i)
            own.insert(at + i, lines.at(i));
        const QString edit = own.join(u'\n');
        beginCommand();
        applyOp([&](BlockDocument &d) { return d.setBody(row, bodyWithEditText(d.at(row), edit)); });
        endCommand(Focus{row, -1});
        if (editingRow() == row)
            emitEditTextReset();
        return row;
    }
    struct Piece {
        QString body;
        int depth = 0;
        bool listItem = true;
    };
    QList<Piece> pieces;
    const markdown::BlockEntry &target = m_model->document().at(row);
    const bool intoList =
            target.kind == BlockKind::ListItem || (!documentMode() && target.kind == BlockKind::Paragraph);
    if (!markdown.isEmpty()) {
        QString text = markdown;
        text.replace(u"\r\n"_s, u"\n"_s);
        // Ids that resolve elsewhere would be duplicated (yy p); a deleted block's stay (dd p).
        text = withoutTakenIds(text, freedIds);
        const BlockDocument doc(text);
        const int base = doc.isEmpty() ? 0 : doc.at(0).depth;
        for (int k = 0; k < doc.count(); ++k) {
            const markdown::BlockEntry &e = doc.at(k);
            QString body = e.body;
            if (const QString closing = missingClosingFence(body); !closing.isEmpty())
                body += u'\n' + closing;
            pieces.append(Piece{body, std::max(0, e.depth - base), intoList || e.kind == BlockKind::ListItem});
        }
    }
    if (pieces.isEmpty()) {
        for (const QString &l : lines)
            pieces.append(Piece{l, 0, intoList});
    }
    if (pieces.isEmpty())
        return -1;
    const int depth = target.depth;
    beginCommand();
    // Below: after the block and its children; above: where the block is (after the row before it
    // at the block's depth — its parent, or its previous sibling's subtree).
    int last = below ? row : row - 1;
    int firstNew = -1;
    quint64 firstUid = 0;
    for (const Piece &p : pieces) {
        const int at = last;
        const int d = depth + p.depth;
        const EditResult r = applyOp([&](BlockDocument &doc) { return doc.insertAfter(at, p.body, d, p.listItem); });
        if (r.focus >= 0) {
            last = r.focus;
            if (!firstUid)
                firstUid = m_model->uidAt(r.focus);
        }
    }
    firstNew = firstUid ? m_model->rowOfUid(firstUid) : -1;
    endCommand(Focus{firstNew, 0});
    return firstNew;
}

QVariantMap PageController::openLine(int row, int line, bool below)
{
    const auto result = [](int r, int c) { return QVariantMap{{u"row"_s, r}, {u"cursor"_s, c}}; };
    if (refusesEdit() || m_sourceMode || !validRow(row))
        return result(-1, 0);
    const BlockShape shape = m_model->shapeAt(row);
    const QStringList lines = shape.editText.split(u'\n');
    line = std::clamp(line, 0, int(lines.size()) - 1);
    const bool edge = below ? line == lines.size() - 1 : line == 0;
    const markdown::BlockEntry &entry = m_model->document().at(row);
    if (refusesText(shape.view) || (!linesInText(shape.view) && edge && (below || shape.editText.isEmpty()))) {
        // A new block after (or before) this one at its depth.
        const int depth = entry.depth;
        const bool listItem = entry.kind == BlockKind::ListItem || !documentMode();
        if (below && !refusesText(shape.view)) {
            // Enter at the block's end (a task goes on as a task; a block with children gets its
            // first child).
            const QVariantMap r = split(row, int(shape.editText.size()));
            const int newRow = r.value(u"row"_s, -1).toInt();
            if (newRow >= 0)
                return result(newRow, r.value(u"cursor"_s, 0).toInt());
        }
        beginCommand();
        const int after = below ? row : row - 1;
        const EditResult r =
                applyOp([&](BlockDocument &d) { return d.insertAfter(after, QString(), depth, listItem); });
        endCommand(Focus{r.focus, 0});
        return result(r.focus, 0);
    }
    if (!linesInText(shape.view) && edge && !below) {
        // Enter at the start: an empty block above (a task: an empty task), the text below it.
        const quint64 uid = m_model->uidAt(row);
        split(row, 0);
        const int moved = m_model->rowOfUid(uid);
        const int newRow = moved > 0 ? m_model->previousVisibleRow(moved) : -1;
        if (newRow >= 0 && m_model->editTextAt(newRow).isEmpty())
            return result(newRow, 0);
        return result(moved, 0);
    }
    // A new line in the block's text, with the line's indentation.
    QString indent;
    for (const QChar c : lines.at(line)) {
        if (c != u' ' && c != u'\t')
            break;
        indent += c;
    }
    int offset = 0;
    for (int i = 0; i < line; ++i)
        offset += int(lines.at(i).size()) + 1;
    const int at = below ? offset + int(lines.at(line).size()) : offset;
    const QString inserted = below ? QString(u'\n' + indent) : QString(indent + u'\n');
    const QString edit = shape.editText.left(at) + inserted + shape.editText.mid(at);
    beginCommand();
    applyOp([&](BlockDocument &d) { return d.setBody(row, bodyWithEditText(d.at(row), edit)); });
    const int caret = below ? at + 1 + int(indent.size()) : at + int(indent.size());
    endCommand(Focus{row, caret});
    if (editingRow() == row)
        emitEditTextReset();
    return result(row, caret);
}

bool PageController::shiftLines(int firstRow, int firstLine, int lastRow, int lastLine, int levels)
{
    if (refusesEdit() || m_sourceMode || !validRow(firstRow) || !validRow(lastRow) || levels == 0)
        return false;
    const BlockShape shape = m_model->shapeAt(firstRow);
    if (firstRow == lastRow && linesInText(shape.view) && shape.editText.contains(u'\n')) {
        // Lines of code: a tab per level.
        QStringList lines = shape.editText.split(u'\n');
        for (int i = std::max(0, firstLine); i <= std::min(lastLine, int(lines.size()) - 1); ++i) {
            QString &l = lines[i];
            if (l.isEmpty())
                continue;
            for (int k = 0; k < std::abs(levels); ++k) {
                if (levels > 0)
                    l.prepend(u'\t');
                else if (l.startsWith(u'\t'))
                    l.remove(0, 1);
                else {
                    int n = 0;
                    while (n < 4 && n < l.size() && l[n] == u' ')
                        ++n;
                    l.remove(0, n);
                }
            }
        }
        const QString edit = lines.join(u'\n');
        if (edit == shape.editText)
            return false;
        beginCommand();
        applyOp([&](BlockDocument &d) { return d.setBody(firstRow, bodyWithEditText(d.at(firstRow), edit)); });
        endCommand(currentFocus());
        if (editingRow() == firstRow)
            emitEditTextReset();
        return true;
    }
    // Blocks, as Tab / Shift+Tab: a document indents list items only.
    if (documentMode() && m_model->document().at(firstRow).kind != BlockKind::ListItem)
        return false;
    const quint64 firstUid = m_model->uidAt(firstRow);
    const quint64 lastUid = m_model->uidAt(lastRow);
    bool changed = false;
    beginCommand();
    for (int k = 0; k < std::abs(levels); ++k) {
        const int a = m_model->rowOfUid(firstUid);
        const int b = m_model->rowOfUid(lastUid);
        if (a < 0 || b < 0)
            break;
        const EditResult r =
                applyOp([&](BlockDocument &d) { return levels > 0 ? d.indentRange(a, b) : d.outdentRange(a, b); });
        if (!r.changed())
            break;
        changed = true;
    }
    endCommand(currentFocus());
    if (changed && editingRow() >= 0)
        emitEditTextReset();
    return changed;
}

QString PageController::rowsMarkdown(int first, int last) const
{
    if (!validRow(first) || !validRow(last) || last < first)
        return {};
    // Each visible row, with the children its fold hides; an open block's children are rows of
    // their own (they are in the range or not).
    const BlockDocument &doc = m_model->document();
    QStringList parts;
    for (int r = first; r <= last; ++r) {
        if (!m_model->isRowVisible(r))
            continue;
        if (m_model->isCollapsed(r)) {
            parts.append(markdown::blocksMarkdown(doc, {r}));
            continue;
        }
        // The block alone: its source re-based like a copy (its children left out).
        BlockDocument one(doc.blockSource(r));
        parts.append(markdown::blocksMarkdown(one, one.isEmpty() ? QList<int>{} : QList<int>{0}));
    }
    // Nested rows keep their nesting relative to the first one.
    if (parts.size() > 1) {
        const int base = doc.at(first).depth;
        QString indented;
        for (int r = first, i = 0; r <= last; ++r) {
            if (!m_model->isRowVisible(r))
                continue;
            const int rel = std::max(0, doc.at(r).depth - base);
            QStringList ls = parts.at(i++).split(u'\n');
            for (QString &l : ls)
                if (!l.isEmpty())
                    l.prepend(QString(rel, u'\t'));
            if (!indented.isEmpty())
                indented += u'\n';
            indented += ls.join(u'\n');
        }
        return indented;
    }
    return parts.isEmpty() ? QString() : parts.first();
}

QList<PageController::TextRow> PageController::textRows() const
{
    QList<TextRow> out;
    for (int r = m_model->firstVisibleRow(); r >= 0; r = m_model->nextVisibleRow(r)) {
        const BlockShape shape = m_model->shapeAt(r);
        if (shape.view == BlockView::Frontmatter)
            continue;
        const bool opaque = refusesText(shape.view);
        out.append(TextRow{r, opaque ? QString() : shape.editText, opaque, m_model->isCollapsed(r)});
    }
    return out;
}

QSet<QString> PageController::rowsBlockIds(int first, int last) const
{
    QSet<QString> ids;
    const BlockDocument &doc = m_model->document();
    for (int r = std::max(0, first); r <= last && r < doc.count(); ++r) {
        const markdown::RowRange sub = doc.subtreeRange(r);
        for (int k = sub.first; k <= sub.last(); ++k)
            if (!doc.at(k).blockId.isEmpty())
                ids.insert(doc.at(k).blockId);
    }
    return ids;
}

} // namespace brulion::editor
