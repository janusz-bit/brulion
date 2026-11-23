#include "spellmarks.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// How the renderer writes `c` of verbatim text (QString::toHtmlEscaped); "" for a line break,
/// which is a piece of its own (`<br/>`).
QStringView escaped(const QChar &c)
{
    switch (c.unicode()) {
    case u'&': return u"&amp;";
    case u'<': return u"&lt;";
    case u'>': return u"&gt;";
    case u'"': return u"&quot;";
    case u'\n':
    case u'\r': return {};
    default: return QStringView(&c, 1);
    }
}

/// `anchor` shows its source verbatim: its HTML is the source escaped (a space may be a no-break
/// space: Polish typography keeps "w domu" together).
bool verbatim(QStringView html, const markdown::HtmlSourceAnchor &anchor, QStringView source)
{
    if (anchor.sourceFrom < 0 || anchor.sourceTo > source.size() || anchor.sourceTo <= anchor.sourceFrom
        || anchor.htmlFrom < 0 || anchor.htmlTo > html.size() || anchor.htmlTo <= anchor.htmlFrom)
        return false;
    qsizetype h = anchor.htmlFrom;
    for (qsizetype i = anchor.sourceFrom; i < anchor.sourceTo; ++i) {
        const QChar c = source[i];
        if (c == u' ' && h < anchor.htmlTo && (html[h] == u' ' || html[h] == QChar(0x00A0))) {
            ++h;
            continue;
        }
        const QStringView e = escaped(c);
        if (e.isEmpty() || h + e.size() > anchor.htmlTo || html.sliced(h, e.size()) != e)
            return false;
        h += e.size();
    }
    return h == anchor.htmlTo;
}

/// The length of source [from, to) as verbatim HTML.
qsizetype escapedLength(QStringView source, qsizetype from, qsizetype to)
{
    qsizetype n = 0;
    for (qsizetype i = from; i < to; ++i)
        n += std::max<qsizetype>(1, escaped(source[i]).size());
    return n;
}

} // namespace

QString spellMarkStyle(const QColor &color)
{
    return u"text-decoration:underline;text-decoration-color:"_s + color.name(QColor::HexRgb);
}

QString markMisspelledHtml(const QString &html, const QList<markdown::HtmlSourceAnchor> &anchors, QStringView source,
                           const QList<spell::WordSpan> &wrong, const QColor &color)
{
    if (wrong.isEmpty() || anchors.isEmpty())
        return html;
    // The HTML ranges to wrap, from every verbatim anchor a word overlaps.
    QList<std::pair<qsizetype, qsizetype>> ranges;
    for (const markdown::HtmlSourceAnchor &a : anchors) {
        bool checked = false;
        bool isVerbatim = false;
        for (const spell::WordSpan &w : wrong) {
            const qsizetype from = std::max(w.from, a.sourceFrom);
            const qsizetype to = std::min(w.to, a.sourceTo);
            if (from >= to)
                continue;
            if (!checked) {
                isVerbatim = verbatim(html, a, source);
                checked = true;
            }
            if (!isVerbatim)
                break;
            const qsizetype start = a.htmlFrom + escapedLength(source, a.sourceFrom, from);
            ranges.append({start, start + escapedLength(source, from, to)});
        }
    }
    if (ranges.isEmpty())
        return html;
    std::ranges::sort(ranges);
    const QString open = u"<span style=\""_s + spellMarkStyle(color) + u"\">"_s;
    const QString close = u"</span>"_s;
    QString out;
    out.reserve(html.size() + ranges.size() * (open.size() + close.size()));
    qsizetype done = 0;
    for (const auto &[from, to] : std::as_const(ranges)) {
        if (from < done || to > html.size())
            continue; // (overlapping anchors: never from the renderer)
        out += QStringView(html).sliced(done, from - done);
        out += open;
        out += QStringView(html).sliced(from, to - from);
        out += close;
        done = to;
    }
    out += QStringView(html).sliced(done);
    return out;
}

} // namespace brulion::editor
