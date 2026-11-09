#include "calc/lexer_p.h"

#include "calc/util_p.h"

#include <algorithm>
#include <array>

namespace brulion::calc {

namespace detail {

bool isIdentifier(QStringView name)
{
    const QString s = toNfc(name);
    qsizetype len = 1;
    for (qsizetype i = 0; i < s.size(); i += len) {
        const char32_t cp = codePointAt(s, i, &len);
        if (i == 0 ? !isIdentStart(cp) : !isIdentPart(cp))
            return false;
    }
    return !s.isEmpty();
}

QString sanitizeIdentifier(QStringView key)
{
    const QString s = toNfc(key).trimmed();
    QString out;
    bool gap = false;
    qsizetype len = 1;
    for (qsizetype i = 0; i < s.size(); i += len) {
        if (!isIdentPart(codePointAt(s, i, &len))) {
            gap = true;
            continue;
        }
        if (std::exchange(gap, false))
            out += u'_';
        out += QStringView(s).sliced(i, len);
    }
    qsizetype start = 0;
    qsizetype end = out.size();
    while (start < end && out.at(start) == u'_')
        ++start;
    while (end > start && out.at(end - 1) == u'_')
        --end;
    out = out.mid(start, end - start);
    if (out.isEmpty())
        return {};
    if (!isIdentStart(codePointAt(out, 0, &len)))
        out.prepend(u'_');
    return out;
}

} // namespace detail

namespace {

using detail::isAsciiDigit;

/// A bracket, separator or operator as written and the token it gives (`canonical` = its text).
struct Symbol {
    QStringView spelling;
    QStringView canonical;
    TokenKind kind = TokenKind::Operator;
};

// Brackets and separators first (the most frequent), then the operators: a longer spelling before
// the shorter ones it starts with.
constexpr std::array<Symbol, 39> kSymbols = {{
        {u"(", u"(", TokenKind::LParen},
        {u")", u")", TokenKind::RParen},
        {u"[", u"[", TokenKind::LBracket},
        {u"]", u"]", TokenKind::RBracket},
        {u",", u",", TokenKind::Comma},
        {u";", u";", TokenKind::Semicolon},
        {u"**", u"^"},
        {u"==", u"=="},
        {u"!=", u"!="},
        {u"<>", u"!="},
        {u"<=", u"<="},
        {u">=", u">="},
        {u"&&", u"and"},
        {u"||", u"or"},
        {u"+", u"+"},
        {u"-", u"-"},
        {u"*", u"*"},
        {u"/", u"/"},
        {u"^", u"^"},
        {u"%", u"%"},
        {u"!", u"!"},
        {u"=", u"="},
        {u"<", u"<"},
        {u">", u">"},
        {u"?", u"?"},
        {u":", u":"},
        {u"×", u"*"},
        {u"·", u"*"},
        {u"÷", u"/"},
        {u"−", u"-"},
        {u"√", u"√"},
        // ⋅ ∙ ✕ ∶ – ≤ ≥ ≠
        {u"\u22c5", u"*"},
        {u"\u2219", u"*"},
        {u"\u2715", u"*"},
        {u"\u2236", u"/"},
        {u"\u2013", u"-"},
        {u"\u2264", u"<="},
        {u"\u2265", u">="},
        {u"\u2260", u"!="},
}};

/// Superscript digit/minus value: '²' -> 2, '⁻' -> -1 (as a marker), -2 for none.
int superscriptValue(QChar c)
{
    switch (c.unicode()) {
    case 0x2070:
        return 0;
    case 0x00b9:
        return 1;
    case 0x00b2:
        return 2;
    case 0x00b3:
        return 3;
    case 0x207b:
        return -1;
    default:
        if (c.unicode() >= 0x2074 && c.unicode() <= 0x2079)
            return c.unicode() - 0x2070;
        return -2;
    }
}

bool isCurrencySymbol(QChar c)
{
    return c == QChar(0x20ac) || c == u'$' || c == QChar(0x00a3);
}

bool allDigits(QStringView s)
{
    return !s.isEmpty() && std::ranges::all_of(s, isAsciiDigit);
}

/// `^\d{3}(\.\d+)?$` on a canonical number text.
bool isThreeDigitGroup(QStringView s)
{
    const qsizetype dot = s.indexOf(u'.');
    const QStringView integer = dot < 0 ? s : s.left(dot);
    if (integer.size() != 3 || !allDigits(integer))
        return false;
    return dot < 0 || allDigits(s.mid(dot + 1));
}

class Lexer {
public:
    explicit Lexer(QStringView src)
        : m_src(src)
    {
    }

