#include "markdown/frontmatter.h"

#include "markdown/frontmatter_p.h"
#include "markdown/lines_p.h"
#include "markdown/tokens_p.h"
#include "text/text.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

constexpr int MaxDepth = 64;
constexpr int MaxFlowLines = 500;
/// Largest integer kept as a number (JavaScript's Number.MAX_SAFE_INTEGER, as in v1).
constexpr qlonglong MaxSafeInteger = 9007199254740991LL;

qsizetype lineEnd(QStringView s, qsizetype from)
{
    qsizetype i = from;
    while (i < s.size() && !isLineBreak(s[i]))
        ++i;
    return i;
}

qsizetype nextLineStart(QStringView s, qsizetype end)
{
    if (end >= s.size())
        return s.size();
    return s[end] == u'\r' && charAt(s, end + 1) == u'\n' ? end + 2 : end + 1;
}

} // namespace

std::optional<FrontmatterBlock> findFrontmatter(QStringView content)
{
    const qsizetype start = startsWithBom(content) ? 1 : 0;
    qsizetype end = lineEnd(content, start);
    if (trimEnd(content.sliced(start, end - start)) != u"---" || end >= content.size())
        return std::nullopt;
    qsizetype pos = nextLineStart(content, end);
    const qsizetype yamlStart = pos;
    for (int line = 1;; ++line) {
        end = lineEnd(content, pos);
        const QStringView text = trimEnd(content.sliced(pos, end - pos));
        if (text == u"---" || text == u"...")
            return FrontmatterBlock{.endLine = line,
                                    .yaml = content.sliced(yamlStart, pos - yamlStart).toString(),
                                    .yamlStart = yamlStart,
                                    .bodyStart = nextLineStart(content, end),
                                    .bodyStartLine = line + 1};
        if (end >= content.size())
            return std::nullopt;
        pos = nextLineStart(content, end);
    }
}

FrontmatterResult parseFrontmatter(QStringView content)
{
    FrontmatterResult out;
    const auto fm = findFrontmatter(content);
    if (!fm)
        return out;
    out.data = parseYaml(fm->yaml, &out.keys);
    out.bodyStart = fm->bodyStart;
    out.bodyStartLine = fm->bodyStartLine;
    return out;
}

// ---------------------------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------------------------

