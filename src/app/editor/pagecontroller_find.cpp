// PageController: where a piece of the page's text is shown, and replacing pieces of it (the find bar,
// NoteFind; docs/UI.md "Find and replace").
#include "blocktext.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include "text/text.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

using markdown::BlockDocument;

namespace {

/// Offset of each line's first character.
QList<qsizetype> lineStarts(QStringView text)
{
    QList<qsizetype> starts{0};
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'\n' || (c == u'\r' && (i + 1 == text.size() || text.at(i + 1) != u'\n')))
            starts.append(i + 1);
    }
    return starts;
}

/// The end of the line starting at `start`, without its terminator.
qsizetype lineEnd(QStringView text, qsizetype start)
{
    qsizetype end = start;
    while (end < text.size() && text.at(end) != u'\n' && text.at(end) != u'\r')
        ++end;
    return end;
}

} // namespace

qsizetype PageController::textOffsetOfRow(int row) const
{
    if (!validRow(row))
        return 0;
    const QString current = text();
    const QList<qsizetype> starts = lineStarts(current);
    const int line = m_model->document().at(row).line;
    return starts.value(line, current.size());
}

PageController::TextLocation PageController::locateText(qsizetype start, qsizetype length) const
{
    const QString current = text();
    if (start < 0 || length < 0 || start > current.size() - length)
        return {};
    const QList<qsizetype> starts = lineStarts(current);
    const int line = int(std::ranges::upper_bound(starts, start) - starts.begin()) - 1;
    const BlockDocument &doc = m_model->document();
    const int row = doc.indexAtLine(line);
    if (row < 0)
        return {};
    const TextLocation rowOnly = {.row = row}; // the block's inline editor cannot select the text

    // A line of the file ends with the same line of the block's body: a list item's marker or its
    // continuation's indentation comes before it (markdown::BlockEntry::body).
    const markdown::BlockEntry &entry = doc.at(row);
    const QList<QStringView> bodyLines = QStringView(entry.body).split(u'\n');
    const int inBlock = line - entry.line;
    if (inBlock < 0 || inBlock >= bodyLines.size())
        return rowOnly; // a blank line after the block
    const qsizetype lineStart = starts.at(line);
    const QStringView fileLine = QStringView(current).sliced(lineStart, lineEnd(current, lineStart) - lineStart);
    const QStringView bodyLine = bodyLines.at(inBlock);
    if (!fileLine.endsWith(bodyLine))
        return rowOnly;
    const qsizetype hiddenInLine = fileLine.size() - bodyLine.size();
    const qsizetype column = start - lineStart - hiddenInLine;
    if (column < 0)
        return rowOnly; // in the marker or the indentation
    qsizetype bodyAt = column;
    for (int k = 0; k < inBlock; ++k)
        bodyAt += bodyLines.at(k).size() + 1;

    const BlockShape shape = m_model->shapeAt(row);
    const qsizetype from = bodyAt - shape.editOffset;
    if (from < 0 || from + length > shape.editText.size() || shape.view == BlockView::Table)
        return rowOnly; // a task marker, a fence line; a table's cells have editors of their own
    return {.row = row, .from = int(from), .to = int(from + length), .editable = true};
}

bool PageController::replaceTextRanges(const QList<text::TextRange> &ranges, const QString &replacement)
{
    if (refusesEdit() || !m_loaded || m_sourceMode || ranges.isEmpty())
        return false;
    const QString current = text();
    const QString updated = text::replaceMatches(current, ranges, replacement);
    if (updated == current)
        return false;
    applyText(updated, Focus{-1, -1});
    // The block being edited shows its new text; the caret stays where it can.
    emitEditTextReset();
    return true;
}

} // namespace brulion::editor
