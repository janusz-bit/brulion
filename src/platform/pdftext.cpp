#include "pdftext.h"

#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

bool usable(const QRectF &box)
{
    return !box.isEmpty() && std::isfinite(box.x()) && std::isfinite(box.y()) && std::isfinite(box.width())
           && std::isfinite(box.height());
}

/// How far `v` lies outside [lo, hi] (0 inside).
qreal outside(qreal v, qreal lo, qreal hi)
{
    return v < lo ? lo - v : v > hi ? v - hi : 0;
}

bool isControl(QChar c)
{
    const QChar::Category cat = c.category();
    return cat == QChar::Other_Control || cat == QChar::Other_Format || c.unicode() == 0x2028 || c.unicode() == 0x2029;
}

} // namespace

qsizetype pdfNearestBox(const QList<QRectF> &boxes, QPointF point)
{
    if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
        return -1;
    qsizetype best = -1;
    qreal bestDy = std::numeric_limits<qreal>::infinity();
    qreal bestDx = std::numeric_limits<qreal>::infinity();
    for (qsizetype i = 0; i < boxes.size(); ++i) {
        const QRectF &box = boxes.at(i);
        if (!usable(box))
            continue;
        const qreal dy = outside(point.y(), box.top(), box.bottom());
        const qreal dx = outside(point.x(), box.left(), box.right());
        // The nearest line first (half a point apart counts as one line), on it the nearest character.
        if (dy < bestDy - 0.5 || (std::abs(dy - bestDy) <= 0.5 && dx < bestDx)) {
            best = i;
            bestDy = std::min(dy, bestDy);
            bestDx = dx;
            if (dy == 0 && dx == 0)
                break;
        }
    }
    return best;
}

qsizetype pdfCaretAt(const QList<QRectF> &boxes, QPointF point)
{
    const qsizetype best = pdfNearestBox(boxes, point);
    if (best < 0)
        return -1;
    // Above all the text: its start; below it: its end (a drag past the last line takes it all).
    qreal top = std::numeric_limits<qreal>::infinity();
    qreal bottom = -std::numeric_limits<qreal>::infinity();
    for (const QRectF &box : boxes) {
        if (usable(box)) {
            top = std::min(top, box.top());
            bottom = std::max(bottom, box.bottom());
        }
    }
    if (point.y() < top)
        return 0;
    if (point.y() > bottom)
        return boxes.size();
    const QRectF box = boxes.at(best);
    qsizetype caret = best;
    if (point.x() > box.center().x()) {
        // After the character — and after the units sharing its box (a surrogate pair, combining marks).
        caret = best + 1;
        while (caret < boxes.size() && boxes.at(caret) == box)
            ++caret;
    } else {
        while (caret > 0 && boxes.at(caret - 1) == box)
            --caret;
    }
    return caret;
}

PdfTextRange pdfRangeBetween(const QList<QRectF> &boxes, QPointF from, QPointF to)
{
    const qsizetype a = pdfCaretAt(boxes, from);
    const qsizetype b = pdfCaretAt(boxes, to);
    if (a < 0 || b < 0 || a == b)
        return {};
    return {std::min(a, b), std::abs(b - a)};
}

namespace {

bool wordUnit(QChar c)
{
    return c.isLetterOrNumber() || c.isMark() || c.isSurrogate() || c == u'_';
}

/// An apostrophe or a hyphen between two units of a word ("rock'n'roll", "biało-czerwony").
bool joiner(QStringView text, qsizetype i)
{
    const QChar c = text.at(i);
    const bool joins = c == u'\'' || c == u'-' || c.unicode() == 0x2019 || c.unicode() == 0x2010;
    return joins && i > 0 && i + 1 < text.size() && wordUnit(text.at(i - 1)) && wordUnit(text.at(i + 1));
}

} // namespace

PdfTextRange pdfWordAt(QStringView text, qsizetype index)
{
    if (index < 0 || index >= text.size())
        return {};
    if (!wordUnit(text.at(index)) && !joiner(text, index))
        return {index, 1};
    qsizetype start = index;
    while (start > 0 && (wordUnit(text.at(start - 1)) || joiner(text, start - 1)))
        --start;
    qsizetype end = index + 1;
    while (end < text.size() && (wordUnit(text.at(end)) || joiner(text, end)))
        ++end;
    return {start, end - start};
}

QString pdfCopyText(QStringView text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            out.append(c);
            out.append(text.at(++i));
            continue;
        }
        if (c.isSurrogate())
            continue;
        if (c == u'\r') {
            out.append(u'\n');
            if (i + 1 < text.size() && text.at(i + 1) == u'\n')
                ++i;
            continue;
        }
        if (c == u'\n' || c == u'\t') {
            out.append(c);
            continue;
        }
        if (c.unicode() == 0x2028 || c.unicode() == 0x2029) {
            out.append(u'\n');
            continue;
        }
        if (c.unicode() == 0x00AD || isControl(c))
            continue; // soft hyphens, U+0002 (pdfium's line-end hyphen), other controls
        out.append(c);
    }
    qsizetype end = out.size();
    while (end > 0 && out.at(end - 1).isSpace())
        --end;
    out.truncate(end);
    return out;
}

QString pdfCleanTitle(QStringView title)
{
    QString out;
    out.reserve(std::min<qsizetype>(title.size(), MaxPdfTitleLength + 1));
    bool space = false;
    for (qsizetype i = 0; i < title.size(); ++i) {
        const QChar c = title.at(i);
        const bool pair = c.isHighSurrogate() && i + 1 < title.size() && title.at(i + 1).isLowSurrogate();
        if (!pair && c.isSurrogate())
            continue;
        if (!pair && c.category() == QChar::Other_Control && !c.isSpace())
            continue; // NUL, BEL…
        if (!pair && (c.isSpace() || isControl(c))) {
            space = !out.isEmpty();
            continue;
        }
        if (space) {
            out.append(u' ');
            space = false;
        }
        out.append(c);
        if (pair)
            out.append(title.at(++i));
        if (out.size() > MaxPdfTitleLength)
            break;
    }
    if (out.size() > MaxPdfTitleLength) {
        qsizetype cut = MaxPdfTitleLength - 1;
        if (out.at(cut - 1).isHighSurrogate())
            --cut;
        out.truncate(cut);
        while (!out.isEmpty() && out.back().isSpace())
            out.chop(1);
        out.append(u'…');
    }
    return out;
}

bool pdfLinkUrlAllowed(QStringView url)
{
    if (url.isEmpty() || url.size() > MaxPdfUrlLength)
        return false;
    for (const QChar c : url) {
        if (c.unicode() <= 0x20 || c.unicode() == 0x7f || c.isSpace() || isControl(c) || c.isSurrogate())
            return false;
    }
    const QUrl parsed(url.toString(), QUrl::StrictMode);
    if (!parsed.isValid())
        return false;
    const QString scheme = parsed.scheme().toLower();
    if (scheme == u"http"_s || scheme == u"https"_s)
        return !parsed.host().isEmpty();
    if (scheme == u"mailto"_s) {
        const QString address = parsed.path();
        return address.contains(u'@') && !address.startsWith(u'@') && !address.endsWith(u'@');
    }
    return false;
}

} // namespace brulion::platform
