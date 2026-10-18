#include "markdown/inline.h"

#include "markdown/inline_p.h"
#include "markdown/tokens_p.h"

#include <QRegularExpression>
#include <QSet>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace detail {

namespace {

/// Longest link text / destination / wiki link we try to match (bounds work per '[').
constexpr qsizetype MaxLinkText = 1000;
constexpr qsizetype MaxDest = 2048;

bool isWs(QChar c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || (c.unicode() > 127 && c.isSpace());
}

bool isUrlPreceder(QChar c)
{
    switch (c.unicode()) {
    case u'(':
    case u'*':
    case u'_':
    case u'~':
    case u'"':
    case u'\'':
    case u'[':
    case u'>': return true;
    default: return false;
    }
}

bool isUrlTrailingPunct(QChar c)
{
    switch (c.unicode()) {
    case u'?':
    case u'!':
    case u'.':
    case u',':
    case u':':
    case u';':
    case u'*':
    case u'_':
    case u'~':
    case u'\'':
    case u'"': return true;
    default: return false;
    }
}

bool isLetterOrNumber(char32_t cp)
{
    switch (QChar::category(cp)) {
    case QChar::Letter_Uppercase:
    case QChar::Letter_Lowercase:
    case QChar::Letter_Titlecase:
    case QChar::Letter_Modifier:
    case QChar::Letter_Other:
    case QChar::Number_DecimalDigit:
    case QChar::Number_Letter:
    case QChar::Number_Other: return true;
    default: return false;
    }
}

bool isMark(char32_t cp)
{
    const QChar::Category cat = QChar::category(cp);
    return cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining || cat == QChar::Mark_Enclosing;
}

/// `[\p{L}\p{N}\p{M}_\-/]+` at `p`: end position (== p when none).
qsizetype tagBodyEnd(QStringView s, qsizetype p)
{
    qsizetype i = p;
    while (i < s.size()) {
        const CodePoint cp = codePointAt(s, i);
        if (cp.value == U'_' || cp.value == U'-' || cp.value == U'/' || isLetterOrNumber(cp.value) || isMark(cp.value))
            i += cp.length;
        else
            break;
    }
    return i;
}

QString nfc(QStringView s)
{
    for (const QChar c : s) {
        if (c.unicode() > 127)
            return s.toString().normalized(QString::NormalizationForm_C);
    }
    return s.toString();
}

/// Validate a `#tag` body; the cleaned name or an empty string.
QString cleanTagBody(QStringView body)
{
    QStringView name = body;
    while (name.endsWith(u'/'))
        name.chop(1);
    if (name.isEmpty())
        return {};
    bool allDigits = true;
    bool letterOrDigit = false;
    for (qsizetype i = 0; i < name.size();) {
        const CodePoint cp = codePointAt(name, i);
        // "#1" is no tag (SYNTAX §3) — in any script: "#٣", "#１２" are numbers too.
        if (QChar::category(cp.value) != QChar::Number_DecimalDigit)
            allDigits = false;
        if (isLetterOrNumber(cp.value))
            letterOrDigit = true;
        i += cp.length;
    }
    if (allDigits || !letterOrDigit)
        return {};
    return nfc(name);
}

qsizetype skipBlanks(QStringView s, qsizetype j)
{
    while (isSpaceOrTab(charAt(s, j)))
        ++j;
    return j;
}

/// End of a bare URL starting at `p` (GFM autolink extension rules), or -1. Linear.
qsizetype bareUrlEnd(QStringView s, qsizetype p)
{
    const QStringView rest = s.sliced(p);
    qsizetype schemeLen = 0;
    if (rest.startsWith(u"http://", Qt::CaseInsensitive))
        schemeLen = 7;
    else if (rest.startsWith(u"https://", Qt::CaseInsensitive))
        schemeLen = 8;
    if (!schemeLen)
        return -1;
    qsizetype j = p + schemeLen;
    int parens = 0;
    int brackets = 0;
    while (j < s.size()) {
        const QChar c = s[j];
        if (c == u'<' || c == u'>' || c == u'"' || c == u'`' || isWs(c))
            break;
        if (c == u'(')
            ++parens;
        else if (c == u')')
            --parens;
        else if (c == u'[')
            ++brackets;
        else if (c == u']')
            --brackets;
        ++j;
    }
    while (j > p + schemeLen) {
        const QChar last = s[j - 1];
        if (isUrlTrailingPunct(last)) {
            --j;
        } else if (last == u')' && parens < 0) {
            ++parens;
            --j;
        } else if (last == u']' && brackets < 0) {
            ++brackets;
            --j;
        } else {
            break;
        }
    }
    return j > p + schemeLen ? j : -1;
}

/// `<scheme:…>` autolink at `i`: end or -1.
qsizetype autolinkEnd(QStringView s, qsizetype i)
{
    qsizetype j = i + 1;
    if (!isAsciiLetter(charAt(s, j)))
        return -1;
    ++j;
    while (j - (i + 1) < 32) {
        const QChar c = charAt(s, j);
        if (isAsciiLetter(c) || isAsciiDigit(c) || c == u'+' || c == u'.' || c == u'-')
            ++j;
        else
            break;
    }
    const qsizetype schemeLen = j - (i + 1);
    if (schemeLen < 2 || schemeLen > 32 || charAt(s, j) != u':')
        return -1;
    ++j;
    while (j < s.size() && !isWs(s[j]) && s[j] != u'<' && s[j] != u'>')
        ++j;
    return charAt(s, j) == u'>' ? j + 1 : -1;
}

/// Mutable scan state: cached positions of the next `]]` / `]` (the scan only moves forward)
/// and of the subject's last lone surrogate.
struct ScanState {
    qsizetype wikiClose = -1; ///< -2 = none, -1 = unknown
    qsizetype bracketClose = -1;
    qsizetype lastLoneSurrogate = -2; ///< -2 = unknown, -1 = none
};

/// Position of the last UTF-16 unit of `s` that is not part of a surrogate pair, or -1.
qsizetype lastLoneSurrogate(QStringView s)
{
    for (qsizetype k = s.size() - 1; k >= 0; --k) {
        if (s[k].isLowSurrogate()) {
            if (k == 0 || !s[k - 1].isHighSurrogate())
                return k;
            --k; // a pair
        } else if (s[k].isHighSurrogate()) {
            return k; // its low surrogate would have been seen first
        }
    }
    return -1;
}

/// A character between `<` and `>` of an e-mail autolink: the local part, '@' or the domain.
bool isEmailAutolinkChar(QChar c)
{
    if (isAsciiLetter(c) || isAsciiDigit(c))
        return true;
    switch (c.unicode()) {
    case u'.':
    case u'!':
    case u'#':
    case u'$':
    case u'%':
    case u'&':
    case u'\'':
    case u'*':
    case u'+':
    case u'/':
    case u'=':
    case u'?':
    case u'^':
    case u'_':
    case u'`':
    case u'{':
    case u'|':
    case u'}':
    case u'~':
    case u'-':
    case u'@': return true;
    default: return false;
    }
}

/// `<local@domain>` autolink at `i`: end or -1.
qsizetype emailAutolinkEnd(QStringView s, qsizetype i, ScanState &st)
{
    // A match consists of characters of isEmailAutolinkChar() up to the first other character,
    // which must be '>': run the regex on that slice only. (QRegularExpression checks the UTF-16
    // of its whole subject on every call — per '<' this made long paragraphs quadratic.)
    qsizetype j = i + 1;
    while (j < s.size() && isEmailAutolinkChar(s[j]))
        ++j;
    if (j >= s.size() || s[j] != u'>')
        return -1;
    const QStringView candidate = s.sliced(i, j + 1 - i);
    if (!candidate.contains(u'@'))
        return -1;
    // As before with the whole paragraph as the subject and `i` as the offset: PCRE2 checks the
    // UTF-16 from the offset to the end (the pattern has no lookbehind) and matches nothing when
    // a lone surrogate follows.
    if (st.lastLoneSurrogate == -2)
        st.lastLoneSurrogate = lastLoneSurrogate(s);
    if (st.lastLoneSurrogate > i)
        return -1;
    static const QRegularExpression re(
            uR"(<[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?(?:\.[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?)*>)"_s);
    const auto m =
            re.matchView(candidate, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption);
    return m.hasMatch() ? i + m.capturedEnd(0) : -1;
}

/// `((id))` at `i`: end or -1 (id = [A-Za-z0-9_-]+).
qsizetype blockRefEnd(QStringView s, qsizetype i)
{
    if (charAt(s, i) != u'(' || charAt(s, i + 1) != u'(')
        return -1;
    qsizetype j = i + 2;
    while (j < s.size()) {
        const QChar c = s[j];
        if (isAsciiLetter(c) || isAsciiDigit(c) || c == u'_' || c == u'-')
            ++j;
        else
            break;
    }
    if (j == i + 2 || charAt(s, j) != u')' || charAt(s, j + 1) != u')')
        return -1;
    return j + 2;
}

qsizetype nextCached(QStringView s, qsizetype from, QStringView needle, qsizetype &cached)
{
    if (cached == -2)
        return -1;
    if (cached >= from)
        return cached;
    const qsizetype found = s.indexOf(needle, from);
    cached = found < 0 ? -2 : found;
    return found;
}

/// The `]]` closing a wiki link opened at `p` (first '['), on the same line, or -1.
qsizetype wikiCloseAt(QStringView s, qsizetype p, ScanState &st)
{
    const qsizetype close = nextCached(s, p + 2, u"]]", st.wikiClose);
    if (close < 0 || close - p > MaxLinkText)
        return -1;
    for (qsizetype k = p + 2; k < close; ++k) {
        if (isLineBreak(s[k]))
            return -1;
    }
    return close;
}

} // namespace