namespace {

struct YLine {
    QStringView raw;
    int indent = 0;
    QStringView content; ///< after indentation, trailing whitespace removed
};

YLine toYLine(QStringView raw)
{
    YLine l;
    l.raw = raw;
    qsizetype i = 0;
    for (; i < raw.size() && isSpaceOrTab(raw[i]); ++i)
        l.indent += raw[i] == u'\t' ? 2 : 1;
    l.content = trimEnd(raw.sliced(i));
    return l;
}

bool isSkippable(const YLine &l)
{
    return l.content.isEmpty() || l.content.startsWith(u'#');
}

bool isSeqItem(QStringView content)
{
    return content == u"-" || content.startsWith(u"- ") || content.startsWith(u"-\t");
}

int hexDigit(QChar c)
{
    if (c >= u'0' && c <= u'9')
        return c.unicode() - u'0';
    if (c >= u'a' && c <= u'f')
        return c.unicode() - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c.unicode() - u'A' + 10;
    return -1;
}

struct Quoted {
    QString value;
    qsizetype end = 0;
};

/// A quoted scalar starting at `pos` (which holds the quote), or nullopt when unterminated.
std::optional<Quoted> readQuoted(QStringView s, qsizetype pos)
{
    const QChar q = charAt(s, pos);
    QString out;
    qsizetype i = pos + 1;
    if (q == u'\'') {
        while (i < s.size()) {
            const QChar c = s[i];
            if (c == u'\'') {
                if (charAt(s, i + 1) == u'\'') {
                    out += u'\'';
                    i += 2;
                    continue;
                }
                return Quoted{out, i + 1};
            }
            out += c;
            ++i;
        }
        return std::nullopt;
    }
    if (q != u'"')
        return std::nullopt;
    while (i < s.size()) {
        const QChar c = s[i];
        if (c == u'"')
            return Quoted{out, i + 1};
        if (c != u'\\') {
            out += c;
            ++i;
            continue;
        }
        const QChar e = charAt(s, i + 1);
        QChar simple;
        switch (e.unicode()) {
        case u'n': simple = u'\n'; break;
        case u't': simple = u'\t'; break;
        case u'r': simple = u'\r'; break;
        case u'0': simple = QChar(0); break;
        case u'b': simple = QChar(8); break;
        case u'f': simple = QChar(12); break;
        case u'e': simple = QChar(0x1b); break;
        case u'a': simple = QChar(7); break;
        case u'v': simple = QChar(11); break;
        case u'"': simple = u'"'; break;
        case u'\\': simple = u'\\'; break;
        case u'/': simple = u'/'; break;
        case u' ': simple = u' '; break;
        case u'\t': simple = u'\t'; break;
        case u'N': simple = QChar(0x85); break;
        case u'_': simple = QChar(0xa0); break;
        case u'L': simple = QChar(0x2028); break;
        case u'P': simple = QChar(0x2029); break;
        default: break;
        }
        if (!simple.isNull() || e == u'0') {
            out += simple;
            i += 2;
            continue;
        }
        const int hexLen = e == u'x' ? 2 : e == u'u' ? 4 : e == u'U' ? 8 : 0;
        if (hexLen && i + 2 + hexLen <= s.size()) {
            char32_t code = 0;
            bool ok = true;
            for (int k = 0; k < hexLen; ++k) {
                const int d = hexDigit(s[i + 2 + k]);
                if (d < 0) {
                    ok = false;
                    break;
                }
                code = code * 16 + char32_t(d);
            }
            if (ok && code <= 0x10ffff) {
                out += QString::fromUcs4(&code, 1);
                i += 2 + hexLen;
                continue;
            }
        }
        if (!e.isNull())
            out += e;
        i += 2;
    }
    return std::nullopt;
}

bool isYamlNumber(QStringView t)
{
    // ^[-+]?(?:(?:0|[1-9]\d*)(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?$
    qsizetype i = 0;
    if (charAt(t, i) == u'-' || charAt(t, i) == u'+')
        ++i;
    if (charAt(t, i) == u'.') {
        ++i;
        const qsizetype d0 = i;
        while (isAsciiDigit(charAt(t, i)))
            ++i;
        if (i == d0)
            return false;
    } else {
        if (charAt(t, i) == u'0') {
            ++i;
        } else if (isAsciiDigit(charAt(t, i))) {
            while (isAsciiDigit(charAt(t, i)))
                ++i;
        } else {
            return false;
        }
        if (charAt(t, i) == u'.') {
            ++i;
            while (isAsciiDigit(charAt(t, i)))
                ++i;
        }
    }
    if (charAt(t, i) == u'e' || charAt(t, i) == u'E') {
        ++i;
        if (charAt(t, i) == u'-' || charAt(t, i) == u'+')
            ++i;
        const qsizetype d0 = i;
        while (isAsciiDigit(charAt(t, i)))
            ++i;
        if (i == d0)
            return false;
    }
    return i == t.size();
}

/// Resolve a plain scalar: null, booleans, numbers; everything else (dates too) is a string.
QVariant resolvePlain(QStringView s)
{
    const QStringView t = s.trimmed();
    if (t.isEmpty() || t == u"~" || t == u"null" || t == u"Null" || t == u"NULL")
        return {};
    if (t == u"true" || t == u"True" || t == u"TRUE")
        return true;
    if (t == u"false" || t == u"False" || t == u"FALSE")
        return false;
    if (isYamlNumber(t)) {
        bool ok = false;
        if (!t.contains(u'.') && !t.contains(u'e', Qt::CaseInsensitive)) {
            if (const qlonglong n = t.toLongLong(&ok); ok && n <= MaxSafeInteger && n >= -MaxSafeInteger)
                return n;
        } else if (const double d = t.toDouble(&ok); ok && std::isfinite(d)) {
            return d;
        }
    }
    return t.toString();
}

/// Remove a ` #comment` from a plain scalar.
QString stripComment(QStringView s)
{
    if (s.startsWith(u'#'))
        return {};
    for (qsizetype i = 0; i + 1 < s.size(); ++i) {
        if (isSpaceOrTab(s[i]) && s[i + 1] == u'#')
            return s.first(i).trimmed().toString();
    }
    return s.trimmed().toString();
}

struct KeyValue {
    QString key;
    QStringView rest;
    qsizetype colon = 0; ///< index of the colon after the key
};

/// Split `key: rest` (plain or quoted key); nullopt when the line is not a mapping entry.
std::optional<KeyValue> splitKey(QStringView content)
{
    if (content.isEmpty() || content[0] == u'#')
        return std::nullopt;
    const QChar c0 = content[0];
    if (c0 == u'"' || c0 == u'\'') {
        const auto q = readQuoted(content, 0);
        if (!q)
            return std::nullopt;
        qsizetype j = q->end;
        while (isSpaceOrTab(charAt(content, j)))
            ++j;
        if (charAt(content, j) != u':')
            return std::nullopt;
        const QChar after = charAt(content, j + 1);
        if (!after.isNull() && !isSpaceOrTab(after))
            return std::nullopt;
        return KeyValue{q->value, content.sliced(j + 1).trimmed(), j};
    }
    if (c0 == u'[' || c0 == u'{' || c0 == u'|' || c0 == u'>' || c0 == u'%' || c0 == u'@' || c0 == u'`')
        return std::nullopt;
    if (isSeqItem(content))
        return std::nullopt;
    for (qsizetype k = 0; k < content.size(); ++k) {
        const QChar c = content[k];
        if (c == u':') {
            const QChar after = charAt(content, k + 1);
            if (after.isNull() || isSpaceOrTab(after)) {
                const QStringView key = content.first(k).trimmed();
                if (key.isEmpty())
                    return std::nullopt;
                return KeyValue{key.toString(), content.sliced(k + 1).trimmed(), k};
            }
        } else if (isSpaceOrTab(c) && charAt(content, k + 1) == u'#') {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

/// True when `[`/`{` brackets outside quotes are balanced.
bool flowBalanced(QStringView s)
{
    int depth = 0;
    QChar quote;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (!quote.isNull()) {
            if (quote == u'"' && c == u'\\')
                ++i;
            else if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'')
            quote = c;
        else if (c == u'[' || c == u'{')
            ++depth;
        else if (c == u']' || c == u'}')
            --depth;
        else if (c == u'#' && (i == 0 || s[i - 1] == u' ') && depth <= 0)
            break;
    }
    return depth <= 0;
}

/// Recursive-descent parser for flow collections (nullopt = not a valid flow collection).
class FlowParser {
public:
    explicit FlowParser(QStringView s) : m_s(s) { }

    std::optional<QVariant> parse()
    {
        auto v = value();
        if (!v)
            return std::nullopt;
        ws();
        if (m_i < m_s.size() && m_s[m_i] != u'#')
            return std::nullopt;
        return v;
    }

private:
    QChar at(qsizetype i) const { return charAt(m_s, i); }

    void ws()
    {
        while (m_i < m_s.size() && m_s[m_i].isSpace())
            ++m_i;
    }

    std::optional<QVariant> value()
    {
        ws();
        const QChar c = at(m_i);
        if (c == u'[' || c == u'{') {
            if (++m_depth > MaxDepth)
                return std::nullopt;
            auto v = c == u'[' ? seq() : map();
            --m_depth;
            return v;
        }
        if (c == u'"' || c == u'\'') {
            const auto q = readQuoted(m_s, m_i);
            if (!q)
                return std::nullopt;
            m_i = q->end;
            return QVariant(q->value);
        }
        return resolvePlain(plain(false));
    }

    QStringView plain(bool isKey)
    {
        const qsizetype start = m_i;
        while (m_i < m_s.size()) {
            const QChar c = m_s[m_i];
            if (c == u',' || c == u']' || c == u'}' || c == u'[' || c == u'{')
                break;
            if (c == u':') {
                const QChar next = m_i + 1 < m_s.size() ? m_s[m_i + 1] : QChar(u' ');
                if (isKey || next.isSpace() || next == u',' || next == u']' || next == u'}')
                    break;
            }
            ++m_i;
        }
        return m_s.sliced(start, m_i - start).trimmed();
    }

    std::optional<QVariant> seq()
    {
        ++m_i; // [
        QVariantList out;
        for (;;) {
            ws();
            if (at(m_i) == u']') {
                ++m_i;
                return QVariant(out);
            }
            if (m_i >= m_s.size())
                return std::nullopt;
            const qsizetype start = m_i;
            auto v = value();
            if (!v)
                return std::nullopt;
            ws();
            if (at(m_i) == u':') {
                // single-pair mapping inside a sequence: [a: 1]
                ++m_i;
                auto pv = value();
                if (!pv)
                    return std::nullopt;
                QVariantMap obj;
                obj.insert(v->isNull() ? QString() : v->toString(), *pv);
                out.append(obj);
            } else if (m_i > start || !v->isNull()) {
                out.append(*v);
            }
            ws();
            if (at(m_i) == u',')
                ++m_i;
            else if (at(m_i) != u']')
                return std::nullopt;
        }
    }

    std::optional<QVariant> map()
    {
        ++m_i; // {
        QVariantMap out;
        for (;;) {
            ws();
            if (at(m_i) == u'}') {
                ++m_i;
                return QVariant(out);
            }
            if (m_i >= m_s.size())
                return std::nullopt;
            QString key;
            const QChar c = at(m_i);
            if (c == u'"' || c == u'\'') {
                const auto q = readQuoted(m_s, m_i);
                if (!q)
                    return std::nullopt;
                m_i = q->end;
                key = q->value;
            } else {
                key = plain(true).toString();
            }
            ws();
            QVariant v;
            if (at(m_i) == u':') {
                ++m_i;
                auto parsed = value();
                if (!parsed)
                    return std::nullopt;
                v = *parsed;
            }
            out.insert(key, v);
            ws();
            if (at(m_i) == u',')
                ++m_i;
            else if (at(m_i) != u'}')
                return std::nullopt;
        }
    }

    QStringView m_s;
    qsizetype m_i = 0;
    int m_depth = 0;
};

/// YAML line folding for `>` scalars: single breaks become spaces, blank lines newlines.
QString foldLines(const QStringList &raw)
{
    QString body = raw.value(0);
    for (qsizetype k = 1; k < raw.size(); ++k) {
        const QString &cur = raw[k];
        const QString &prev = raw[k - 1];
        if (cur.isEmpty())
            body += u'\n';
        else if (prev.isEmpty())
            body += cur;
        else if (isSpaceOrTab(charAt(cur, 0)) || isSpaceOrTab(charAt(prev, 0)))
            body += u'\n' + cur;
        else
            body += u' ' + cur;
    }
    return body;
}

QStringView sliceIndent(QStringView raw, int n)
{
    int col = 0;
    qsizetype i = 0;
    while (i < raw.size() && col < n && isSpaceOrTab(raw[i])) {
        col += raw[i] == u'\t' ? 2 : 1;
        ++i;
    }
    return raw.sliced(i);
}

class YamlParser {
public:
    /// The lines are read in place: `src` must outlive the parser.
    explicit YamlParser(QStringView src)
    {
        for (qsizetype pos = 0;;) {
            const qsizetype end = lineEnd(src, pos);
            m_lines.append(toYLine(src.sliced(pos, end - pos)));
            if (end >= src.size())
                break;
            pos = nextLineStart(src, end);
        }
        // The break ending the last line does not start another (blank) line (matters for `|+`).
        if (m_lines.size() > 1 && m_lines.constLast().raw.isEmpty())
            m_lines.removeLast();
    }

    QVariantMap parseRoot(QStringList *keys)
    {
        QVariantMap root;
        while (const YLine *line = peek()) {
            if (splitKey(line->content))
                parseMapInto(root, line->indent, 0, keys);
            else
                ++m_i; // a top-level sequence/scalar is not frontmatter: skip
        }
        return root;
    }

private:
    const YLine *peek()
    {
        while (m_i < m_lines.size() && isSkippable(m_lines.at(m_i)))
            ++m_i;
        return lineAt(m_i);
    }

    const YLine *lineAt(qsizetype i) const { return i < m_lines.size() ? &m_lines[i] : nullptr; }

    void parseMapInto(QVariantMap &obj, int indent, int depth, QStringList *keys = nullptr)
    {
        while (const YLine *line = peek()) {
            if (line->indent < indent)
                return;
            ++m_i;
            // A deeper line here (malformed) or a line that is no `key: value` entry is skipped.
            const auto kv = line->indent == indent ? splitKey(line->content) : std::nullopt;
            if (!kv)
                continue;
            const QVariant value = parseValue(kv->rest, indent, depth + 1, true);
            if (keys && !obj.contains(kv->key))
                keys->append(kv->key);
            obj.insert(kv->key, value);
        }
    }

    QVariant parseValue(QStringView rest, int indent, int depth, bool allowSameIndentSeq)
    {
        if (depth > MaxDepth) {
            skipDeeper(indent);
            return {};
        }
        if (rest.isEmpty() || rest.startsWith(u'#'))
            return parseNested(indent, depth, allowSameIndentSeq);
        const QChar c = rest[0];
        if (c == u'|' || c == u'>') {
            static const QRegularExpression header(u"^([|>])([+-]?)(\\d?)([+-]?)[ \\t]*(?:#.*)?$"_s);
            if (const auto m = header.matchView(rest); m.hasMatch()) {
                const QStringView chomp = m.capturedView(2).isEmpty() ? m.capturedView(4) : m.capturedView(2);
                return parseBlockScalar(c == u'>', chomp, indent);
            }
        }
        if (c == u'[' || c == u'{')
            return parseFlowValue(rest, indent);
        if (c == u'"' || c == u'\'')
            return parseQuotedValue(rest, indent);
        return parsePlainValue(rest, indent);
    }

    void skipDeeper(int indent)
    {
        for (const YLine *l = peek(); l && l->indent > indent; l = peek())
            ++m_i;
    }

    QVariant parseNested(int indent, int depth, bool allowSameIndentSeq)
    {
        const YLine *next = peek();
        if (!next)
            return {};
        if (next->indent > indent)
            return parseBlock(next->indent, depth);
        if (allowSameIndentSeq && next->indent == indent && isSeqItem(next->content))
            return parseSeq(indent, depth);
        return {};
    }

    QVariant parseBlock(int indent, int depth)
    {
        const YLine *line = peek();
        if (isSeqItem(line->content))
            return parseSeq(indent, depth);
        if (splitKey(line->content)) {
            QVariantMap obj;
            parseMapInto(obj, indent, depth);
            return obj;
        }
        ++m_i;
        return parsePlainValue(line->content, indent - 1);
    }

    QVariant parseSeq(int indent, int depth)
    {
        QVariantList arr;
        while (const YLine *line = peek()) {
            if (line->indent != indent || !isSeqItem(line->content))
                break;
            ++m_i;
            const QStringView afterDash = line->content.sliced(1);
            const QStringView rest = afterDash.trimmed(); // the line has no trailing whitespace
            // A quoted scalar here is a value, never a key.
            const QChar lead = charAt(rest, 0);
            if (const auto kv = lead == u'"' || lead == u'\'' ? std::nullopt : splitKey(rest)) {
                // "- key: value" starts a mapping whose keys are aligned with "key".
                const int itemIndent = indent + 1 + int(afterDash.size() - rest.size());
                QVariantMap obj;
                obj.insert(kv->key, parseValue(kv->rest, itemIndent, depth + 1, true));
                parseMapInto(obj, itemIndent, depth + 1);
                arr.append(obj);
            } else if (isSeqItem(rest)) {
                // "- - a": nested sequence on the same line (rare) — a one-element list.
                arr.append(QVariant(QVariantList{resolvePlain(stripComment(rest.sliced(1)))}));
            } else {
                arr.append(parseValue(rest, indent, depth + 1, false));
            }
        }
        return arr;
    }

    QVariant parseBlockScalar(bool folded, QStringView chomp, int indent)
    {
        QStringList raw;
        int contentIndent = -1;
        for (; m_i < m_lines.size(); ++m_i) {
            const YLine &l = m_lines.at(m_i);
            if (l.content.isEmpty()) {
                raw.append(QString());
                continue;
            }
            if (contentIndent < 0 && l.indent > indent)
                contentIndent = l.indent; // the first line with content sets the indentation
            if (contentIndent < 0 || l.indent < contentIndent)
                break;
            raw.append(sliceIndent(l.raw, contentIndent).toString());
        }
        int trailing = 0;
        while (!raw.isEmpty() && raw.constLast().isEmpty()) {
            raw.removeLast();
            ++trailing;
        }
        if (raw.isEmpty())
            return QString();
        const QString body = folded ? foldLines(raw) : raw.join(u'\n');
        if (chomp == u"-")
            return body;
        if (chomp == u"+")
            return QString(body + QString(trailing + 1, u'\n'));
        return QString(body + u'\n');
    }

    QVariant parseFlowValue(QStringView rest, int indent)
    {
        QString text = rest.toString();
        int guard = 0;
        while (!flowBalanced(text) && guard++ < MaxFlowLines) {
            const YLine *next = lineAt(m_i);
            if (!next)
                break;
            // Deeper lines continue the collection; a closing bracket may sit at the key's indent.
            const bool closing = next->content.startsWith(u']') || next->content.startsWith(u'}');
            if (next->indent <= indent && !next->content.isEmpty() && !closing)
                break;
            text += u' ' + next->content;
            ++m_i;
        }
        FlowParser p(text);
        const auto v = p.parse();
        return v ? *v : QVariant(stripComment(text));
    }

    QVariant parseQuotedValue(QStringView rest, int indent)
    {
        QString text = rest.toString();
        auto q = readQuoted(text, 0);
        int guard = 0;
        while (!q && guard++ < MaxFlowLines) {
            const YLine *next = lineAt(m_i);
            if (!next || (next->indent <= indent && !next->content.isEmpty()))
                break;
            if (next->content.isEmpty())
                text += u'\n';
            else
                text += (text.endsWith(u'\n') ? QString() : u" "_s) + next->content;
            ++m_i;
            q = readQuoted(text, 0);
        }
        return q ? q->value : stripComment(text);
    }

    QVariant parsePlainValue(QStringView rest, int indent)
    {
        QString text = stripComment(rest);
        // Plain multi-line scalars: deeper lines continue the value.
        for (;;) {
            const YLine *next = lineAt(m_i);
            if (!next || next->content.isEmpty() || next->indent <= indent || next->content.startsWith(u'#'))
                break;
            text += u' ' + stripComment(next->content);
            ++m_i;
        }
        return resolvePlain(text);
    }

    QList<YLine> m_lines;
    qsizetype m_i = 0;
};

} // namespace

QVariantMap parseYaml(QStringView yaml, QStringList *keys)
{
    return YamlParser(yaml).parseRoot(keys);
}

// ---------------------------------------------------------------------------------------------
// Serializer (its pieces are shared through frontmatter_p.h)
// ---------------------------------------------------------------------------------------------

/// The JSON string literal (the double-quoted YAML form written by v1 via JSON.stringify).
QString detail::yamlQuoted(QStringView s)
{
    QString out;
    out.reserve(s.size() + 2);
    out += u'"';
    for (const QChar c : s) {
        switch (c.unicode()) {
        case u'"': out += u"\\\""; break;
        case u'\\': out += u"\\\\"; break;
        case u'\n': out += u"\\n"; break;
        case u'\r': out += u"\\r"; break;
        case u'\t': out += u"\\t"; break;
        case 8: out += u"\\b"; break;
        case 12: out += u"\\f"; break;
        default:
            if (c.unicode() < 0x20 || c.unicode() == 0x7f)
                out += u"\\u"_s + QString::number(c.unicode(), 16).rightJustified(4, u'0');
            else
                out += c;
        }
    }
    out += u'"';
    return out;
}

namespace {

bool needsQuotes(const QString &s)
{
    if (s.isEmpty() || s != s.trimmed())
        return true;
    static const QString indicators = u"-?:,[]{}#&*!|>'\"%@`"_s;
    if (indicators.contains(s[0]))
        return true;
    if (s.contains(u": ") || s.endsWith(u':') || s.contains(u" #") || s.contains(u'\t'))
        return true;
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u == 0x7f || u == 0x2028 || u == 0x2029 || u == 0xfeff)
            return true;
    }
    // Plain scalars that other YAML readers (YAML 1.1 such as Obsidian's, or full YAML 1.2)
    // would turn into booleans or numbers.
    static const QRegularExpression foreign(
            uR"(^(?:y|Y|yes|Yes|YES|n|N|no|No|NO|on|On|ON|off|Off|OFF|[-+]?\.(?:inf|Inf|INF)|\.(?:nan|NaN|NAN)|[-+]?0[0-7_]+|[-+]?0[xX][0-9a-fA-F_]+|[-+]?0[oO][0-7_]+|[-+]?0[bB][01_]+|[-+]?[0-9][0-9_]*(?:\.[0-9_]*)?(?:[eE][-+]?[0-9]+)?|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+(?:\.[0-9_]*)?)$)"_s);
    if (foreign.match(s).hasMatch())
        return true;
    const QVariant resolved = resolvePlain(s);
    return resolved.typeId() != QMetaType::QString || resolved.toString() != s;
}

} // namespace