    QList<Token> run()
    {
        while (m_pos < m_src.size()) {
            const QChar c = m_src.at(m_pos);
            if (c.isSpace()) {
                ++m_pos;
                continue;
            }
            if (c == u'#' || (c == u'/' && peekChar(1) == u'/'))
                break; // comment
            if (c == u'"' || (c == u'\'' && !isApostrophe()))
                readString(c);
            else if (isAsciiDigit(c) || (c == u'.' && isAsciiDigit(peekChar(1))))
                readNumber();
            else if (!readSymbol(c))
                readOther();
        }
        return m_tokens;
    }

private:
    QChar peekChar(qsizetype offset) const
    {
        const qsizetype i = m_pos + offset;
        return i < m_src.size() ? m_src.at(i) : QChar();
    }

    /// A `'` directly after a word, number or closing bracket is an apostrophe ("Jan's"), not a
    /// string quote — like the v1 engine, where it was mathjs' transpose operator.
    bool isApostrophe() const
    {
        if (m_tokens.isEmpty())
            return false;
        const Token &last = m_tokens.last();
        if (last.pos + last.length != m_pos)
            return false;
        return last.kind == TokenKind::Ident || last.kind == TokenKind::Number || last.kind == TokenKind::RParen
               || last.kind == TokenKind::RBracket || last.kind == TokenKind::Unknown;
    }

    void push(TokenKind kind, QString text, qsizetype start, qsizetype length, double number = 0)
    {
        m_tokens.append({.kind = kind, .text = std::move(text), .pos = start, .length = length, .number = number});
    }

    /// A token of the next `length` characters, which it consumes.
    void take(TokenKind kind, QString text, qsizetype length)
    {
        push(kind, std::move(text), m_pos, length);
        m_pos += length;
    }

    void readString(QChar quote)
    {
        const qsizetype start = m_pos++;
        QString content;
        bool closed = false;
        while (m_pos < m_src.size() && !closed) {
            const QChar c = m_src.at(m_pos++);
            if (c == u'\\' && m_pos < m_src.size())
                content += m_src.at(m_pos++);
            else if (c == quote)
                closed = true;
            else
                content += c;
        }
        m_tokens.append({.kind = TokenKind::String,
                         .text = std::move(content),
                         .pos = start,
                         .length = m_pos - start,
                         .unterminated = !closed});
    }

    void readNumber()
    {
        const qsizetype start = m_pos;
        while (isAsciiDigit(peekChar(0)))
            ++m_pos;
        if (peekChar(0) == u'.' && isAsciiDigit(peekChar(1))) {
            ++m_pos;
            while (isAsciiDigit(peekChar(0)))
                ++m_pos;
        }
        if (const QChar e = peekChar(0); e == u'e' || e == u'E') {
            const qsizetype digitsAt = (peekChar(1) == u'+' || peekChar(1) == u'-') ? 2 : 1;
            if (isAsciiDigit(peekChar(digitsAt))) {
                m_pos += digitsAt;
                while (isAsciiDigit(peekChar(0)))
                    ++m_pos;
            }
        }
        QString text = m_src.sliced(start, m_pos - start).toString();
        const double number = text.toDouble();
        push(TokenKind::Number, std::move(text), start, m_pos - start, number);
    }