std::optional<WikiParts> parseWikiInner(QStringView inner)
{
    QStringView left = inner;
    QString display;
    const qsizetype pipe = inner.indexOf(u'|');
    if (pipe >= 0) {
        left = inner.first(pipe);
        // `[[Page\|Alias]]` inside tables escapes the pipe.
        if (left.endsWith(u'\\'))
            left.chop(1);
        display = inner.sliced(pipe + 1).trimmed().toString();
    }
    const qsizetype hash = left.indexOf(u'#');
    const QStringView target = (hash < 0 ? left : left.first(hash)).trimmed();
    const QStringView frag = hash < 0 ? QStringView() : left.sliced(hash + 1).trimmed();
    if (target.isEmpty() && frag.isEmpty())
        return std::nullopt;
    WikiParts out;
    out.target = target.toString();
    out.display = display;
    if (frag.startsWith(u'^'))
        out.blockId = frag.sliced(1).trimmed().toString();
    else
        out.heading = frag.toString();
    return out;
}

std::optional<MdLinkParts> parseMdLink(QStringView s, qsizetype p)
{
    const qsizetype n = s.size();
    const qsizetype limit = std::min(n, p + 1 + MaxLinkText);
    int depth = 1;
    qsizetype j = p + 1;
    for (; j < limit; ++j) {
        const QChar c = s[j];
        if (c == u'\\') {
            ++j;
            continue;
        }
        if (c == u'[') {
            ++depth;
        } else if (c == u']') {
            if (--depth == 0)
                break;
        }
    }
    if (depth != 0 || j >= limit || charAt(s, j + 1) != u'(')
        return std::nullopt;
    MdLinkParts out;
    out.textFrom = p + 1;
    out.textTo = j;
    j = skipBlanks(s, j + 2);
    if (charAt(s, j) == u'<') {
        out.destFrom = j + 1;
        qsizetype k = out.destFrom;
        for (; k < n && k - out.destFrom < MaxDest; ++k) {
            const QChar c = s[k];
            if (c == u'>' || c == u'\n' || c == u'<')
                break;
        }
        if (charAt(s, k) != u'>')
            return std::nullopt;
        out.destTo = k;
        out.angle = true;
        j = k + 1;
    } else {
        out.destFrom = j;
        int parens = 0;
        while (j < n && j - out.destFrom < MaxDest) {
            const QChar c = s[j];
            if (c == u' ' || c == u'\t' || isLineBreak(c))
                break;
            if (c == u'\\') {
                j += 2;
                continue;
            }
            if (c == u'(') {
                ++parens;
            } else if (c == u')') {
                if (parens == 0)
                    break;
                --parens;
            }
            ++j;
        }
        j = std::min(j, n);
        out.destTo = j;
    }
    j = skipBlanks(s, j);
    const QChar q = charAt(s, j);
    if (q == u'"' || q == u'\'' || (q == u'(' && j > out.destTo)) {
        // Bounded search: an unclosed title must not make every '[' scan to the end of the text.
        const QStringView window = s.sliced(j + 1, std::min(MaxDest, n - j - 1));
        const qsizetype found = window.indexOf(q == u'(' ? QChar(u')') : q);
        if (found < 0)
            return std::nullopt;
        j = skipBlanks(s, j + 1 + found + 1);
    }
    if (charAt(s, j) != u')')
        return std::nullopt;
    out.end = j + 1;
    return out;
}

