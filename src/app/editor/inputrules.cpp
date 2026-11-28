#include "inputrules.h"

#include "markdown/frontmatter.h"
#include "markdown/inline.h"
#include "markdown/math.h"

#include <QRegularExpression>
#include <QStringView>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// A character that is part of a word: a letter, a digit, a combining mark, `_`.
bool isWordChar(char32_t c)
{
    if (c == U'_' || QChar::isLetterOrNumber(c))
        return true;
    const QChar::Category category = QChar::category(c);
    return category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
           || category == QChar::Mark_Enclosing;
}

/// The code point that ends right before `at` and its length in UTF-16 units (0 at the start).
char32_t codePointBefore(const QString &text, int at, int *length)
{
    *length = 0;
    if (at <= 0 || at > text.size())
        return 0;
    const QChar last = text[at - 1];
    if (last.isLowSurrogate() && at >= 2 && text[at - 2].isHighSurrogate()) {
        *length = 2;
        return QChar::surrogateToUcs4(text[at - 2], last);
    }
    *length = 1;
    return last.unicode();
}

/// A fence line (``` or ~~~, at least three, after up to three spaces): its marker and length.
bool fenceOf(QStringView line, QChar *marker, int *length)
{
    int i = 0;
    while (i < line.size() && i < 3 && line[i] == u' ')
        ++i;
    if (i >= line.size() || (line[i] != u'`' && line[i] != u'~'))
        return false;
    const QChar c = line[i];
    int n = 0;
    while (i + n < line.size() && line[i + n] == c)
        ++n;
    if (n < 3)
        return false;
    *marker = c;
    *length = n;
    return true;
}

bool closesFence(QStringView line, QChar marker, int length)
{
    QChar m;
    int n = 0;
    return fenceOf(line, &m, &n) && m == marker && n >= length && line.trimmed().size() == n;
}

/// A line that starts a Markdown block of its own (a list item, a heading, a quote): source mode
/// has them one under another, a code span or a comment of the line above ends before it.
bool startsBlock(QStringView line)
{
    static const QRegularExpression block(u"^[ \\t]*(?:(?:[-*+]|\\d{1,9}[.)])(?:[ \\t]|$)|#{1,6}(?:[ \\t]|$)|>)"_s);
    return block.matchView(line).hasMatch();
}

/// Where a whole-word match of `length` characters would end: before the character typed last,
/// which has to end a word (and be all of the typing past the match); nullopt otherwise.
std::optional<int> wordEnd(const QString &text, int cursor, int typed, int length)
{
    int boundary = 0;
    const char32_t last = codePointBefore(text, cursor, &boundary);
    if (boundary == 0 || isWordChar(last) || typed > length + boundary)
        return std::nullopt;
    return cursor - boundary;
}

/// No letter or digit right before `from`: a word starts there.
bool startsWord(const QString &text, int from)
{
    int before = 0;
    const char32_t c = codePointBefore(text, from, &before);
    return before == 0 || !isWordChar(c);
}

/// `rule` matching the text typed before `cursor` (`typed` characters), or nullopt.
std::optional<InputRuleHit> ruleMatch(const QString &text, int cursor, int typed, const InputRule &rule)
{
    const int m = int(rule.match.size());
    if (m == 0)
        return std::nullopt;
    const std::optional<int> end =
            rule.word ? wordEnd(text, cursor, typed, m) : (typed > m ? std::nullopt : std::optional<int>(cursor));
    if (!end || *end < m || QStringView(text).mid(*end - m, m) != rule.match)
        return std::nullopt;
    if (rule.word && !startsWord(text, *end - m))
        return std::nullopt;
    return InputRuleHit{*end - m, *end, rule.replace};
}

// ---- where nothing is replaced (inputRuleProtected) --------------------------------------------

/// The block-level state of the lines read so far: inside fenced code (its fence) or a `$$` formula.
struct Lines {
    bool inFence = false;
    bool inDisplay = false;
    QChar fenceMarker;
    int fenceLength = 0;
};

/// A line before the caret's (it starts at `start`, the next one at `next`): the state after it;
/// `paragraph` moves to where a paragraph starts — after a blank line or a closing line, at a line
/// that starts a block of its own.
void stepLine(Lines &s, QStringView line, qsizetype start, qsizetype next, qsizetype *paragraph)
{
    if (s.inFence) {
        if (closesFence(line, s.fenceMarker, s.fenceLength)) {
            s.inFence = false;
            *paragraph = next;
        }
        return;
    }
    if (s.inDisplay) {
        if (line.trimmed() == u"$$") {
            s.inDisplay = false;
            *paragraph = next;
        }
        return;
    }
    QChar marker;
    int length = 0;
    if (fenceOf(line, &marker, &length)) {
        s = Lines{true, false, marker, length};
    } else if (line.trimmed() == u"$$") {
        s.inDisplay = true;
    } else if (line.trimmed().isEmpty()) {
        *paragraph = next;
    } else if (startsBlock(line)) {
        *paragraph = start; // a list item, a heading or a quote: no code span reaches into it
    }
}

/// The caret's line in the state `s`: nullopt inside fenced code or a `$$` formula (or on a fence's
/// own line: its info string), else where its paragraph starts.
std::optional<qsizetype> caretLine(const Lines &s, QStringView line, qsizetype start, qsizetype paragraph)
{
    QChar marker;
    int length = 0;
    if (s.inFence || s.inDisplay || fenceOf(line, &marker, &length) || line.trimmed() == u"$$")
        return std::nullopt;
    return startsBlock(line) ? start : paragraph;
}

