#include "notebook/itemref.h"

#include "markdown/parse.h"
#include "markdown/tokens.h"
#include "text/text.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

constexpr qsizetype kMaxRefLength = 4096;
constexpr qsizetype kMaxBlockIdLength = 128;
constexpr qsizetype kLineHashLength = 8;
constexpr int kSearchRadius = 50;

bool isHex(QStringView s)
{
    return std::ranges::all_of(s, [](QChar c) { return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); });
}

/// A block id: `^id` ids are [A-Za-z0-9_-]; Logseq ids are UUIDs (hex and dashes).
bool isBlockId(QStringView id)
{
    const auto allowed = [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-'
               || c == u'_';
    };
    return !id.isEmpty() && id.size() <= kMaxBlockIdLength && std::ranges::all_of(id, allowed);
}

/// A reference to the block `id` of `page` ("": anywhere); nullopt for a malformed id.
std::optional<ItemRef> blockRef(QStringView page, QStringView id)
{
    if (!isBlockId(id))
        return std::nullopt;
    ItemRef ref;
    ref.page = page.toString();
    ref.blockId = id.toString();
    return ref;
}

} // namespace

QString lineHash(QStringView lineText)
{
    return text::fnv1a64Hex(lineText.toUtf8()).left(kLineHashLength);
}

QString formatLineRef(const VaultPath &path, int line, QStringView lineText)
{
    return path + u"#L"_s + QString::number(line + 1) + u'@' + lineHash(lineText);
}

QString formatBlockRef(const VaultPath &path, const QString &blockId)
{
    if (path.isEmpty())
        return u'^' + blockId;
    return path + u"#^"_s + blockId;
}

QString formatRef(const ItemRef &ref)
{
    if (ref.hasBlock())
        return formatBlockRef(ref.page, ref.blockId);
    QString out = ref.page;
    if (ref.hasLine()) {
        out += u"#L"_s + QString::number(ref.line + 1);
        if (!ref.lineHash.isEmpty())
            out += u'@' + ref.lineHash;
    }
    return out;
}

bool looksLikeItemRef(QStringView text)
{
    const QStringView t = text.trimmed();
    if (t.startsWith(u'^') || (t.startsWith(u"((") && t.endsWith(u"))")))
        return true;
    static const QRegularExpression fragment(u"#(L\\d|\\^)"_s);
    return fragment.matchView(t).hasMatch();
}

std::optional<ItemRef> parseItemRef(QStringView raw)
{
    if (raw.size() > kMaxRefLength || raw.contains(QChar(u'\0')))
        return std::nullopt;
    const QStringView t = raw.trimmed();
    if (t.isEmpty())
        return std::nullopt;
    // ((id)) and ^id: a block anywhere ("((" and "))" cannot overlap: at least four characters).
    if (t.startsWith(u"((") && t.endsWith(u"))"))
        return blockRef({}, t.sliced(2, t.size() - 4).trimmed());
    if (t.startsWith(u'^'))
        return blockRef({}, t.sliced(1));
    // page#^id, page#L12[@hash]; anything else is a page name / path.
    ItemRef ref;
    const qsizetype hash = t.lastIndexOf(u'#');
    if (hash < 0) {
        ref.page = t.toString();
        return ref;
    }
    const QStringView page = t.first(hash).trimmed();
    const QStringView fragment = t.sliced(hash + 1);
    if (fragment.startsWith(u'^'))
        return page.isEmpty() ? std::nullopt : blockRef(page, fragment.sliced(1));
    // "#L12": a line ("#L", "#L-1", "#L+1" are malformed line refs); "#Lato" is part of a name.
    const bool lineRef = fragment.startsWith(u'L')
                         && (fragment.size() == 1 || fragment.at(1).isDigit() || fragment.at(1) == u'-'
                             || fragment.at(1) == u'+' || fragment.at(1) == u'@');
    if (!lineRef) {
        // "#" inside a name ("C# notes.md", "Plan#Lato"): a page name.
        ref.page = t.toString();
        return ref;
    }
    QStringView rest = fragment.sliced(1);
    QStringView digest;
    if (const qsizetype at = rest.indexOf(u'@'); at >= 0) {
        digest = rest.sliced(at + 1);
        rest = rest.first(at);
        if (digest.size() != kLineHashLength || !isHex(digest))
            return std::nullopt;
    }
    bool ok = false;
    const int number = rest.toInt(&ok);
    if (!ok || number < 1 || rest.startsWith(u'+') || page.isEmpty())
        return std::nullopt;
    ref.page = page.toString();
    ref.line = number - 1;
    ref.lineHash = digest.toString();
    return ref;
}

ResolvedLine resolve(const QString &content, const ItemRef &ref, const VaultConfig &config)
{
    if (ref.hasBlock()) {
        const ParsedNote note = markdown::parseNote(QString(), content, config);
        const int index = note.blockIds.value(ref.blockId, -1);
        if (index < 0 || index >= note.blocks.size())
            return {};
        return {ResolvedLine::Ok, note.blocks.at(index).line};
    }
    if (!ref.hasLine())
        return {};
    const QList<text::Line> lines = text::splitLines(content);
    // The final empty "line" after a trailing terminator is no line of the note.
    qsizetype count = lines.size();
    if (count > 1 && lines.last().text.isEmpty() && lines.last().eol.isEmpty())
        --count;
    // (qsizetype: the lines near a ref's line, which may be INT_MAX - 1, cannot overflow.)
    const auto holdsText = [&](qsizetype line) {
        return line >= 0 && line < count && (ref.lineHash.isEmpty() || lineHash(lines.at(line).text) == ref.lineHash);
    };
    if (holdsText(ref.line))
        return {ResolvedLine::Ok, ref.line};
    if (ref.lineHash.isEmpty())
        return {}; // no such line
    // Lines inserted or removed above: the same text nearby (the nearest wins, above first).
    for (qsizetype d = 1; d <= kSearchRadius; ++d) {
        for (const qsizetype candidate : {ref.line - d, ref.line + d}) {
            if (holdsText(candidate))
                return {ResolvedLine::Moved, int(candidate)};
        }
    }
    // The ref was taken from another text (the line changed, or the note got shorter).
    return {ResolvedLine::Stale, ref.line < count ? ref.line : -1};
}

} // namespace brulion::notebook
