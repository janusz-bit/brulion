#include "spell/spellwords.h"

#include "markdown/inline.h"
#include "markdown/math.h"
#include "markdown/tokens.h"
#include "types.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::spell {

namespace {

void mask(QString &s, qsizetype from, qsizetype to)
{
    from = std::clamp<qsizetype>(from, 0, s.size());
    to = std::clamp<qsizetype>(to, from, s.size());
    for (qsizetype i = from; i < to; ++i) {
        if (s[i] != u'\n')
            s[i] = u' ';
    }
}

void maskMatches(QString &s, const QRegularExpression &re)
{
    for (auto it = re.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        mask(s, m.capturedStart(), m.capturedEnd());
    }
}

/// A Markdown link `[text](dest)` / `![alt](dest)` at [from, to) of `s`: everything but its
/// text masked.
void maskMarkdownLink(QString &s, qsizetype from, qsizetype to)
{
    const QStringView source = QStringView(s).sliced(from, to - from);
    const markdown::MarkdownDestination dest = markdown::markdownDestination(source);
    const qsizetype open = source.startsWith(u'!') ? 2 : 1;
    if (dest.from >= 0) {
        const qsizetype paren = dest.from - (dest.angle ? 1 : 0) - 1;
        if (paren > open && source[paren] == u'(' && source[paren - 1] == u']') {
            mask(s, from, from + open);
            mask(s, from + paren - 1, to);
            return;
        }
    }
    mask(s, from, to);
}

/// The syntax of `text` replaced by spaces (same length): what is left is prose.
QString prose(QStringView text, bool lineStart)
{
    QString s = markdown::maskInlineCode(text);
    for (const markdown::MathSpan &span : markdown::findMathSpans(s))
        mask(s, span.from, span.to);

    for (const Link &link : markdown::extractInline(s).links) {
        // A Markdown link or picture (`[text](…)`, `![alt](…)`): its text is prose.
        const QStringView source = QStringView(s).sliced(link.from, link.to - link.from);
        const bool bracket = source.startsWith(u"[") || source.startsWith(u"![");
        if ((link.kind == LinkKind::Markdown || link.kind == LinkKind::Embed) && bracket && !source.startsWith(u"[[")
            && !source.startsWith(u"![["))
            maskMarkdownLink(s, link.from, link.to);
        else
            mask(s, link.from, link.to);
    }
    for (const markdown::TagSpan &tag : markdown::findTags(s))
        mask(s, tag.from, tag.to);
    for (const markdown::Token &token : markdown::findTokens(s))
        mask(s, token.from, token.to);
    if (const auto id = markdown::blockIdAtEnd(s))
        mask(s, id->from, id->to);

    // HTML tags and entities, Logseq macros and timestamps, footnote markers.
    static const QRegularExpression htmlTagRe(u"</?[A-Za-z][A-Za-z0-9-]*(?:\\s[^<>\\n]*)?/?>"_s);
    static const QRegularExpression entityRe(u"&(?:[A-Za-z][A-Za-z0-9]{1,31}|#[0-9]{1,7}|#[xX][0-9A-Fa-f]{1,6});"_s);
    static const QRegularExpression macroRe(u"\\{\\{.*?\\}\\}"_s);
    static const QRegularExpression timestampRe(u"<\\d{4}-\\d{2}-\\d{2}[^<>\\n]*>"_s);
    static const QRegularExpression footnoteRe(u"\\[\\^[^\\]\\s]+\\]"_s);
    maskMatches(s, htmlTagRe);
    maskMatches(s, entityRe);
    maskMatches(s, macroRe);
    maskMatches(s, timestampRe);
    maskMatches(s, footnoteRe);

    if (lineStart) {
        // Quote and list markers, a checkbox, then a task keyword, a callout type or a property.
        static const QRegularExpression leadRe(
                u"^\\s*(?:>\\s?)*\\s*(?:(?:[-*+]|\\d{1,9}[.)])\\s+)?(?:\\[[^\\]\\n]\\]\\s+)?"_s);
        static const QRegularExpression keywordRe(
                u"^(?:TODO|DOING|DONE|LATER|NOW|WAITING|CANCELED|CANCELLED|IN-PROGRESS)(?=\\s|$)"_s);
        static const QRegularExpression calloutRe(u"^\\[![A-Za-z0-9_-]+\\][+-]?"_s);
        static const QRegularExpression propertyRe(u"^[A-Za-z0-9_-]+::"_s);
        const qsizetype lead = leadRe.matchView(s).capturedLength();
        const QStringView rest = QStringView(s).sliced(lead);
        for (const QRegularExpression *re : {&keywordRe, &calloutRe, &propertyRe}) {
            if (const auto m = re->matchView(rest); m.hasMatch()) {
                mask(s, lead, lead + m.capturedLength());
                break;
            }
        }
    }
    return s;
}

/// The code point at `i` of `s` and its length in UTF-16 units.
char32_t codePointAt(QStringView s, qsizetype i, int *units)
{
    const QChar c = s[i];
    if (c.isHighSurrogate() && i + 1 < s.size() && s[i + 1].isLowSurrogate()) {
        *units = 2;
        return QChar::surrogateToUcs4(c, s[i + 1]);
    }
    *units = 1;
    return c.unicode();
}

bool isMark(char32_t c)
{
    const auto cat = QChar::category(c);
    return cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining || cat == QChar::Mark_Enclosing;
}

bool isLetterOrMark(char32_t c)
{
    return QChar::isLetter(c) || isMark(c);
}

/// A character a word is made of: a letter, a mark or a digit (a digit makes a run that is not
/// checked).
bool isCore(char32_t c)
{
    return isLetterOrMark(c) || QChar::isNumber(c);
}

bool isApostrophe(char32_t c)
{
    return c == U'\'' || c == U'\u2019' || c == U'\u02BC';
}

bool isHyphen(char32_t c)
{
    return c == U'-' || c == U'\u2010' || c == U'\u2011';
}

/// May join two runs of word characters into one run ('_', '.' and '@' make a run that is not
/// checked: snake_case, file.md, jan@example.com; '_' around a word is emphasis: `_też_`).
bool isJoiner(char32_t c)
{
    return isApostrophe(c) || isHyphen(c) || c == U'\u00AD' || c == U'.' || c == U'@' || c == U'_';
}

/// A path, a LaTeX command or a mention next to the run.
bool isAdjacentSyntax(QChar c)
{
    return c == u'/' || c == u'\\' || c == u'@';
}

/// Whether the run [from, to) of `s` is a word to check (see the header).
bool checkable(QStringView s, qsizetype from, qsizetype to)
{
    int letters = 0;
    int upper = 0;
    int lower = 0;
    bool previousLower = false;
    for (qsizetype i = from; i < to;) {
        int units = 1;
        const char32_t c = codePointAt(s, i, &units);
        i += units;
        if (QChar::isNumber(c) || c == U'_' || c == U'.' || c == U'@')
            return false;
        if (isJoiner(c)) {
            previousLower = false;
            continue;
        }
        if (!QChar::isLetter(c))
            continue; // a combining mark
        ++letters;
        if (QChar::isUpper(c) || QChar::isTitleCase(c)) {
            ++upper;
            if (previousLower)
                return false; // iPhone, QtQuick
            previousLower = false;
        } else if (QChar::isLower(c)) {
            ++lower;
            previousLower = true;
        } else {
            previousLower = false;
        }
    }
    if (letters < 2)
        return false;
    if (upper >= 2 && lower == 0)
        return false; // NASA, PDF
    return true;
}

} // namespace

