// Internal inline lexer of the markdown module, shared by parseNote (links/tags) and the
// InlineRenderer, so both recognise exactly the same constructs.
//
// The lexer is a single left-to-right pass over a *masked* string (inline code and comments
// blanked) with values read from the original string at the same positions. Constructs are
// matched with priority by position, so a tag inside a link or a URL is never reported. Work
// per position is bounded, so the scan is linear in practice.
#pragma once

#include "markdown/lines_p.h"
#include "types.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::markdown::detail {

/// Parts of a wiki link's inner text `Target#Heading|Display` / `Target#^id`.
struct WikiParts {
    QString target;
    QString display;
    QString heading;
    QString blockId;
};
std::optional<WikiParts> parseWikiInner(QStringView inner);

/// Parts of a Markdown inline link `[text](dest "title")` (positions in the scanned string).
struct MdLinkParts {
    qsizetype textFrom = 0;
    qsizetype textTo = 0;
    qsizetype destFrom = 0;
    qsizetype destTo = 0;
    bool angle = false; ///< destination written as `<…>`
    qsizetype end = 0;  ///< position after the closing `)`
};
std::optional<MdLinkParts> parseMdLink(QStringView s, qsizetype p);

/// A Markdown link destination split into target / heading / block id (escapes removed,
/// percent-decoded for relative paths; URLs with a scheme are kept as written).
struct MdDestination {
    QString target;
    QString heading;
    QString blockId;
    bool hasFragment = false;
};
MdDestination parseMdDestination(QStringView rawDest);

/// True when `s` starts with a URL scheme ("https:", "mailto:") that is not a Windows drive.
bool hasUrlScheme(QStringView s);

enum class InlineKind {
    Wiki,       ///< [[Page#h|alias]]
    Embed,      ///< ![[Page]]
    Markdown,   ///< [text](dest)
    Image,      ///< ![alt](dest)
    BlockRef,   ///< ((id))
    BlockEmbed, ///< !((id))
    Url,        ///< bare https://… URL
    Autolink,   ///< <https://…> or <mail@example.com> (target gets "mailto:")
    Tag,        ///< #tag / #[[multi word]]
};

/// One recognised construct; positions are in the scanned string.
struct InlineItem {
    InlineKind kind = InlineKind::Wiki;
    qsizetype from = 0;
    qsizetype to = 0;
    WikiParts wiki;     ///< Wiki / Embed
    MdLinkParts md;     ///< Markdown / Image
    MdDestination dest; ///< Markdown / Image
    QString target;     ///< Url / Autolink: the URL; BlockRef/BlockEmbed: id; Tag: name (NFC)
};

/// Scan `masked` (with `orig` of the same length for values) and call `visit` for each
/// construct in order. Markdown links with an empty destination are skipped (not reported).
void lexInline(QStringView orig, QStringView masked, const std::function<void(const InlineItem &)> &visit);

/// Maps positions of a scanned string back to the document: segment k starts at pos[k] in the
/// scanned string, at document offset offset[k] and on document line line[k].
struct Segments {
    QList<qsizetype> pos;
    QList<qsizetype> offset;
    QList<int> line;
};
Segments segmentsForText(QStringView text, qsizetype base = 0, int line = 0);

/// A tag occurrence (name without '#', NFC) with document offsets.
struct TagHit {
    QString name;
    qsizetype from = 0;
    qsizetype to = 0;
    int line = 0;
};

struct ScanResult {
    QList<Link> links;
    QList<TagHit> tags;
};

/// Links and tags of `masked` mapped to document positions through `seg`.
ScanResult scanInline(QStringView orig, QStringView masked, const Segments &seg);

/// Lower-cased NFC tag key.
QString tagKey(QStringView tag);

} // namespace brulion::markdown::detail
