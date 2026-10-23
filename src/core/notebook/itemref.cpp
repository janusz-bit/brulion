#include "notebook/itemref.h"

#include "markdown/parse.h"
#include "markdown/tokens.h"
#include "text/text.h"

#include <QRegularExpression>

#include <cstdlib>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

constexpr qsizetype kMaxRefLength = 4096;
constexpr int kSearchRadius = 50;

bool isHex(QStringView s)
{
    for (const QChar c : s) {
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f')))
            return false;
    }
    return true;
}

/// A block id: `^id` ids are [A-Za-z0-9-]; Logseq ids are UUIDs (hex and dashes).
bool isBlockId(QStringView id)
{
    if (id.isEmpty() || id.size() > 128)
        return false;
    for (const QChar c : id) {
        if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-'
              || c == u'_'))
            return false;
    }
    return true;
}

} // namespace

QString lineHash(QStringView lineText)
{
    return text::fnv1a64Hex(lineText.toUtf8()).left(8);
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
    ItemRef ref;
    // ((id)) and ^id: a block anywhere.
    if (t.startsWith(u"((") && t.endsWith(u"))")) {
        const QStringView id = t.size() > 4 ? t.mid(2, t.size() - 4).trimmed() : QStringView();
        if (!isBlockId(id))
            return std::nullopt;
        ref.blockId = id.toString();
        return ref;
    }
    if (t.startsWith(u'^')) {
        const QStringView id = t.mid(1);
        if (!isBlockId(id))
            return std::nullopt;
        ref.blockId = id.toString();
        return ref;
    }
    // page#^id, page#L12[@hash]; anything else is a page name / path.
    const qsizetype hash = t.lastIndexOf(u'#');
    if (hash < 0) {
        ref.page = t.toString();
        return ref;
    }
    const QStringView page = t.left(hash).trimmed();
    const QStringView fragment = t.mid(hash + 1);
    if (fragment.startsWith(u'^')) {
        const QStringView id = fragment.mid(1);
        if (!isBlockId(id) || page.isEmpty())
            return std::nullopt;
        ref.page = page.toString();
        ref.blockId = id.toString();
        return ref;
    }
    // "#L12": a line ("#L", "#L-1", "#L+1" are malformed line refs); "#Lato" is part of a name.
    const bool lineRef = fragment.startsWith(u'L')
                         && (fragment.size() == 1 || fragment.at(1).isDigit() || fragment.at(1) == u'-'
                             || fragment.at(1) == u'+' || fragment.at(1) == u'@');
    if (lineRef) {
        QStringView rest = fragment.mid(1);
        QStringView digest;
        if (const qsizetype at = rest.indexOf(u'@'); at >= 0) {
            digest = rest.mid(at + 1);
            rest = rest.left(at);
            if (digest.size() != 8 || !isHex(digest))
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
    // "#" inside a name ("C# notes.md", "Plan#Lato"): a page name.
    ref.page = t.toString();
    return ref;
}

ResolvedLine resolve(const QString &content, const ItemRef &ref, const VaultConfig &config)
{
    ResolvedLine out;
    if (ref.hasBlock()) {
        const ParsedNote note = markdown::parseNote(QString(), content, config);
        const int index = note.blockIds.value(ref.blockId, -1);
        if (index < 0 || index >= note.blocks.size())
            return out;
        out.status = ResolvedLine::Ok;
        out.line = note.blocks.at(index).line;
        return out;
    }
    if (!ref.hasLine())
        return out;
    const QList<text::Line> lines = text::splitLines(content);
    // The final empty "line" after a trailing terminator is no line of the note.
    qsizetype count = lines.size();
    if (count > 0 && lines.last().text.isEmpty() && lines.last().eol.isEmpty() && count > 1)
        --count;
    if (ref.lineHash.isEmpty()) {
        if (ref.line < count) {
            out.status = ResolvedLine::Ok;
            out.line = ref.line;
        }
        return out;
    }
    if (ref.line < count && lineHash(lines.at(ref.line).text) == ref.lineHash) {
        out.status = ResolvedLine::Ok;
        out.line = ref.line;
        return out;
    }
    // Lines inserted or removed above: the same text nearby (the nearest wins, above first).
    for (int d = 1; d <= kSearchRadius; ++d) {
        for (const int candidate : {ref.line - d, ref.line + d}) {
            if (candidate >= 0 && candidate < count && lineHash(lines.at(candidate).text) == ref.lineHash) {
                out.status = ResolvedLine::Moved;
                out.line = candidate;
                return out;
            }
        }
    }
    // The ref was taken from another text (the line changed, or the note got shorter).
    out.status = ResolvedLine::Stale;
    out.line = ref.line < count ? ref.line : -1;
    return out;
}

} // namespace brulion::notebook