QList<WordSpan> findWords(QStringView text, bool lineStart)
{
    QList<WordSpan> words;
    if (text.isEmpty())
        return words;
    const QString s = prose(text, lineStart);
    const QStringView v(s);
    const qsizetype n = v.size();
    qsizetype i = 0;
    while (i < n) {
        int units = 1;
        const char32_t c = codePointAt(v, i, &units);
        if (!isCore(c)) {
            i += units;
            continue;
        }
        const qsizetype from = i;
        i += units;
        while (i < n) {
            int u1 = 1;
            const char32_t next = codePointAt(v, i, &u1);
            if (isCore(next)) {
                i += u1;
                continue;
            }
            if (isJoiner(next) && i + u1 < n) {
                int u2 = 1;
                if (isCore(codePointAt(v, i + u1, &u2))) {
                    i += u1 + u2;
                    continue;
                }
            }
            break;
        }
        const qsizetype to = i;
        if ((from > 0 && isAdjacentSyntax(v[from - 1])) || (to < n && isAdjacentSyntax(v[to])))
            continue;
        if (!checkable(v, from, to))
            continue;
        words.append(WordSpan{from, to, to < n && v[to] == u'.'});
    }
    return words;
}

std::optional<WordSpan> wordAt(QStringView text, qsizetype position, bool lineStart)
{
    for (const WordSpan &w : findWords(text, lineStart)) {
        if (w.from > position)
            break;
        if (position <= w.to)
            return w;
    }
    return std::nullopt;
}

QString normalizedWord(QStringView word)
{
    QString w;
    w.reserve(word.size());
    for (const QChar c : word) {
        if (c == u'\u00AD')
            continue;
        if (c == u'\u2019' || c == u'\u02BC')
            w.append(u'\'');
        else if (c == u'\u2010' || c == u'\u2011')
            w.append(u'-');
        else
            w.append(c);
    }
    return w.normalized(QString::NormalizationForm_C);
}

QString checkedText(QStringView text, const WordSpan &span)
{
    if (span.from < 0 || span.to > text.size() || span.from >= span.to)
        return {};
    QString w = normalizedWord(text.sliced(span.from, span.to - span.from));
    if (span.dot)
        w.append(u'.');
    return w;
}

QStringList hyphenParts(QStringView word)
{
    const QString w = normalizedWord(word);
    if (!w.contains(u'-'))
        return {};
    QStringList parts = w.split(u'-', Qt::SkipEmptyParts);
    return parts;
}

} // namespace brulion::spell