bool hasUrlScheme(QStringView s)
{
    // ^[A-Za-z][A-Za-z0-9+.-]*:  but not a Windows drive (^[A-Za-z]:[\\/])
    if (s.isEmpty() || !isAsciiLetter(s[0]))
        return false;
    qsizetype i = 1;
    while (i < s.size() && (isAsciiLetter(s[i]) || isAsciiDigit(s[i]) || s[i] == u'+' || s[i] == u'.' || s[i] == u'-'))
        ++i;
    if (charAt(s, i) != u':')
        return false;
    if (i == 1 && (charAt(s, 2) == u'\\' || charAt(s, 2) == u'/'))
        return false;
    return true;
}

MdDestination parseMdDestination(QStringView rawDest)
{
    QString unescaped;
    unescaped.reserve(rawDest.size());
    for (qsizetype i = 0; i < rawDest.size(); ++i) {
        if (rawDest[i] == u'\\' && i + 1 < rawDest.size() && isAsciiPunct(rawDest[i + 1]))
            ++i;
        unescaped += rawDest[i];
    }
    const QStringView raw = QStringView(unescaped).trimmed();
    MdDestination out;
    if (hasUrlScheme(raw)) {
        out.target = raw.toString();
        return out;
    }
    const qsizetype hash = raw.indexOf(u'#');
    out.target = safeDecode(hash < 0 ? raw : raw.first(hash));
    if (hash >= 0) {
        out.hasFragment = true;
        const QString frag = safeDecode(raw.sliced(hash + 1));
        if (frag.startsWith(u'^')) {
            out.blockId = frag.sliced(1);
        } else {
            out.heading = frag;
        }
    }
    return out;
}