    /// Identifiers, degree signs, superscripts, currencies, brackets and operators.
    bool readSymbol(QChar c)
    {
        qsizetype len = 1;
        if (detail::isIdentStart(detail::codePointAt(m_src, m_pos, &len))) {
            readIdentifier();
        } else if (superscriptValue(c) != -2) {
            readSuperscript();
        } else if (c == QChar(0x00b0)) { // °, °C, °F
            const QChar next = peekChar(1);
            const bool unitFollows =
                    (next == u'C' || next == u'F')
                    && (m_pos + 2 >= m_src.size() || !detail::isIdentPart(detail::codePointAt(m_src, m_pos + 2, &len)));
            const qsizetype length = unitFollows ? 2 : 1;
            take(TokenKind::Ident, m_src.sliced(m_pos, length).toString(), length);
        } else if (c == QChar(0x2103) || c == QChar(0x2109)) { // ℃ ℉
            take(TokenKind::Ident, c == QChar(0x2103) ? QStringLiteral("°C") : QStringLiteral("°F"), 1);
        } else if (c == QChar(0x221e)) { // ∞
            take(TokenKind::Ident, QStringLiteral("∞"), 1);
        } else if (isCurrencySymbol(c)) {
            take(TokenKind::Currency, QString(c), 1);
        } else {
            return readPunctuation(c);
        }
        return true;
    }

    void readIdentifier()
    {
        const qsizetype start = m_pos;
        qsizetype len = 1;
        while (m_pos < m_src.size() && detail::isIdentPart(detail::codePointAt(m_src, m_pos, &len)))
            m_pos += len;
        push(TokenKind::Ident, m_src.sliced(start, m_pos - start).toString(), start, m_pos - start);
    }

    void readSuperscript()
    {
        const qsizetype start = m_pos;
        bool negative = false;
        bool any = false;
        double value = 0;
        while (m_pos < m_src.size()) {
            const int v = superscriptValue(m_src.at(m_pos));
            if (v == -2 || (v == -1 && (any || negative)))
                break;
            if (v == -1) {
                negative = true;
            } else {
                value = value * 10 + v;
                any = true;
            }
            ++m_pos;
        }
        push(any ? TokenKind::Superscript : TokenKind::Unknown, m_src.sliced(start, m_pos - start).toString(), start,
             m_pos - start, negative ? -value : value);
    }

    /// Brackets, separators and operators (kSymbols).
    bool readPunctuation(QChar c)
    {
        const QStringView rest = m_src.sliced(m_pos);
        for (const Symbol &s : kSymbols) {
            if (s.spelling.front() == c && rest.startsWith(s.spelling)) {
                // The token text shares the literal's static data: no allocation per token.
                take(s.kind, QString::fromRawData(s.canonical.data(), s.canonical.size()), s.spelling.size());
                return true;
            }
        }
        return false;
    }

    void readOther()
    {
        qsizetype len = 1;
        detail::codePointAt(m_src, m_pos, &len);
        take(TokenKind::Unknown, m_src.sliced(m_pos, len).toString(), len);
    }

    QStringView m_src;
    qsizetype m_pos = 0;
    QList<Token> m_tokens;
};

// --- number normalisation -------------------------------------------------------------------

struct Frame {
    TokenKind kind = TokenKind::LParen;
    bool call = false;
    bool semicolon = false;
    qsizetype open = -1;    ///< index of the opening bracket
    int commas = 0;         ///< ',' directly inside
    int glued = 0;          ///< of them: between digits without spaces ("2,5")
    bool chained = false;   ///< two of those in a row ("1,2,3"): a list of numbers, not decimals
    bool decimal = false;   ///< a call whose glued commas are decimal commas (decided by arity)
    bool ambiguous = false; ///< a call whose glued commas fit its function both ways (`round(2,5)`)
};

/// The bracket frames of a token list.
struct BracketFrames {
    QList<Frame> frames;
    /// Innermost frame of each token (-1 = top level); a bracket belongs to the frame it delimits.
    QList<int> frameOf;

    /// The frame opened by the bracket at token `i`.
    const Frame &openedAt(qsizetype i) const { return frames.at(frameOf.at(i)); }

