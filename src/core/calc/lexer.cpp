#include "calc/lexer_p.h"

#include "calc/util_p.h"

#include <array>

namespace brulion::calc {

namespace detail {

bool isIdentifier(QStringView name)
{
    const QString s = toNfc(name);
    if (s.isEmpty())
        return false;
    qsizetype i = 0;
    bool first = true;
    while (i < s.size()) {
        qsizetype len = 1;
        const char32_t cp = codePointAt(s, i, &len);
        if (first ? !isIdentStart(cp) : !isIdentPart(cp))
            return false;
        first = false;
        i += len;
    }
    return true;
}

QString sanitizeIdentifier(QStringView key)
{
    const QString s = toNfc(key).trimmed();
    QString out;
    bool gap = false;
    qsizetype i = 0;
    while (i < s.size()) {
        qsizetype len = 1;
        const char32_t cp = codePointAt(s, i, &len);
        if (isIdentPart(cp)) {
            if (gap)
                out += u'_';
            gap = false;
            out += s.mid(i, len);
        } else {
            gap = true;
        }
        i += len;
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
    qsizetype len = 1;
    if (!isIdentStart(codePointAt(out, 0, &len)))
        out.prepend(u'_');
    return out;
}

} // namespace detail

namespace {

using detail::isAsciiDigit;

struct OperatorSpelling {
    QStringView spelling;
    QStringView canonical;
};

// Longest spellings first.
constexpr std::array<OperatorSpelling, 25> kOperators = {{
    {u"**", u"^"}, {u"==", u"=="}, {u"!=", u"!="}, {u"<>", u"!="}, {u"<=", u"<="}, {u">=", u">="},
    {u"&&", u"and"}, {u"||", u"or"},
    {u"+", u"+"}, {u"-", u"-"}, {u"*", u"*"}, {u"/", u"/"}, {u"^", u"^"}, {u"%", u"%"}, {u"!", u"!"},
    {u"=", u"="}, {u"<", u"<"}, {u">", u">"}, {u"?", u"?"}, {u":", u":"},
    {u"×", u"*"}, {u"·", u"*"}, {u"÷", u"/"}, {u"−", u"-"}, {u"√", u"√"},
}};

/// Unicode operators not in kOperators: ⋅ ∙ ✕ ∶ – ≤ ≥ ≠.
QStringView unicodeOperator(QChar c)
{
    switch (c.unicode()) {
    case 0x22c5: // ⋅
    case 0x2219: // ∙
    case 0x2715: // ✕
        return u"*";
    case 0x2236: // ∶
        return u"/";
    case 0x2013: // –
        return u"-";
    case 0x2264: // ≤
        return u"<=";
    case 0x2265: // ≥
        return u">=";
    case 0x2260: // ≠
        return u"!=";
    default:
        return {};
    }
}

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
    if (s.isEmpty())
        return false;
    for (const QChar c : s) {
        if (!isAsciiDigit(c))
            return false;
    }
    return true;
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
        Token t;
        t.kind = kind;
        t.text = std::move(text);
        t.pos = start;
        t.length = length;
        t.number = number;
        m_tokens.append(std::move(t));
    }

    void readString(QChar quote)
    {
        const qsizetype start = m_pos++;
        QString content;
        bool closed = false;
        while (m_pos < m_src.size()) {
            const QChar c = m_src.at(m_pos);
            if (c == u'\\' && m_pos + 1 < m_src.size()) {
                content += m_src.at(m_pos + 1);
                m_pos += 2;
                continue;
            }
            ++m_pos;
            if (c == quote) {
                closed = true;
                break;
            }
            content += c;
        }
        push(TokenKind::String, content, start, m_pos - start);
        m_tokens.last().unterminated = !closed;
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
        const QChar e = peekChar(0);
        if (e == u'e' || e == u'E') {
            qsizetype digitsAt = 1;
            if (peekChar(1) == u'+' || peekChar(1) == u'-')
                digitsAt = 2;
            if (isAsciiDigit(peekChar(digitsAt))) {
                m_pos += digitsAt;
                while (isAsciiDigit(peekChar(0)))
                    ++m_pos;
            }
        }
        const QString text = m_src.mid(start, m_pos - start).toString();
        push(TokenKind::Number, text, start, m_pos - start, text.toDouble());
    }

    /// Identifiers, degree signs, superscripts, currencies, brackets and operators.
    bool readSymbol(QChar c)
    {
        qsizetype len = 1;
        const char32_t cp = detail::codePointAt(m_src, m_pos, &len);
        if (detail::isIdentStart(cp)) {
            readIdentifier();
            return true;
        }
        if (superscriptValue(c) != -2) {
            readSuperscript();
            return true;
        }
        if (c == QChar(0x00b0)) { // °, °C, °F
            const QChar next = peekChar(1);
            qsizetype nextLen = 1;
            const bool unitFollows = (next == u'C' || next == u'F')
                                     && (m_pos + 2 >= m_src.size()
                                         || !detail::isIdentPart(detail::codePointAt(m_src, m_pos + 2, &nextLen)));
            const qsizetype length = unitFollows ? 2 : 1;
            push(TokenKind::Ident, m_src.mid(m_pos, length).toString(), m_pos, length);
            m_pos += length;
            return true;
        }
        if (c == QChar(0x2103) || c == QChar(0x2109)) { // ℃ ℉
            push(TokenKind::Ident, c == QChar(0x2103) ? QStringLiteral("°C") : QStringLiteral("°F"), m_pos, 1);
            ++m_pos;
            return true;
        }
        if (c == QChar(0x221e)) { // ∞
            push(TokenKind::Ident, QStringLiteral("∞"), m_pos, 1);
            ++m_pos;
            return true;
        }
        if (isCurrencySymbol(c)) {
            push(TokenKind::Currency, QString(c), m_pos, 1);
            ++m_pos;
            return true;
        }
        return readPunctuation(c);
    }