void lexInline(QStringView orig, QStringView masked, const std::function<void(const InlineItem &)> &visit)
{
    const QStringView s = masked;
    const qsizetype n = s.size();
    bool trigger = false;
    for (const QChar c : s) {
        if (c == u'[' || c == u'(' || c == u'<' || c == u'#' || c == u'h' || c == u'H') {
            trigger = true;
            break;
        }
    }
    if (!trigger)
        return;
    ScanState st;

    const auto wiki = [&](qsizetype p, qsizetype start, InlineKind kind) -> qsizetype {
        const qsizetype close = wikiCloseAt(s, p, st);
        if (close < 0)
            return -1;
        const QStringView inner = orig.sliced(p + 2, close - p - 2);
        if (inner.contains(u"[["))
            return -1;
        auto parts = parseWikiInner(inner);
        if (!parts)
            return -1;
        InlineItem item;
        item.kind = kind;
        item.from = start;
        item.to = close + 2;
        item.wiki = std::move(*parts);
        visit(item);
        return close + 2;
    };

    const auto mdLink = [&](qsizetype p, qsizetype start) -> qsizetype {
        if (charAt(s, p + 1) == u'^')
            return -1; // footnote reference
        const qsizetype rb = nextCached(s, p + 1, u"]", st.bracketClose);
        if (rb < 0 || rb - p > MaxLinkText)
            return -1;
        const auto parts = parseMdLink(s, p);
        if (!parts)
            return -1;
        MdDestination dest = parseMdDestination(orig.sliced(parts->destFrom, parts->destTo - parts->destFrom));
        if (dest.target.isEmpty() && dest.heading.isEmpty() && dest.blockId.isEmpty())
            return parts->end;
        InlineItem item;
        item.kind = start < p ? InlineKind::Image : InlineKind::Markdown;
        item.from = start;
        item.to = parts->end;
        item.md = *parts;
        item.dest = std::move(dest);
        visit(item);
        return parts->end;
    };

    qsizetype i = 0;
    while (i < n) {
        const QChar c = s[i];
        qsizetype end = -1;
        switch (c.unicode()) {
        case u'\\': i += 2; continue;
        case u'!': {
            const QChar c1 = charAt(s, i + 1);
            if (c1 == u'[') {
                end = charAt(s, i + 2) == u'[' ? wiki(i + 1, i, InlineKind::Embed) : mdLink(i + 1, i);
            } else if (c1 == u'(' && charAt(s, i + 2) == u'(') {
                const qsizetype e = blockRefEnd(s, i + 1);
                if (e >= 0) {
                    InlineItem item;
                    item.kind = InlineKind::BlockEmbed;
                    item.from = i;
                    item.to = e;
                    item.target = s.sliced(i + 3, e - i - 5).toString();
                    visit(item);
                    end = e;
                }
            }
            break;
        }
        case u'[': end = charAt(s, i + 1) == u'[' ? wiki(i, i, InlineKind::Wiki) : mdLink(i, i); break;
        case u'(':
            if (charAt(s, i + 1) == u'(') {
                const qsizetype e = blockRefEnd(s, i);
                if (e >= 0) {
                    InlineItem item;
                    item.kind = InlineKind::BlockRef;
                    item.from = i;
                    item.to = e;
                    item.target = s.sliced(i + 2, e - i - 4).toString();
                    visit(item);
                    end = e;
                }
            }
            break;
        case u'<': {
            qsizetype e = autolinkEnd(s, i);
            QString target;
            if (e >= 0) {
                target = orig.sliced(i + 1, e - i - 2).toString();
            } else {
                e = emailAutolinkEnd(s, i, st);
                if (e >= 0)
                    target = u"mailto:"_s + orig.sliced(i + 1, e - i - 2);
            }
            if (e >= 0) {
                InlineItem item;
                item.kind = InlineKind::Autolink;
                item.from = i;
                item.to = e;
                item.target = target;
                visit(item);
                end = e;
            }
            break;
        }
        case u'h':
        case u'H':
            if (i == 0 || isWs(s[i - 1]) || isUrlPreceder(s[i - 1])) {
                const qsizetype e = bareUrlEnd(s, i);
                if (e >= 0) {
                    InlineItem item;
                    item.kind = InlineKind::Url;
                    item.from = i;
                    item.to = e;
                    item.target = orig.sliced(i, e - i).toString();
                    visit(item);
                    end = e;
                }
            }
            break;
        case u'#': {
            if (i != 0 && s[i - 1] != u'(' && !isWs(s[i - 1]))
                break;
            if (charAt(s, i + 1) == u'[' && charAt(s, i + 2) == u'[') {
                const qsizetype close = wikiCloseAt(s, i + 1, st);
                if (close >= 0) {
                    const QStringView name = orig.sliced(i + 3, close - i - 3).trimmed();
                    if (!name.isEmpty() && !name.contains(u'[') && !name.contains(u']') && !name.contains(u'|')) {
                        InlineItem item;
                        item.kind = InlineKind::Tag;
                        item.from = i;
                        item.to = close + 2;
                        item.target = nfc(name);
                        visit(item);
                        end = close + 2;
                    }
                }
                break;
            }
            const qsizetype bodyEnd = tagBodyEnd(s, i + 1);
            if (bodyEnd == i + 1)
                break;
            const QString name = cleanTagBody(orig.sliced(i + 1, bodyEnd - i - 1));
            if (name.isEmpty()) {
                end = bodyEnd;
                break;
            }
            // The range covers the written characters (NFC may change the name's length).
            qsizetype tagEnd = bodyEnd;
            while (tagEnd > i + 1 && s[tagEnd - 1] == u'/')
                --tagEnd;
            InlineItem item;
            item.kind = InlineKind::Tag;
            item.from = i;
            item.to = tagEnd;
            item.target = name;
            visit(item);
            end = tagEnd;
            break;
        }
        default: break;
        }
        i = end > i ? end : i + 1;
    }
}