    /// A ',' between digits at token `i` is a decimal comma (not an argument or item separator).
    bool commaIsDecimal(qsizetype i) const
    {
        const int f = frameOf.at(i);
        if (f < 0)
            return true;
        const Frame &frame = frames.at(f);
        return frame.kind == TokenKind::LParen && (!frame.call || frame.semicolon || frame.decimal);
    }
};

/// An identifier directly before '(' that names a function (`max(`, `mod(`), not an operator
/// word or "of" (`20% z (…)`).
bool isCallTarget(const Token &t)
{
    if (t.kind != TokenKind::Ident || isOfWord(t.text))
        return false;
    return !isOperatorWord(t.text) || t.text.compare(u"mod", Qt::CaseInsensitive) == 0;
}

bool adjacent(const Token &a, const Token &b)
{
    return a.pos + a.length == b.pos;
}

/// A ',' at `i` written between digits without spaces ("2,5", "1,000.5").
bool commaBetweenDigits(const QList<Token> &tokens, qsizetype i)
{
    if (i < 1 || i + 1 >= tokens.size() || tokens.at(i).kind != TokenKind::Comma)
        return false;
    const Token &before = tokens.at(i - 1);
    const Token &after = tokens.at(i + 1);
    return before.kind == TokenKind::Number && allDigits(before.text) && after.kind == TokenKind::Number
           && isAsciiDigit(after.text.front()) && adjacent(before, tokens.at(i)) && adjacent(tokens.at(i), after);
}

/// Such a comma without a decimal point after it ("2,5"): a decimal comma or a separator.
bool gluedComma(const QList<Token> &tokens, qsizetype i)
{
    return commaBetweenDigits(tokens, i) && !tokens.at(i + 1).text.contains(u'.');
}

/// The bracket frames of `tokens`: they record ';' and ',' inside them, and calls without ';' with
/// commas between digits learn which reading their function takes.
BracketFrames computeFrames(const QList<Token> &tokens)
{
    BracketFrames out;
    out.frameOf.reserve(tokens.size());
    QList<int> stack;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        if (t.kind == TokenKind::LParen || t.kind == TokenKind::LBracket) {
            const bool call = t.kind == TokenKind::LParen && i > 0 && isCallTarget(tokens.at(i - 1));
            out.frames.append({.kind = t.kind, .call = call, .open = i});
            stack.append(int(out.frames.size() - 1));
        }
        const int top = stack.isEmpty() ? -1 : stack.last();
        out.frameOf.append(top);
        if (t.kind == TokenKind::RParen || t.kind == TokenKind::RBracket) {
            if (!stack.isEmpty())
                stack.removeLast();
        } else if (t.kind == TokenKind::Semicolon && top >= 0) {
            out.frames[top].semicolon = true;
        } else if (t.kind == TokenKind::Comma && top >= 0) {
            Frame &f = out.frames[top];
            ++f.commas;
            if (gluedComma(tokens, i)) {
                ++f.glued;
                f.chained = f.chained || gluedComma(tokens, i - 2);
            }
        }
    }
    // `sqrt(2,25)` is sqrt(2.25): two arguments would be too many. `pow(2,5)` keeps two, and
    // `round(2,5)` fits both readings.
    for (Frame &f : out.frames) {
        if (!f.call || f.semicolon || f.glued == 0 || f.chained)
            continue;
        const std::optional<std::pair<int, int>> arity = builtinArity(tokens.at(f.open - 1).text);
        if (!arity) // a function of the notebook (or an unknown one): the arguments as written
            continue;
        const auto fits = [&arity](int count) {
            return count >= arity->first && (arity->second < 0 || count <= arity->second);
        };
        const bool decimalFits = fits(f.commas + 1 - f.glued);
        const bool separatedFits = fits(f.commas + 1);
        f.decimal = decimalFits && !separatedFits;
        f.ambiguous = decimalFits && separatedFits;
    }
    return out;
}

Token mergedNumber(const Token &first, const Token &last, QString code)
{
    const double number = code.toDouble();
    return {.kind = TokenKind::Number,
            .text = std::move(code),
            .pos = first.pos,
            .length = last.pos + last.length - first.pos,
            .number = number};
}

QList<Token> mergeDecimalCommas(const QList<Token> &tokens, CalcLocale locale)
{
    const BracketFrames brackets = computeFrames(tokens);
    QList<Token> out;
    out.reserve(tokens.size());
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        if (t.kind == TokenKind::LParen && brackets.openedAt(i).ambiguous) {
            out.append(t);
            out.last().ambiguousComma = true;
            continue;
        }
        if (!commaBetweenDigits(tokens, i + 1) || !brackets.commaIsDecimal(i + 1)) {
            out.append(t);
            continue;
        }
        const Token &next = tokens.at(i + 2);
        if (locale == CalcLocale::English && t.text.size() <= 3 && isThreeDigitGroup(next.text)) {
            // English thousands separators: 1,000 / 12,345,678.5
            QString digits = t.text;
            qsizetype j = i + 1;
            while (j + 1 < tokens.size() && tokens.at(j).kind == TokenKind::Comma
                   && tokens.at(j + 1).kind == TokenKind::Number && isThreeDigitGroup(tokens.at(j + 1).text)
                   && adjacent(tokens.at(j - 1), tokens.at(j)) && adjacent(tokens.at(j), tokens.at(j + 1))) {
                digits += tokens.at(j + 1).text;
                j += 2;
                if (digits.contains(u'.'))
                    break;
            }
            out.append(mergedNumber(t, tokens.at(j - 1), std::move(digits)));
            i = j - 1;
        } else if (next.text.contains(u'.')) {
            out.append(t);
        } else {
            out.append(mergedNumber(t, next, t.text + u'.' + next.text));
            i += 2;
        }
    }
    return out;
}

