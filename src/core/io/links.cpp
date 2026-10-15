#include "io/links_p.h"

#include "markdown/parse.h"

#include <QRegularExpression>
#include <QStringDecoder>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

struct MarkdownParts {
    bool image = false;
    QString label;
    QString destination;
    QString title;
};

QChar charAt(QStringView s, qsizetype i)
{
    return i >= 0 && i < s.size() ? s[i] : QChar();
}

/// Split the source of a Markdown link / image (`[label](dest "title")`).
std::optional<MarkdownParts> parseMarkdownSource(QStringView s)
{
    MarkdownParts p;
    qsizetype i = 0;
    if (charAt(s, 0) == u'!') {
        p.image = true;
        i = 1;
    }
    if (charAt(s, i) != u'[' || !s.endsWith(u')'))
        return std::nullopt;
    int depth = 0;
    qsizetype close = -1;
    for (qsizetype j = i; j < s.size(); ++j) {
        const QChar c = s[j];
        if (c == u'\\') {
            ++j;
        } else if (c == u'[') {
            ++depth;
        } else if (c == u']' && --depth == 0) {
            close = j;
            break;
        }
    }
    if (close < 0 || charAt(s, close + 1) != u'(')
        return std::nullopt;
    p.label = s.sliced(i + 1, close - i - 1).toString();
    qsizetype k = close + 2;
    while (k < s.size() && s[k].isSpace())
        ++k;
    const qsizetype destFrom = k;
    if (charAt(s, k) == u'<') {
        const qsizetype gt = s.indexOf(u'>', k);
        if (gt < 0)
            return std::nullopt;
        k = gt + 1;
    } else {
        int parens = 0;
        while (k < s.size() - 1) {
            const QChar c = s[k];
            if (c == u'\\') {
                k += 2;
                continue;
            }
            if (c.isSpace())
                break;
            if (c == u'(') {
                ++parens;
            } else if (c == u')') {
                if (parens == 0)
                    break;
                --parens;
            }
            ++k;
        }
        k = std::min(k, s.size() - 1);
    }
    p.destination = s.sliced(destFrom, k - destFrom).toString();
    p.title = s.sliced(k, s.size() - 1 - k).trimmed().toString();
    return p;
}

bool hasScheme(QStringView target)
{
    static const QRegularExpression re(u"^[A-Za-z][A-Za-z0-9+.-]{1,31}:"_s);
    return re.matchView(target).hasMatch();
}

} // namespace

QString replaceLinks(const QString &content, const LinkReplacer &replace)
{
    const ParsedNote note = markdown::parseNote(u"note.md"_s, content);
    QString out;
    qsizetype last = 0;
    bool changed = false;
    for (const Link &l : note.links) {
        if (l.kind == LinkKind::Url || l.from < last || l.from >= l.to || l.to > content.size())
            continue;
        FoundLink found;
        found.kind = l.kind;
        found.target = l.target;
        found.heading = l.heading;
        found.blockId = l.blockId;
        found.display = l.display;
        found.raw = content.sliced(l.from, l.to - l.from);
        if (l.kind == LinkKind::Markdown) {
            const auto parts = parseMarkdownSource(found.raw);
            if (!parts)
                continue;
            found.image = parts->image;
            found.display = parts->label;
            found.destination = parts->destination;
            found.title = parts->title;
            found.hasUrlScheme = hasScheme(l.target);
        }
        const std::optional<QString> replacement = replace(found);
        if (!replacement || *replacement == found.raw)
            continue;
        out += QStringView(content).sliced(last, l.from - last);
        out += *replacement;
        last = l.to;
        changed = true;
    }
    if (!changed)
        return content;
    out += QStringView(content).sliced(last);
    return out;
}

QString wikiLink(const QString &target, const QString &heading, const QString &blockId, const QString &display,
                 bool embed)
{
    QString out = embed ? u"![["_s : u"[["_s;
    out += target;
    if (!heading.isEmpty())
        out += u'#' + heading;
    else if (!blockId.isEmpty())
        out += u"#^"_s + blockId;
    if (!display.isEmpty())
        out += u'|' + display;
    return out + u"]]"_s;
}

QString markdownLink(const QString &label, const QString &destination, bool image, const QString &title)
{
    QString out = image ? u"!["_s : u"["_s;
    out += label + u"]("_s + destination;
    if (!title.isEmpty())
        out += u' ' + title;
    return out + u')';
}

QString encodeLinkPath(QStringView path)
{
    static const QString special = u" ()<>%#?\\[]\"`{}|^"_s;
    QString out;
    out.reserve(path.size() + 8);
    for (const QChar c : path) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u == 0x7f || special.contains(c)) {
            out += u'%' + QString::number(u, 16).toUpper().rightJustified(2, u'0');
        } else {
            out += c;
        }
    }
    return out;
}

QString percentDecode(QStringView text)
{
    if (!text.contains(u'%'))
        return text.toString();
    const auto hex = [](QChar c) -> int {
        const char16_t u = c.unicode();
        if (u >= u'0' && u <= u'9')
            return u - u'0';
        if (u >= u'a' && u <= u'f')
            return u - u'a' + 10;
        if (u >= u'A' && u <= u'F')
            return u - u'A' + 10;
        return -1;
    };
    QByteArray bytes;
    const QByteArray utf8 = text.toUtf8();
    for (qsizetype i = 0; i < utf8.size(); ++i) {
        if (utf8[i] == '%' && i + 2 < utf8.size()) {
            const int hi = hex(QChar(char16_t(static_cast<unsigned char>(utf8[i + 1]))));
            const int lo = hex(QChar(char16_t(static_cast<unsigned char>(utf8[i + 2]))));
            if (hi >= 0 && lo >= 0 && (hi || lo)) { // %00 stays literal
                bytes += char(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        bytes += utf8[i];
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString decoded = decoder(bytes);
    return decoder.hasError() ? text.toString() : decoded;
}

QString relativePath(const QString &fromDir, const QString &to)
{
    const QStringList from = fromDir.split(u'/', Qt::SkipEmptyParts);
    const QStringList target = to.split(u'/', Qt::SkipEmptyParts);
    qsizetype common = 0;
    while (common < from.size() && common + 1 < target.size() && from[common] == target[common])
        ++common;
    QStringList parts;
    for (qsizetype k = common; k < from.size(); ++k)
        parts.append(u".."_s);
    for (qsizetype k = common; k < target.size(); ++k)
        parts.append(target[k]);
    return parts.join(u'/');
}

QString markdownFragment(const QString &heading, const QString &blockId)
{
    if (!heading.isEmpty())
        return u'#' + encodeLinkPath(heading);
    if (!blockId.isEmpty())
        return u"#^"_s + blockId;
    return {};
}

} // namespace brulion::io::detail
