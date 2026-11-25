#include "media/transcript.h"

#include "media/media.h"

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

/// How well block `body` embeds `target`: matchOf() of its best embed, and how that embed names it.
struct EmbedMatch {
    int match = 0;
    MomentLinks links;
};

EmbedMatch embedMatch(const QString &body, QStringView target)
{
    EmbedMatch best;
    if (!body.contains(u'!'))
        return best;
    const markdown::InlineScan scan = markdown::extractInline(body);
    for (const Link &link : scan.links) {
        const bool markdown =
                link.kind == LinkKind::Markdown && link.from >= 0 && link.from < body.size() && body[link.from] == u'!';
        if (link.kind != LinkKind::Embed && !markdown)
            continue;
        const int match = matchOf(link.target, target);
        if (match > best.match) {
            best.match = match;
            best.links.wiki = !markdown;
            best.links.target = link.target;
        }
    }
    return best;
}

/// The block of `doc` that embeds `target` nearest to `nearLine` (-1: none), and how it names it.
int embeddingBlock(const markdown::BlockDocument &doc, QStringView target, int nearLine, MomentLinks *links)
{
    int found = -1;
    int foundMatch = 0;
    int foundDistance = std::numeric_limits<int>::max();
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &e = doc.at(i);
        const EmbedMatch m = embedMatch(e.body, target);
        if (m.match == 0)
            continue;
        const int distance = nearLine < 0 ? 0 : std::abs(e.line - nearLine);
        // The nearest block; at the same distance the one that names it as written.
        if (found < 0 || distance < foundDistance || (distance == foundDistance && m.match > foundMatch)) {
            found = i;
            foundMatch = m.match;
            foundDistance = distance;
            if (links)
                *links = m.links;
        }
    }
    return found;
}

/// `blocks` right under block `found` of `doc`, in order: a list item's first children, else
/// paragraphs after it. The line of the first one (-1: nothing inserted).
int insertBlocks(markdown::BlockDocument &doc, int found, const QStringList &blocks)
{
    const markdown::BlockEntry &block = doc.at(found);
    const bool listItem = !block.prefix.isEmpty();
    const int depth = block.depth + 1;
    int first = -1;
    int after = found;
    for (const QString &text : blocks) {
        const markdown::EditResult edit =
                listItem ? doc.insertAfter(after, text, depth, true) : doc.insertAfter(after, text, 0, false);
        if (!edit.changed() || edit.focus < 0)
            return -1;
        if (first < 0)
            first = edit.focus;
        after = edit.focus;
    }
    return first < 0 ? -1 : doc.at(first).line;
}

/// The characters a Markdown link's destination cannot hold as they are, percent-encoded: white
/// space, brackets, `<`, `>`, `#`, `%`, `\`, control characters. Letters stay (Obsidian writes
/// `%20` for spaces alone).
QString markdownDestination(const QString &target)
{
    QString out;
    out.reserve(target.size() + 8);
    for (const QChar c : target) {
        const char16_t u = c.unicode();
        const bool encode = u <= 0x20 || u == 0x7F || u == u'(' || u == u')' || u == u'[' || u == u']' || u == u'<'
                            || u == u'>' || u == u'#' || u == u'%' || u == u'\\';
        if (encode && u < 0x80)
            out += u'%' + QString::number(u, 16).toUpper().rightJustified(2, u'0');
        else
            out += c;
    }
    return out;
}

/// The characters a sentence may end with before its last `.`, `!`, `?`: closing quotes and
/// brackets.
bool isClosing(QChar c)
{
    return c == u'"' || c == u'\'' || c == u')' || c == u']' || c == u'}' || c == QChar(0x201D) || c == QChar(0x2019)
           || c == QChar(0x00BB) || c == QChar(0x201C);
}

/// `a` and `b` joined by one space.
QString joined(const QString &a, const QString &b)
{
    if (a.isEmpty())
        return b;
    if (b.isEmpty())
        return a;
    return a + u' ' + b;
}

} // namespace

bool endsSentence(QStringView text)
{
    qsizetype i = text.size();
    while (i > 0 && (text[i - 1].isSpace() || isClosing(text[i - 1])))
        --i;
    if (i == 0)
        return false;
    const QChar c = text[i - 1];
    return c == u'.' || c == u'!' || c == u'?' || c == QChar(0x2026) || c == QChar(0x3002) || c == QChar(0xFF01)
           || c == QChar(0xFF1F) || c == QChar(0xFF0E);
}