QString detail::yamlString(const QString &s)
{
    return needsQuotes(s) ? yamlQuoted(s) : s;
}

QString detail::yamlFlowString(const QString &s)
{
    static const QString flowIndicators = u",[]{}"_s;
    const bool flowUnsafe = std::ranges::any_of(s, [](QChar c) { return flowIndicators.contains(c); });
    return flowUnsafe || needsQuotes(s) ? yamlQuoted(s) : s;
}

std::optional<QStringList> detail::yamlBlockScalar(const QString &s, const QString &indent)
{
    if (isSpaceOrTab(charAt(s, 0)))
        return std::nullopt;
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        if ((u < 0x20 && u != u'\n' && u != u'\t') || u == 0x7f) // also a '\r'
            return std::nullopt;
    }
    QStringView body(s);
    while (body.endsWith(u'\n'))
        body.chop(1);
    const qsizetype trailing = s.size() - body.size();
    if (body.isEmpty())
        return std::nullopt;
    QStringList out{trailing == 0 ? u"|-"_s : trailing == 1 ? u"|"_s : u"|+"_s};
    for (const QStringView l : body.split(u'\n'))
        out.append(l.isEmpty() ? QString() : indent + l);
    for (qsizetype k = 1; k < trailing; ++k)
        out.append(QString());
    return out;
}