bool isGroupSpace(QChar c)
{
    return c == u' ' || c == QChar(0x00a0) || c == QChar(0x2009) || c == QChar(0x202f);
}

QList<Token> mergeSpaceGroups(const QList<Token> &tokens, QStringView source)
{
    const auto groupSpaceBetween = [&](const Token &a, const Token &b) {
        const qsizetype gap = a.pos + a.length;
        return b.pos == gap + 1 && gap < source.size() && isGroupSpace(source.at(gap));
    };
    QList<Token> out;
    out.reserve(tokens.size());
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        if (t.kind == TokenKind::Number && t.text.size() <= 3 && allDigits(t.text)) {
            QString code = t.text;
            qsizetype j = i + 1;
            while (j < tokens.size() && tokens.at(j).kind == TokenKind::Number
                   && groupSpaceBetween(tokens.at(j - 1), tokens.at(j)) && isThreeDigitGroup(tokens.at(j).text)) {
                code += tokens.at(j).text;
                ++j;
                if (code.contains(u'.'))
                    break;
            }
            if (j > i + 1) {
                out.append(mergedNumber(t, tokens.at(j - 1), std::move(code)));
                i = j - 1;
                continue;
            }
        }
        out.append(t);
    }
    return out;
}

} // namespace

QList<Token> lex(QStringView input)
{
    return Lexer(input).run();
}

void normalizeNumbers(QList<Token> &tokens, QStringView source, CalcLocale locale)
{
    tokens = mergeSpaceGroups(mergeDecimalCommas(tokens, locale), source);
}

bool isOperatorWord(QStringView word)
{
    static constexpr std::array<QStringView, 6> kWords = {u"to", u"mod", u"and", u"or", u"xor", u"not"};
    return std::ranges::any_of(kWords, [word](QStringView w) { return word.compare(w, Qt::CaseInsensitive) == 0; });
}

bool isOfWord(QStringView word)
{
    return word.compare(u"of", Qt::CaseInsensitive) == 0 || word == u"z" || word.compare(u"od", Qt::CaseInsensitive) == 0;
}

} // namespace brulion::calc