QList<TimedText> transcriptSentences(const QList<TimedText> &segments, qint64 longestMs)
{
    QList<TimedText> out;
    TimedText current;
    bool open = false;
    for (const TimedText &s : segments) {
        const QString text = s.text.simplified();
        if (text.isEmpty())
            continue;
        if (!open) {
            current = TimedText{s.startMs, s.endMs, text};
            open = true;
        } else {
            current.text = joined(current.text, text);
            current.endMs = std::max(current.endMs, s.endMs);
        }
        if (endsSentence(current.text) || current.endMs - current.startMs >= longestMs) {
            out.append(current);
            open = false;
        }
    }
    if (open)
        out.append(current);
    return out;
}

QList<TimedText> transcriptParagraphs(const QList<TimedText> &segments, const ParagraphRules &rules)
{
    QList<TimedText> out;
    TimedText current;
    bool open = false;
    for (const TimedText &sentence : transcriptSentences(segments, rules.longestMs)) {
        if (open) {
            const bool whole = endsSentence(current.text);
            const bool pause = sentence.startMs - current.endMs >= rules.pauseMs;
            const bool longer =
                    sentence.startMs - current.startMs >= rules.lengthMs || current.text.size() >= rules.lengthChars;
            const bool longest = sentence.startMs - current.startMs >= rules.longestMs;
            if ((whole && (pause || longer)) || longest) {
                out.append(current);
                open = false;
            }
        }
        if (!open) {
            current = sentence;
            open = true;
        } else {
            current.text = joined(current.text, sentence.text);
            current.endMs = std::max(current.endMs, sentence.endMs);
        }
    }
    if (open)
        out.append(current);
    return out;
}

QString momentLink(const MomentLinks &links, qint64 ms)
{
    if (links.target.trimmed().isEmpty())
        return {};
    const qint64 at = std::max<qint64>(0, ms) / 1000 * 1000;
    const QString fragment = timeFragment(TimeRange{at, -1});
    const QString time = formatTime(at, links.durationMs);
    if (links.wiki)
        return u"[["_s + links.target + u'#' + fragment + u'|' + time + u"]]"_s;
    return u'[' + time + u"]("_s + markdownDestination(links.target) + u'#' + fragment + u')';
}

QStringList transcriptBlocks(const QList<TimedText> &paragraphs, const MomentLinks &links)
{
    QList<TimedText> spoken;
    for (const TimedText &p : paragraphs) {
        if (!transcriptText({p.text}).isEmpty())
            spoken.append(p);
    }
    QStringList out;
    if (spoken.size() == 1) {
        out.append(transcriptText({spoken.first().text}));
        return out;
    }
    for (const TimedText &p : std::as_const(spoken)) {
        const QString link = momentLink(links, p.startMs);
        const QString text = transcriptText({p.text});
        out.append(link.isEmpty() ? text : link + u' ' + text);
    }
    return out;
}

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
    const int found = embeddingBlock(doc, target, nearLine, nullptr);
    if (found < 0)
        return {};
    const int line = insertBlocks(doc, found, {text});
    return line < 0 ? TranscriptPlace{} : TranscriptPlace{doc.toString(), line};
}

TranscriptPlace placeTranscript(const QString &content, QStringView target, int nearLine,
                                const QList<TimedText> &paragraphs, qint64 durationMs)
{
    if (target.trimmed().isEmpty())
        return {};
    markdown::BlockDocument doc(content);
    MomentLinks links;
    const int found = embeddingBlock(doc, target, nearLine, &links);
    if (found < 0)
        return {};
    links.durationMs = durationMs >= 0 ? durationMs : (paragraphs.isEmpty() ? -1 : paragraphs.last().endMs);
    const QStringList blocks = transcriptBlocks(paragraphs, links);
    if (blocks.isEmpty())
        return {};
    const int line = insertBlocks(doc, found, blocks);
    return line < 0 ? TranscriptPlace{} : TranscriptPlace{doc.toString(), line};
}

} // namespace brulion::media