/// Where the paragraph of the caret — at the end of `prefix` — starts, read line by line (fenced
/// code and `$$` formulas inside the text; code spans and formulas end at a blank line or a new
/// block); nullopt when the caret is in fenced code or a `$$` formula.
std::optional<qsizetype> paragraphStart(QStringView prefix)
{
    Lines lines;
    qsizetype paragraph = 0;
    qsizetype start = 0;
    for (qsizetype end = prefix.indexOf(u'\n'); end >= 0; end = prefix.indexOf(u'\n', start)) {
        stepLine(lines, prefix.mid(start, end - start), start, end + 1, &paragraph);
        start = end + 1;
    }
    return caretLine(lines, prefix.mid(start), start, paragraph);
}

bool startsWithTwo(QStringView text, qsizetype i, QChar first, QChar second)
{
    return text[i] == first && i + 1 < text.size() && text[i + 1] == second;
}

qsizetype backtickRun(QStringView text, qsizetype i)
{
    qsizetype n = 0;
    while (i + n < text.size() && text[i + n] == u'`')
        ++n;
    return n;
}

/// The code span open after a run of `n` backticks: outside one (and a comment) the run opens
/// one, inside one the same count closes it.
qsizetype afterRun(qsizetype openRun, bool comment, qsizetype n)
{
    if (openRun == 0)
        return comment ? 0 : n;
    return n == openRun ? 0 : openRun;
}

/// A code span (one still open at the end too) or a `%%` comment open at the end of `para`.
bool inCodeSpanOrComment(QStringView para)
{
    qsizetype openRun = 0; // the backticks that opened the span the text is in
    bool comment = false;
    for (qsizetype i = 0; i < para.size(); ++i) {
        if (openRun == 0 && startsWithTwo(para, i, u'\\', u'`')) {
            ++i; // an escaped backtick
        } else if (para[i] == u'`') {
            const qsizetype n = backtickRun(para, i);
            openRun = afterRun(openRun, comment, n);
            i += n - 1;
        } else if (openRun == 0 && startsWithTwo(para, i, u'%', u'%')) {
            comment = !comment;
            ++i;
        }
    }
    return openRun > 0 || comment;
}

/// A formula around the end of `para`: one the text (`rest` after the caret) already closes, or one
/// opened before the caret that a `$` (`$$`) typed here would close (after a letter: a `$` after a
/// space closes nothing).
bool inFormula(const QString &para, QStringView rest)
{
    const qsizetype pos = para.size();
    for (const QString &text : {QString(para + rest), QString(para + u"x$"_s), QString(para + u"x$$"_s)}) {
        for (const markdown::MathSpan &span : markdown::findMathSpans(markdown::maskInlineCode(text)))
            if (span.contentFrom <= pos && span.contentTo > pos)
                return true;
    }
    return false;
}

/// A wiki link or embed not closed yet, or a Markdown link's destination still open.
bool inLinkTarget(const QString &masked)
{
    const qsizetype wiki = masked.lastIndexOf(u"[["_s);
    if (wiki >= 0 && masked.indexOf(u"]]"_s, wiki) < 0)
        return true;
    const qsizetype destination = masked.lastIndexOf(u"]("_s);
    return destination >= 0 && masked.indexOf(u')', destination) < 0;
}

/// The word the text ends in is a URL (`https://…`, `www.…`, `<…`).
bool endsInUrl(const QString &masked)
{
    qsizetype word = masked.size();
    while (word > 0 && !masked[word - 1].isSpace())
        --word;
    const QStringView token = QStringView(masked).mid(word);
    return token.contains(u"://"_s) || token.startsWith(u"www."_s, Qt::CaseInsensitive) || token.startsWith(u"<"_s);
}

} // namespace

bool inputRuleProtected(const QString &text, int at)
{
    at = std::clamp(at, 0, int(text.size()));
    const QString prefix = text.left(at);
    const std::optional<qsizetype> paragraph = paragraphStart(prefix);
    if (!paragraph)
        return true; // fenced code or a `$$` formula
    const QString para = prefix.mid(*paragraph);
    if (inCodeSpanOrComment(para) || inFormula(para, QStringView(text).mid(at)))
        return true;
    // Link targets and URLs (code spans blanked: a `[[` in code opens nothing).
    const QString masked = markdown::maskInlineCode(para);
    return inLinkTarget(masked) || endsInUrl(masked);
}

std::optional<InputRuleHit> inputRuleMatch(const QString &text, int cursor, int typed, const QList<InputRule> &rules)
{
    if (rules.isEmpty() || typed < 1 || cursor < typed || cursor > text.size())
        return std::nullopt;
    std::optional<InputRuleHit> best;
    // The longest match wins; of equally long ones the rule declared last.
    for (qsizetype i = rules.size() - 1; i >= 0; --i) {
        if (best && best->to - best->from >= rules[i].match.size())
            continue;
        if (const std::optional<InputRuleHit> hit = ruleMatch(text, cursor, typed, rules[i]))
            best = hit;
    }
    return best;
}

std::optional<InputRuleHit> inputRuleHit(const QString &text, int cursor, int typed, const QList<InputRule> &rules)
{
    std::optional<InputRuleHit> hit = inputRuleMatch(text, cursor, typed, rules);
    if (!hit || inputRuleProtected(text, hit->from))
        return std::nullopt;
    return hit;
}

bool sourceInputRuleProtected(const QString &source, int at)
{
    if (const std::optional<markdown::FrontmatterBlock> front = markdown::findFrontmatter(source);
        front && at < front->bodyStart)
        return true;
    return inputRuleProtected(source, at);
}

} // namespace brulion::editor