Segments segmentsForText(QStringView text, qsizetype base, int line)
{
    Segments seg;
    int k = 0;
    for (const text::Line &l : text::splitLines(text)) {
        seg.pos.append(l.start);
        seg.offset.append(base + l.start);
        seg.line.append(line + k);
        ++k;
    }
    return seg;
}

namespace {

/// Position mapper with a moving cursor (queries are mostly increasing).
class PosMap {
public:
    explicit PosMap(const Segments &seg) : m_seg(seg) { }

    qsizetype offset(qsizetype p)
    {
        const qsizetype k = seek(p);
        return k < m_seg.offset.size() ? m_seg.offset[k] + (p - m_seg.pos[k]) : p;
    }

    int line(qsizetype p)
    {
        const qsizetype k = seek(p);
        return k < m_seg.line.size() ? m_seg.line[k] : 0;
    }

private:
    qsizetype seek(qsizetype p)
    {
        const QList<qsizetype> &pos = m_seg.pos;
        if (m_k >= pos.size() || pos[m_k] > p)
            m_k = 0;
        while (m_k + 1 < pos.size() && pos[m_k + 1] <= p)
            ++m_k;
        return m_k;
    }

    const Segments &m_seg;
    qsizetype m_k = 0;
};

} // namespace

ScanResult scanInline(QStringView orig, QStringView masked, const Segments &seg)
{
    ScanResult out;
    PosMap map(seg);
    lexInline(orig, masked, [&](const InlineItem &item) {
        if (item.kind == InlineKind::Tag) {
            out.tags.append(TagHit{item.target, map.offset(item.from), map.offset(item.to), map.line(item.from)});
            return;
        }
        Link link;
        link.line = map.line(item.from);
        link.from = int(map.offset(item.from));
        link.to = int(map.offset(item.to));
        switch (item.kind) {
        case InlineKind::Wiki:
        case InlineKind::Embed:
            link.kind = item.kind == InlineKind::Wiki ? LinkKind::Wiki : LinkKind::Embed;
            link.target = item.wiki.target;
            link.display = item.wiki.display;
            link.heading = item.wiki.heading;
            link.blockId = item.wiki.blockId;
            break;
        case InlineKind::Markdown:
        case InlineKind::Image:
            link.kind = LinkKind::Markdown;
            link.target = item.dest.target;
            link.heading = item.dest.heading;
            link.blockId = item.dest.blockId;
            link.display = orig.sliced(item.md.textFrom, item.md.textTo - item.md.textFrom).trimmed().toString();
            break;
        case InlineKind::BlockRef:
        case InlineKind::BlockEmbed:
            link.kind = item.kind == InlineKind::BlockRef ? LinkKind::BlockRef : LinkKind::BlockEmbed;
            link.target = item.target;
            link.blockId = item.target;
            break;
        case InlineKind::Url:
        case InlineKind::Autolink:
            link.kind = LinkKind::Url;
            link.target = item.target;
            break;
        case InlineKind::Tag: break;
        }
        out.links.append(std::move(link));
    });
    return out;
}

QString tagKey(QStringView tag)
{
    return nfc(tag).toLower();
}

} // namespace detail

QString maskInlineCode(QStringView text)
{
    return maskInline(text);
}

QStringList uniqueTags(const QStringList &names)
{
    QSet<QString> seen;
    QStringList out;
    for (const QString &name : names) {
        const QString key = name.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append(name);
    }
    return out;
}

QList<TagSpan> findTags(QStringView text)
{
    const QString masked = maskInline(text);
    QList<TagSpan> out;
    lexInline(text, masked, [&out](const InlineItem &item) {
        if (item.kind == InlineKind::Tag)
            out.append(TagSpan{item.target, item.from, item.to});
    });
    return out;
}

InlineScan extractInline(QStringView text, qsizetype baseOffset, int line)
{
    const QString masked = maskInline(text);
    ScanResult res = scanInline(text, masked, segmentsForText(text, baseOffset, line));
    InlineScan out;
    out.links = std::move(res.links);
    QStringList names;
    for (const TagHit &t : res.tags)
        names.append(t.name);
    out.tags = uniqueTags(names);
    return out;
}

} // namespace brulion::markdown
