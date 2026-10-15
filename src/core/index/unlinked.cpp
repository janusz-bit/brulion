#include "index/unlinked_p.h"

#include "index/textutil_p.h"
#include "markdown/inline.h"
#include "text/text.h"

#include <QSet>

#include <algorithm>

namespace brulion::index_detail {

using namespace Qt::StringLiterals;

namespace {

/// Shortest page name worth looking for (code points).
constexpr qsizetype MinNameLength = 2;

qsizetype codePointCount(QStringView s)
{
    qsizetype n = 0;
    for (qsizetype i = 0; i < s.size(); i += codePointAt(s, i).second)
        ++n;
    return n;
}

/// Replace [from, to) of `s` by spaces, keeping line breaks.
void blank(QString &s, qsizetype from, qsizetype to)
{
    from = std::clamp<qsizetype>(from, 0, s.size());
    to = std::clamp<qsizetype>(to, from, s.size());
    for (qsizetype i = from; i < to; ++i) {
        if (s[i] != u'\n' && s[i] != u'\r')
            s[i] = u' ';
    }
}

/// Blank the tags of a line so `#Dom` is not a mention of "Dom" — exactly the tags the parser
/// sees (markdown::findTags: `#tag`, `#nested/tag`, `#[[tag name]]`).
void maskTags(QString &line)
{
    for (const markdown::TagSpan &tag : markdown::findTags(line))
        blank(line, tag.from, tag.to);
}

/// Blank every character of `masked` that `mask` (same length) turned into a space.
void applyMask(QString &masked, qsizetype offset, QStringView original, QStringView mask)
{
    if (mask.size() != original.size())
        return; // defensive: a mask must keep offsets
    for (qsizetype i = 0; i < mask.size(); ++i) {
        if (mask[i] == u' ' && original[i] != u' ')
            blank(masked, offset + i, offset + i + 1);
    }
}

/// Content with code spans, comments and every link / URL replaced by spaces (same length).
/// Like the parser: `%%…%%` / `<!-- -->` comments are matched across the whole note (they may
/// span blocks), code spans only within one block — a stray backtick must not pair with one
/// blocks later and hide everything in between.
QString maskedContent(const QString &content, const QList<text::Line> &lines, const ParsedNote &note)
{
    QString masked = content;
    if (content.contains(u"%%") || content.contains(u"<!--")) {
        QString noBackticks = content;
        noBackticks.replace(u'`', u'\'');
        applyMask(masked, 0, noBackticks, markdown::maskInlineCode(noBackticks));
    }
    if (content.contains(u'`')) {
        for (const Block &b : note.blocks) {
            if (b.line < 0 || b.ownEndLine < b.line || b.ownEndLine >= lines.size())
                continue;
            const qsizetype from = lines[b.line].start;
            const qsizetype to = lines[b.ownEndLine].start + lines[b.ownEndLine].text.size();
            const QStringView own = QStringView(content).sliced(from, to - from);
            if (own.contains(u'`'))
                applyMask(masked, from, own, markdown::maskInlineCode(own));
        }
    }
    for (const Link &l : note.links)
        blank(masked, l.from, l.to);
    return masked;
}

/// Lines (0-based) of code, HTML and frontmatter blocks, which never contain mentions.
QSet<int> skippedLines(const ParsedNote &note)
{
    QSet<int> out;
    for (const Block &b : note.blocks) {
        if (b.kind != BlockKind::Code && b.kind != BlockKind::Html && b.kind != BlockKind::Frontmatter)
            continue;
        for (int l = b.line; l <= b.ownEndLine; ++l)
            out.insert(l);
    }
    return out;
}

struct Span {
    qsizetype start = -1;
    qsizetype end = -1;
};

/// Earliest whole-word match of any matcher in `line` (longer names win ties).
Span firstMatch(const QString &line, const QList<MentionMatcher> &matchers)
{
    Span best;
    for (const MentionMatcher &m : matchers) {
        qsizetype offset = 0;
        while (offset <= line.size()) {
            const QRegularExpressionMatch hit = m.re.match(line, offset);
            if (!hit.hasMatch())
                break;
            const qsizetype s = hit.capturedStart();
            const qsizetype e = hit.capturedEnd();
            if (best.start >= 0 && s >= best.start)
                break;
            if (e > s && text::isWholeWord(line, s, e)) {
                best = {s, e};
                break;
            }
            // Next code point: an offset inside a surrogate pair makes PCRE2 fail the match.
            offset = s + codePointAt(line, s).second;
        }
    }
    return best;
}

} // namespace

QList<MentionMatcher> mentionMatchers(const QStringList &names)
{
    QList<MentionMatcher> out;
    QSet<QString> seen;
    for (const QString &raw : names) {
        const QString name = nfc(raw).trimmed();
        if (codePointCount(name) < MinNameLength)
            continue;
        const QString lower = name.toLower();
        if (seen.contains(lower))
            continue;
        seen.insert(lower);
        // Unicode whitespace (also the no-break space common after Polish one-letter words).
        static const QRegularExpression space = text::unicodeRegex(u"\\s+"_s);
        const QStringList parts = name.split(space, Qt::SkipEmptyParts);
        QStringList escaped;
        for (const QString &p : parts)
            escaped.append(QRegularExpression::escape(p));
        MentionMatcher m;
        m.firstWord = parts.isEmpty() ? lower : parts.first().toLower();
        m.re = text::unicodeRegex(escaped.join(u"\\s+"_s), QRegularExpression::CaseInsensitiveOption);
        m.length = name.size();
        out.append(std::move(m));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const MentionMatcher &a, const MentionMatcher &b) { return a.length > b.length; });
    return out;
}

QList<Mention> findMentions(const QString &content, const ParsedNote &note, const QList<MentionMatcher> &matchers,
                            qsizetype max)
{
    QList<Mention> out;
    if (max <= 0 || matchers.isEmpty())
        return out;
    const QList<text::Line> lines = text::splitLines(content);
    const QString masked = maskedContent(content, lines, note);
    const QSet<int> skip = skippedLines(note);
    for (qsizetype i = 0; i < lines.size() && out.size() < max; ++i) {
        if (skip.contains(int(i)))
            continue;
        const text::Line &l = lines[i];
        QString text = masked.mid(l.start, l.text.size());
        maskTags(text);
        if (text.trimmed().isEmpty())
            continue;
        QString orig = l.text;
        const QString normalized = nfc(orig);
        if (normalized != orig) {
            // Decomposed text (e.g. from macOS): compare in NFC; offsets only feed the excerpt.
            orig = normalized;
            text = nfc(text);
        }
        const Span hit = firstMatch(text, matchers);
        if (hit.start >= 0)
            out.append(Mention{int(i), lineExcerpt(orig, hit.start, hit.end)});
    }
    return out;
}

} // namespace brulion::index_detail
