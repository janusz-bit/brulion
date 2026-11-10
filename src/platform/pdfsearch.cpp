#include "pdfsearch.h"

#include "text/text.h"

#include <algorithm>
#include <cmath>

namespace brulion::platform {

namespace {

/// A unit the search ignores: soft hyphens and pdfium's line-end hyphen marker.
bool ignored(QChar c)
{
    return c.unicode() == 0x00AD || c.unicode() == 0x0002;
}

bool space(QChar c)
{
    return c.isSpace() || c.unicode() == 0x2028 || c.unicode() == 0x2029;
}

/// `folded` with runs of white space as one ' ' and ignored units dropped; `map` (optional): for
/// each unit of the result, its index in `folded`.
QString squeeze(QStringView folded, QList<qsizetype> *map)
{
    QString out;
    out.reserve(folded.size());
    if (map)
        map->reserve(folded.size());
    bool inSpace = false;
    for (qsizetype i = 0; i < folded.size(); ++i) {
        const QChar c = folded.at(i);
        if (ignored(c))
            continue;
        if (space(c)) {
            if (inSpace)
                continue;
            inSpace = true;
            out.append(u' ');
        } else {
            inSpace = false;
            out.append(c);
        }
        if (map)
            map->append(i);
    }
    return out;
}

} // namespace

QList<PdfTextRange> findPdfText(QStringView text, QStringView query, int maxHits)
{
    QList<PdfTextRange> out;
    if (maxHits <= 0 || text.isEmpty())
        return out;
    const QString needle = squeeze(text::foldKey(query), nullptr).trimmed();
    if (needle.isEmpty())
        return out;
    const text::FoldedText folded = text::foldKeyWithMap(text);
    QList<qsizetype> map;
    const QString haystack = squeeze(folded.text, &map);
    for (qsizetype at = haystack.indexOf(needle); at >= 0 && out.size() < maxHits;
         at = haystack.indexOf(needle, at + needle.size())) {
        const qsizetype first = map.at(at);
        const qsizetype last = map.at(at + needle.size() - 1) + 1; // the folded unit after the match
        const qsizetype start = folded.toOriginal(first);
        // The original character after the match's last one: past its combining marks, never inside
        // a surrogate pair (both of a pair's units come from one code point).
        qsizetype end = folded.toOriginal(last);
        if (end <= start)
            end = std::min<qsizetype>(text.size(), start + 1);
        out.append({start, end - start});
    }
    return out;
}

bool pdfUsableBox(const QRectF &box)
{
    return !box.isEmpty() && std::isfinite(box.x()) && std::isfinite(box.y()) && std::isfinite(box.width())
           && std::isfinite(box.height());
}

QList<QRectF> pdfRangeRects(const QList<QRectF> &boxes, PdfTextRange range)
{
    QList<QRectF> out;
    const qsizetype from = std::clamp<qsizetype>(range.start, 0, boxes.size());
    const qsizetype to = std::clamp<qsizetype>(range.start + std::max<qsizetype>(range.length, 0), from, boxes.size());
    QRectF line;
    for (qsizetype i = from; i < to; ++i) {
        const QRectF box = boxes.at(i);
        if (!pdfUsableBox(box))
            continue;
        if (line.isNull()) {
            line = box;
            continue;
        }
        const qreal limit = std::max(line.height(), box.height()) / 2;
        if (std::abs(line.center().y() - box.center().y()) <= limit) {
            line = line.united(box);
        } else {
            out.append(line);
            line = box;
        }
    }
    if (!line.isNull())
        out.append(line);
    return out;
}

QStringList pdfMatchSpellings(QStringView text, const QList<PdfTextRange> &ranges, int max)
{
    QStringList out;
    for (const PdfTextRange &range : ranges) {
        if (out.size() >= max)
            break;
        if (range.start < 0 || range.length <= 0 || range.start + range.length > text.size())
            continue;
        const QString spelling = text.sliced(range.start, range.length).toString();
        if (!out.contains(spelling))
            out.append(spelling);
    }
    return out;
}

} // namespace brulion::platform
