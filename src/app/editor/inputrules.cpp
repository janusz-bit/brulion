#include "inputrules.h"

#include "markdown/inline.h"
#include "markdown/math.h"

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

} // namespace

bool inputRuleProtected(const QString &text, int at)
{
    at = std::clamp(at, 0, int(text.size()));
    const QString prefix = text.left(at);
    // Line by line: fenced code and `$$` display formulas written inside the block's text, and
    // where the paragraph the caret is in starts (code spans and formulas end at a blank line).
    qsizetype paragraph = 0;
    bool inFence = false, inDisplay = false;
    QChar fenceMarker;
    int fenceLength = 0;
    qsizetype start = 0;
    while (true) {
        const qsizetype end = prefix.indexOf(u'\n', start);
        const bool last = end < 0;
        const QStringView line = QStringView(prefix).mid(start, (last ? prefix.size() : end) - start);
        if (inFence) {
            if (last)
                return true;
            if (closesFence(line, fenceMarker, fenceLength)) {
                inFence = false;
                paragraph = end + 1;
            }
        } else if (inDisplay) {
            if (last)
                return true;
            if (line.trimmed() == u"$$") {
                inDisplay = false;
                paragraph = end + 1;
            }
        } else {
            QChar marker;
            int length = 0;
            if (fenceOf(line, &marker, &length)) {
                if (last)
                    return true; // the fence line itself: its info string
                inFence = true;
                fenceMarker = marker;
                fenceLength = length;
            } else if (line.trimmed() == u"$$") {
                if (last)
                    return true;
                inDisplay = true;
            } else if (line.trimmed().isEmpty() && !last) {
                paragraph = end + 1;
            }
        }
        if (last)
            break;
        start = end + 1;
    }
    const QString para = prefix.mid(paragraph);

    // Code spans (one still open before the caret too) and %% comments.
    int openRun = 0;
    bool comment = false;
    for (qsizetype i = 0; i < para.size(); ++i) {
        const QChar c = para[i];
        if (openRun == 0 && c == u'\\' && i + 1 < para.size() && para[i + 1] == u'`') {
            ++i;
            continue;
        }
        if (c == u'`') {
            int n = 0;
            while (i + n < para.size() && para[i + n] == u'`')
                ++n;
            if (openRun == 0 && !comment)
                openRun = n;
            else if (n == openRun)
                openRun = 0;
            i += n - 1;
            continue;
        }
        if (openRun == 0 && c == u'%' && i + 1 < para.size() && para[i + 1] == u'%') {
            comment = !comment;
            ++i;
        }
    }
    if (openRun > 0 || comment)
        return true;

    // Formulas: one the text already closes, or one opened before the caret that a `$` (`$$`)
    // typed here would close (after a letter: a `$` after a space closes nothing).
    const qsizetype pos = para.size();
    for (const QString &closer : {QString(), u"x$"_s, u"x$$"_s}) {
        const QString masked = markdown::maskInlineCode(closer.isNull() ? QString(para + text.mid(at)) : para + closer);
        for (const markdown::MathSpan &span : markdown::findMathSpans(masked))
            if (span.contentFrom <= pos && span.contentTo > pos)
                return true;
    }

    // Link targets and URLs (code spans blanked: a `[[` in code opens nothing).
    const QString masked = markdown::maskInlineCode(para);
    const qsizetype wiki = masked.lastIndexOf(u"[["_s);
    if (wiki >= 0 && masked.indexOf(u"]]"_s, wiki) < 0)
        return true;
    const qsizetype destination = masked.lastIndexOf(u"]("_s);
    if (destination >= 0 && masked.indexOf(u')', destination) < 0)
        return true;
    qsizetype word = masked.size();
    while (word > 0 && !masked[word - 1].isSpace())
        --word;
    const QStringView token = QStringView(masked).mid(word);
    if (token.contains(u"://"_s) || token.startsWith(u"www."_s, Qt::CaseInsensitive) || token.startsWith(u"<"_s))
        return true;
    return false;
}

std::optional<InputRuleHit> inputRuleHit(const QString &text, int cursor, int typed, const QList<InputRule> &rules)
{
    if (rules.isEmpty() || typed < 1 || cursor < typed || cursor > text.size())
        return std::nullopt;
    std::optional<InputRuleHit> best;
    // The longest match wins; of equally long ones the rule declared last.
    for (qsizetype i = rules.size() - 1; i >= 0; --i) {
        const InputRule &rule = rules[i];
        const int m = int(rule.match.size());
        if (m == 0 || (best && best->to - best->from >= m))
            continue;
        int end = cursor;
        if (rule.word) {
            int boundary = 0;
            const char32_t last = codePointBefore(text, cursor, &boundary);
            if (boundary == 0 || isWordChar(last) || typed > m + boundary)
                continue;
            end = cursor - boundary;
        } else if (typed > m) {
            continue;
        }
        if (end < m || QStringView(text).mid(end - m, m) != rule.match)
            continue;
        if (rule.word) {
            int before = 0;
            const char32_t c = codePointBefore(text, end - m, &before);
            if (before > 0 && isWordChar(c))
                continue;
        }
        best = InputRuleHit{end - m, end, rule.replace};
    }
    if (!best || inputRuleProtected(text, best->from))
        return std::nullopt;
    return best;
}

} // namespace brulion::editor
