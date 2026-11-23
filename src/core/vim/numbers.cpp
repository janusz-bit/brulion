// Ctrl+A / Ctrl+X of the Vim engine (vim.h): add to or subtract from the number under or after the
// caret, as Vim does with nrformats=bin,hex — a decimal number (a leading '-' is its sign unless a
// letter, digit or '_' stands before it, so the day in 2026-11-23 is 11, not -11), a hexadecimal
// 0x1F and a binary 0b101 one (their width and the case of their digits kept; they wrap around at
// 64 bits). A decimal number with leading zeros keeps its width (009 → 010).
#include "vim.h"
#include "vim_p.h"

#include <QtNumeric>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::vim {

namespace {

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

bool isHexDigit(QChar c)
{
    return isDigit(c) || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
}

bool isWordCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == u'_';
}

struct NumberSpan {
    enum Kind { Decimal, Hex, Binary };
    Kind kind = Decimal;
    int start = 0; ///< including the sign or the 0x prefix
    int end = 0;   ///< after the last digit
    int digits = 0; ///< where the digits start
    bool negative = false;
};

/// The first number of `s` that ends after `fromColumn`.
bool findNumber(const QString &s, int fromColumn, NumberSpan &out)
{
    int i = 0;
    while (i < s.size()) {
        if (!isDigit(s[i])) {
            ++i;
            continue;
        }
        NumberSpan n;
        n.start = i;
        if (s[i] == u'0' && i + 2 < s.size() && (s[i + 1] == u'x' || s[i + 1] == u'X') && isHexDigit(s[i + 2])) {
            n.kind = NumberSpan::Hex;
            n.digits = i + 2;
            n.end = n.digits;
            while (n.end < s.size() && isHexDigit(s[n.end]))
                ++n.end;
        } else if (s[i] == u'0' && i + 2 < s.size() && (s[i + 1] == u'b' || s[i + 1] == u'B')
                   && (s[i + 2] == u'0' || s[i + 2] == u'1')) {
            n.kind = NumberSpan::Binary;
            n.digits = i + 2;
            n.end = n.digits;
            while (n.end < s.size() && (s[n.end] == u'0' || s[n.end] == u'1'))
                ++n.end;
        } else {
            n.digits = i;
            n.end = i;
            while (n.end < s.size() && isDigit(s[n.end]))
                ++n.end;
            if (i > 0 && s[i - 1] == u'-' && !(i > 1 && isWordCharacter(s[i - 2]))) {
                n.negative = true;
                n.start = i - 1;
            }
        }
        i = n.end;
        if (n.end > fromColumn) {
            out = n;
            return true;
        }
    }
    return false;
}

qint64 parseDecimal(const QString &digits)
{
    qint64 v = 0;
    for (const QChar c : digits) {
        if (qMulOverflow(v, qint64(10), &v) || qAddOverflow(v, qint64(c.unicode() - u'0'), &v))
            return std::numeric_limits<qint64>::max();
    }
    return v;
}

quint64 parseRadix(const QString &digits, int radix)
{
    quint64 v = 0;
    for (const QChar c : digits)
        v = v * quint64(radix) + quint64(c.digitValue() >= 0 ? c.digitValue() : (c.toLower().unicode() - u'a' + 10));
    return v;
}

} // namespace

/// Adds `delta` to the first number of `line` that ends after `fromColumn` and starts before
/// `toColumn`; `caret` is its last character afterwards.
bool Engine::addToNumberOnLine(int line, int fromColumn, int toColumn, qint64 delta, Position &caret)
{
    const QString s = lineText(line);
    NumberSpan n;
    if (!findNumber(s, fromColumn, n) || n.start >= toColumn)
        return false;
    const QString digits = s.mid(n.digits, n.end - n.digits);
    QString text;
    if (n.kind == NumberSpan::Decimal) {
        qint64 value = parseDecimal(digits);
        if (n.negative)
            value = -value;
        qint64 sum = 0;
        if (qAddOverflow(value, delta, &sum))
            sum = delta > 0 ? std::numeric_limits<qint64>::max() : -std::numeric_limits<qint64>::max();
        QString magnitude = QString::number(sum < 0 ? -sum : sum);
        if (digits.size() > 1 && digits[0] == u'0')
            magnitude = magnitude.rightJustified(int(digits.size()), u'0');
        text = (sum < 0 ? u"-"_s : QString()) + magnitude;
    } else {
        const int radix = n.kind == NumberSpan::Hex ? 16 : 2;
        const quint64 value = parseRadix(digits, radix) + quint64(delta);
        QString body = QString::number(value, radix);
        bool upper = false;
        if (n.kind == NumberSpan::Hex) {
            bool lower = false;
            for (const QChar c : digits) {
                if (c >= u'A' && c <= u'F')
                    upper = true;
                else if (c >= u'a' && c <= u'f')
                    lower = true;
            }
            if (!upper && !lower)
                upper = s[n.start + 1] == u'X';
            if (upper)
                body = body.toUpper();
        }
        body = body.rightJustified(int(digits.size()), u'0');
        text = s.mid(n.start, 2) + body;
    }
    if (!m_buffer->replace(Position{line, n.start}, Position{line, n.end}, text)) {
        setMessage(Message::ReadOnly);
        return false;
    }
    caret = Position{line, n.start + int(text.size()) - 1};
    return true;
}

void Engine::addToNumber(qint64 delta)
{
    const Position cur = m_buffer->cursor();
    Position caret;
    beginChange();
    const bool done = addToNumberOnLine(cur.line, cur.column, int(lineText(cur.line).size()) + 1, delta, caret);
    endChange();
    if (!done) {
        m_failed = true;
        return;
    }
    moveTo(caret);
}

void Engine::addToNumbersInSelection(Position from, Position to, bool lines, qint64 delta, bool progressive)
{
    const int last = std::min(lastLine(), to.line);
    qint64 step = delta;
    bool any = false;
    beginChange();
    for (int l = from.line; l <= last; ++l) {
        if (!lines && l == to.line && to.column == 0 && l > from.line)
            break; // the line break at the end of the selection
        const int fromColumn = !lines && l == from.line ? from.column : 0;
        const int toColumn = !lines && l == to.line ? to.column : lineLength(l);
        Position caret;
        if (addToNumberOnLine(l, fromColumn, toColumn, step, caret)) {
            any = true;
            if (progressive)
                step += delta;
        }
    }
    endChange();
    if (!any)
        m_failed = true;
}

} // namespace brulion::vim
