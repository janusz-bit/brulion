#include "media/transcript.h"

#include "markdown/blockdocument.h"
#include "markdown/inline.h"
#include "util/paths.h"
#include "util/unicode.h"

#include <QUrl>

#include <cstdlib>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::media {

namespace {

/// The start of a line that would make it more than a paragraph — a heading, a list item, a
/// quote, a table row, a fence, a rule, a display formula, a container: where a backslash keeps it
/// a paragraph (before the character that makes it one); -1 none.
qsizetype blockMarker(QStringView s)
{
    if (s.isEmpty())
        return -1;
    const QChar c = s.front();
    if (c == u'#' || c == u'>' || c == u'|' || c == u'-' || c == u'+' || c == u'*' || c == u'=' || c == u'_'
        || c == u'`' || c == u'~' || c == u'$' || c == u':')
        return 0;
    // "1." / "1)" — an ordered list: "1\.".
    qsizetype digits = 0;
    while (digits < s.size() && digits < 10 && s[digits] >= u'0' && s[digits] <= u'9')
        ++digits;
    if (digits > 0 && digits < s.size() && (s[digits] == u'.' || s[digits] == u')'))
        return digits;
    return -1;
}

/// The written target with the decoding a link may have used: "%20" as a space, NFC.
QString decoded(QStringView target)
{
    return unicode::nfc(QUrl::fromPercentEncoding(target.toString().toUtf8()));
}

/// How well `link` names `target`: 2 exactly as written, 1 the same after decoding or by the
/// file's name alone, 0 not at all.
int matchOf(const QString &link, QStringView target)
{
    if (link == target)
        return 2;
    const QString a = decoded(link);
    const QString b = decoded(target);
    if (a == b)
        return 1;
    const QString an = paths::basename(paths::normalize(a));
    return !an.isEmpty() && an.compare(paths::basename(paths::normalize(b)), Qt::CaseInsensitive) == 0 ? 1 : 0;
}

/// How well block `body` embeds `target` (matchOf() of its best embed).
int embedMatch(const QString &body, QStringView target)
{
    if (!body.contains(u'!'))
        return 0;
    int best = 0;
    const markdown::InlineScan scan = markdown::extractInline(body);
    for (const Link &link : scan.links) {
        const bool embed = link.kind == LinkKind::Embed
                           || (link.kind == LinkKind::Markdown && link.from >= 0 && link.from < body.size()
                               && body[link.from] == u'!');
        if (embed)
            best = std::max(best, matchOf(link.target, target));
    }
    return best;
}

} // namespace

QString transcriptText(const QStringList &segments)
{
    QString joined;
    for (const QString &segment : segments) {
        for (const QChar c : segment) {
            const QChar::Category cat = c.category();
            if (c.isSpace() || cat == QChar::Other_Control) {
                if (!joined.isEmpty() && !joined.endsWith(u' '))
                    joined += u' ';
            } else if (cat != QChar::Other_Format || c == QChar(0x200D)) { // (a ZWJ joins an emoji)
                joined += c;
            }
        }
        if (!joined.isEmpty() && !joined.endsWith(u' '))
            joined += u' ';
    }
    joined = joined.trimmed();
    if (joined.isEmpty())
        return {};
    // Inside: no link, embed, block reference, template or tag out of words heard.
    QString out;
    out.reserve(joined.size() + 8);
    for (qsizetype i = 0; i < joined.size(); ++i) {
        const QChar c = joined[i];
        const QChar next = i + 1 < joined.size() ? joined[i + 1] : QChar();
        if (((c == u'[' || c == u'(' || c == u'{') && next == c) || (c == u'#' && next.isLetter()) || c == u'\\')
            out += u'\\';
        out += c;
    }
    // At the start: a plain paragraph, not a heading, a list, a quote… (`#` before a letter is
    // escaped already).
    const qsizetype marker = out.startsWith(u'\\') ? -1 : blockMarker(out);
    if (marker >= 0)
        out.insert(marker, u'\\');
    return out;
}

TranscriptPlace placeTranscript(const QString &content, QStringView target, int nearLine, const QString &text)
{
    if (text.trimmed().isEmpty() || target.trimmed().isEmpty())
        return {};
    markdown::BlockDocument doc(content);
    int found = -1;
    int foundMatch = 0;
    int foundDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &e = doc.at(i);
        const int match = embedMatch(e.body, target);
        if (match == 0)
            continue;
        const int distance = nearLine < 0 ? 0 : std::abs(e.line - nearLine);
        // The nearest block; at the same distance the one that names it as written.
        if (found < 0 || distance < foundDistance || (distance == foundDistance && match > foundMatch)) {
            found = i;
            foundMatch = match;
            foundDistance = distance;
        }
    }
    if (found < 0)
        return {};
    const markdown::BlockEntry &block = doc.at(found);
    const bool listItem = !block.prefix.isEmpty();
    const markdown::EditResult edit =
            listItem ? doc.insertAfter(found, text, block.depth + 1, true) : doc.insertAfter(found, text, 0, false);
    if (!edit.changed() || edit.focus < 0)
        return {};
    return TranscriptPlace{doc.toString(), doc.at(edit.focus).line};
}

} // namespace brulion::media