namespace {

QString formatDouble(double d)
{
    if (!std::isfinite(d))
        return yamlQuoted(std::isnan(d) ? u"NaN"_s : d > 0 ? u"Infinity"_s : u"-Infinity"_s);
    return QString::number(d, 'g', QLocale::FloatingPointShortest);
}

/// Scalar in flow / inline position; nullopt for unsupported values.
std::optional<QString> formatScalar(const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return u"null"_s;
    switch (v.typeId()) {
    case QMetaType::Bool: return v.toBool() ? u"true"_s : u"false"_s;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort: return v.toString();
    case QMetaType::Double:
    case QMetaType::Float: return formatDouble(v.toDouble());
    case QMetaType::QString: return yamlString(v.toString());
    case QMetaType::QDate: return v.toDate().toString(u"yyyy-MM-dd"_s);
    case QMetaType::QDateTime: {
        const QDateTime dt = v.toDateTime();
        if (!dt.isValid())
            return QString();
        const QTime t = dt.time();
        const QString date = dt.date().toString(u"yyyy-MM-dd"_s);
        return t.hour() == 0 && t.minute() == 0 ? date : date + u' ' + t.toString(u"HH:mm"_s);
    }
    default:
        if (v.canConvert<QString>())
            return yamlString(v.toString());
        return std::nullopt;
    }
}

bool isList(const QVariant &v)
{
    return v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QStringList;
}

bool isMap(const QVariant &v)
{
    return v.typeId() == QMetaType::QVariantMap || v.typeId() == QMetaType::QVariantHash;
}

QString formatFlow(const QVariant &v)
{
    if (isList(v)) {
        QStringList parts;
        const QVariantList list = v.toList();
        for (const QVariant &item : list)
            parts.append(formatFlow(item));
        return u'[' + parts.join(u", ") + u']';
    }
    if (isMap(v)) {
        QStringList parts;
        const QVariantMap map = v.toMap();
        for (const auto &[key, value] : map.asKeyValueRange())
            parts.append(yamlFlowString(key) + u": " + formatFlow(value));
        return u'{' + parts.join(u", ") + u'}';
    }
    // Inside a flow collection a comma or bracket anywhere in a plain string would end it.
    if (v.typeId() == QMetaType::QString)
        return yamlFlowString(v.toString());
    return formatScalar(v).value_or(u"null"_s);
}

void emitSeq(const QVariantList &arr, int indent, QStringList &out);

void emitEntry(const QString &key, const QVariant &value, int indent, QStringList &out)
{
    const QString head = QString(indent, u' ') + yamlString(key) + u':';
    if (isList(value)) {
        const QVariantList list = value.toList();
        out.append(list.isEmpty() ? head + u" []" : head);
        emitSeq(list, indent + 2, out);
    } else if (isMap(value)) {
        const QVariantMap map = value.toMap();
        out.append(map.isEmpty() ? head + u" {}" : head);
        for (const auto &[k, v] : map.asKeyValueRange())
            emitEntry(k, v, indent + 2, out);
    } else if (!value.isValid() || value.isNull()) {
        out.append(head);
    } else if (value.typeId() == QMetaType::QString && value.toString().contains(u'\n')) {
        const QString s = value.toString();
        if (const auto block = yamlBlockScalar(s, QString(indent + 2, u' '))) {
            out.append(head + u' ' + block->first());
            out.append(block->mid(1));
        } else {
            out.append(head + u' ' + yamlQuoted(s));
        }
    } else if (const auto s = formatScalar(value)) {
        out.append(head + u' ' + *s);
    }
}

void emitSeq(const QVariantList &arr, int indent, QStringList &out)
{
    const QString dash = QString(indent, u' ') + u"- ";
    for (const QVariant &item : arr) {
        if (isList(item)) {
            out.append(dash + formatFlow(item));
        } else if (isMap(item)) {
            QStringList inner;
            const QVariantMap map = item.toMap();
            for (const auto &[key, value] : map.asKeyValueRange())
                emitEntry(key, value, indent + 2, inner);
            // The first entry goes on the dash line (in place of its indentation).
            out.append(dash + (inner.isEmpty() ? u"{}"_s : inner.constFirst().mid(indent + 2)));
            out.append(inner.mid(1));
        } else if (item.typeId() == QMetaType::QString && item.toString().contains(u'\n')) {
            out.append(dash + yamlQuoted(item.toString()));
        } else {
            out.append(dash + formatScalar(item).value_or(u"null"_s));
        }
    }
}

QStringList orderedKeys(const QVariantMap &data, const QStringList &keyOrder)
{
    QStringList keys;
    QSet<QString> seen;
    for (const QString &k : keyOrder) {
        if (data.contains(k) && !seen.contains(k)) {
            keys.append(k);
            seen.insert(k);
        }
    }
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (!seen.contains(it.key()))
            keys.append(it.key());
    }
    return keys;
}

} // namespace

