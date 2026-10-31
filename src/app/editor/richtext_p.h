// Shared geometry of rich-text spans for RichTextPills and RichTextLinks (internal): where a
// range of a laid-out QTextDocument block lies, line by line.
#pragma once

#include <QAbstractTextDocumentLayout>
#include <QList>
#include <QTextBlock>
#include <QTextLayout>

#include <algorithm>

namespace brulion::editor::detail {

/// One line's part of a range: the line and the range's horizontal extent in it (block layout
/// coordinates: add the layout's position).
struct LineSegment {
    QTextLine line;
    qreal x1 = 0;
    qreal x2 = 0;
};

/// The line segments of document positions [from, to) in `block`, laid out by `layout` with its
/// current width. A range going on in the next line ends at the line's text, not at the space the
/// line broke at; segments narrower than half a pixel are left out.
inline QList<LineSegment> lineSegments(QAbstractTextDocumentLayout *layout, const QTextBlock &block, int from, int to)
{
    QList<LineSegment> out;
    (void)layout->blockBoundingRect(block); // lays the block out (with the current width)
    const QTextLayout *text = block.layout();
    if (!text)
        return out;
    const int at = block.position();
    for (int i = 0; i < text->lineCount(); ++i) {
        const QTextLine line = text->lineAt(i);
        const int lineEnd = line.textStart() + line.textLength();
        const int start = std::max(from - at, line.textStart());
        const int end = std::min(to - at, lineEnd);
        if (start >= end)
            continue;
        qreal x1 = line.cursorToX(start);
        qreal x2 = line.cursorToX(end);
        if (x2 < x1)
            std::swap(x1, x2);
        if (to - at > lineEnd)
            x2 = std::min(x2, line.naturalTextRect().right());
        if (x2 - x1 < 0.5)
            continue;
        out.append({line, x1, x2});
    }
    return out;
}

} // namespace brulion::editor::detail