    void readIdentifier()
    {
        const qsizetype start = m_pos;
        while (m_pos < m_src.size()) {
            qsizetype len = 1;
            const char32_t cp = detail::codePointAt(m_src, m_pos, &len);
            if (!detail::isIdentPart(cp))
                break;
            m_pos += len;
        }
        push(TokenKind::Ident, m_src.mid(start, m_pos - start).toString(), start, m_pos - start);
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
            if (v == -1)
                negative = true;
            else {
                value = value * 10 + v;
                any = true;
            }
            ++m_pos;
        }
        push(any ? TokenKind::Superscript : TokenKind::Unknown, m_src.mid(start, m_pos - start).toString(), start,
             m_pos - start, negative ? -value : value);
    }

    bool readPunctuation(QChar c)
    {
        switch (c.unicode()) {
        case u'(':
            push(TokenKind::LParen, QStringLiteral("("), m_pos++, 1);
            return true;
        case u')':
            push(TokenKind::RParen, QStringLiteral(")"), m_pos++, 1);
            return true;
        case u'[':
            push(TokenKind::LBracket, QStringLiteral("["), m_pos++, 1);
            return true;
        case u']':
            push(TokenKind::RBracket, QStringLiteral("]"), m_pos++, 1);
            return true;
        case u',':
            push(TokenKind::Comma, QStringLiteral(","), m_pos++, 1);
            return true;
        case u';':
            push(TokenKind::Semicolon, QStringLiteral(";"), m_pos++, 1);
            return true;
        default:
            break;
        }
        const QStringView rest = m_src.mid(m_pos);
        for (const OperatorSpelling &op : kOperators) {
            if (rest.startsWith(op.spelling)) {
                push(TokenKind::Operator, op.canonical.toString(), m_pos, op.spelling.size());
                m_pos += op.spelling.size();
                return true;
            }
        }
        const QStringView unicode = unicodeOperator(c);
        if (!unicode.isEmpty()) {
            push(TokenKind::Operator, unicode.toString(), m_pos++, 1);
            return true;
        }
        return false;
    }

    void readOther()
    {
        qsizetype len = 1;
        detail::codePointAt(m_src, m_pos, &len);
        push(TokenKind::Unknown, m_src.mid(m_pos, len).toString(), m_pos, len);
        m_pos += len;
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

/// Innermost bracket frame of each token (-1 = top level); frames record ';' inside them.
QList<int> computeFrames(const QList<Token> &tokens, QList<Frame> &frames)
{
    QList<int> frameOf(tokens.size(), -1);
    QList<int> stack;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        const int top = stack.isEmpty() ? -1 : stack.last();
        frameOf[i] = top;
        if (t.kind == TokenKind::LParen || t.kind == TokenKind::LBracket) {
            Frame f;
            f.kind = t.kind;
            f.call = t.kind == TokenKind::LParen && i > 0 && isCallTarget(tokens.at(i - 1));
            frames.append(f);
            stack.append(int(frames.size() - 1));
        } else if (t.kind == TokenKind::RParen || t.kind == TokenKind::RBracket) {
            if (!stack.isEmpty())
                stack.removeLast();
            frameOf[i] = stack.isEmpty() ? -1 : stack.last();
        } else if (t.kind == TokenKind::Semicolon && top >= 0) {
            frames[top].semicolon = true;
        }
    }
    return frameOf;
}

Token mergedNumber(const Token &first, const Token &last, QString code)
{
    Token t;
    t.kind = TokenKind::Number;
    t.pos = first.pos;
    t.length = last.pos + last.length - first.pos;
    t.number = code.toDouble();
    t.text = std::move(code);
    return t;
}

QList<Token> mergeDecimalCommas(const QList<Token> &tokens, CalcLocale locale)
{
    QList<Frame> frames;
    const QList<int> frameOf = computeFrames(tokens, frames);
    const auto commaIsDecimal = [&](qsizetype commaIndex) {
        const int f = frameOf.at(commaIndex);
        if (f < 0)
            return true;
        const Frame &frame = frames.at(f);
        return frame.kind == TokenKind::LParen && (!frame.call || frame.semicolon);
    };
    QList<Token> out;
    out.reserve(tokens.size());
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        const bool candidate = t.kind == TokenKind::Number && allDigits(t.text) && i + 2 < tokens.size()
                               && tokens.at(i + 1).kind == TokenKind::Comma
                               && tokens.at(i + 2).kind == TokenKind::Number
                               && isAsciiDigit(tokens.at(i + 2).text.front()) && adjacent(t, tokens.at(i + 1))
                               && adjacent(tokens.at(i + 1), tokens.at(i + 2)) && commaIsDecimal(i + 1);
        if (!candidate) {
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
            out.append(mergedNumber(t, tokens.at(j - 1), digits));
            i = j - 1;
            continue;
        }
        if (next.text.contains(u'.')) {
            out.append(t);
            continue;
        }
        out.append(mergedNumber(t, next, t.text + u'.' + next.text));
        i += 2;
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
                out.append(mergedNumber(t, tokens.at(j - 1), code));
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
    for (const QStringView w : kWords) {
        if (word.compare(w, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool isOfWord(QStringView word)
{
    return word.compare(u"of", Qt::CaseInsensitive) == 0 || word == u"z" || word.compare(u"od", Qt::CaseInsensitive) == 0;
}

} // namespace brulion::calc