QString stringifyYaml(const QVariantMap &data, const QStringList &keyOrder)
{
    QStringList out;
    for (const QString &key : orderedKeys(data, keyOrder))
        emitEntry(key, data.value(key), 0, out);
    return out.isEmpty() ? QString() : out.join(u'\n') + u'\n';
}

QString stringifyFrontmatter(const QVariantMap &data, const QStringList &keyOrder)
{
    const QString yaml = stringifyYaml(data, keyOrder);
    return yaml.isEmpty() ? QString() : u"---\n"_s + yaml + u"---\n"_s;
}

// ---------------------------------------------------------------------------------------------
// Single-key edits
// ---------------------------------------------------------------------------------------------

QString setFrontmatterValue(const QString &content, const QString &key, const QVariant &value)
{
    if (key.isEmpty())
        return content;
    QStringList entry;
    emitEntry(key, value, 0, entry);
    const QString eol = text::detectEol(content);
    const auto fm = findFrontmatter(content);
    if (!fm) {
        const qsizetype bom = startsWithBom(content) ? 1 : 0;
        return content.first(bom) + u"---"_s + eol + entry.join(eol) + eol + u"---"_s + eol + content.sliced(bom);
    }
    // The key's last entry is replaced; a new key goes before the closing delimiter.
    const QList<text::Line> lines = text::splitLines(content);
    const QList<FrontmatterEntry> found = frontmatterEntries(lines, fm->endLine, key);
    const QString replacement = entry.isEmpty() ? QString() : entry.join(eol) + eol;
    if (found.isEmpty())
        return rebuildLines(lines, {}, fm->endLine, replacement);
    return rebuildLines(lines, {found.last()}, found.last().first, replacement);
}

QString removeFrontmatterValue(const QString &content, const QString &key)
{
    const auto fm = findFrontmatter(content);
    if (!fm || key.isEmpty())
        return content;
    const QList<text::Line> lines = text::splitLines(content);
    const QList<FrontmatterEntry> found = frontmatterEntries(lines, fm->endLine, key);
    return found.isEmpty() ? content : rebuildLines(lines, {found.last()}, -1, QString());
}

// ---------------------------------------------------------------------------------------------
// Internal helpers (frontmatter_p.h)
// ---------------------------------------------------------------------------------------------

namespace detail {

QList<FrontmatterEntry> frontmatterEntries(const QList<text::Line> &lines, int endLine)
{
    QList<FrontmatterEntry> out;
    endLine = std::min(endLine, int(lines.size()));
    for (int i = 1; i < endLine; ++i) {
        const QStringView t = lines[i].text;
        if (isBlank(t) || isSpaceOrTab(t[0]) || t[0] == u'#' || isSeqItem(trimEndBlanks(t)))
            continue;
        QStringList keys;
        parseYaml(t, &keys);
        if (keys.isEmpty())
            continue;
        int last = i + 1;
        for (int j = i + 1; j < endLine; ++j) {
            const QStringView n = trimEnd(lines[j].text); // as the parser reads it: any whitespace
            if (n.isEmpty())
                continue;
            if (isSpaceOrTab(n[0]) || isSeqItem(n) || n[0] == u']' || n[0] == u'}')
                last = j + 1;
            else
                break;
        }
        out.append(FrontmatterEntry{keys.first(), i, last});
        i = last - 1;
    }
    return out;
}

QList<FrontmatterEntry> frontmatterEntries(const QList<text::Line> &lines, int endLine, const QString &key)
{
    QList<FrontmatterEntry> out = frontmatterEntries(lines, endLine);
    out.removeIf([&key](const FrontmatterEntry &e) { return e.key != key; });
    return out;
}

QString rebuildLines(const QList<text::Line> &lines, const QList<FrontmatterEntry> &entries, int at,
                     const QString &replacement)
{
    QString out;
    qsizetype next = 0; // index into entries
    for (int i = 0; i < lines.size(); ++i) {
        if (i == at)
            out += replacement;
        while (next < entries.size() && i >= entries[next].last)
            ++next;
        if (next < entries.size() && i >= entries[next].first)
            continue;
        out += lines[i].text + lines[i].eol;
    }
    return out;
}

bool isYamlMapping(QStringView yaml)
{
    return !parseYaml(yaml).isEmpty() || std::ranges::all_of(text::splitLines(yaml), [](const text::Line &l) {
        const QStringView t = QStringView(l.text).trimmed();
        return t.isEmpty() || t.startsWith(u'#');
    });
}

qsizetype yamlCommentStart(QStringView s, qsizetype from)
{
    QChar quote;
    for (qsizetype i = from; i < s.size(); ++i) {
        const QChar c = s[i];
        if (!quote.isNull()) {
            if (c == quote)
                quote = QChar();
            continue;
        }
        if ((c == u'"' || c == u'\'') && (i == from || isSpaceOrTab(s[i - 1]) || s[i - 1] == u'[' || s[i - 1] == u','))
            quote = c;
        else if (c == u'#' && (i == from || isSpaceOrTab(s[i - 1])))
            return i;
    }
    return s.size();
}

std::optional<YamlKeyLine> splitYamlKeyLine(QStringView line)
{
    const auto kv = splitKey(trimEnd(line));
    if (!kv)
        return std::nullopt;
    // The value starts after the blanks following the colon.
    const qsizetype afterColon = kv->colon + 1;
    return YamlKeyLine{kv->key, kv->colon, afterColon + indentOf(line.sliced(afterColon)).chars};
}

std::optional<QString> yamlScalar(const QVariant &v)
{
    if (isList(v) || isMap(v))
        return std::nullopt;
    return formatScalar(v);
}

} // namespace detail

} // namespace brulion::markdown
