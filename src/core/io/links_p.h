// Link rewriting for imports and exports: find the wiki links, embeds, Markdown links / images
// and block references of a note (exactly those parseNote() reports — never in code, comments or
// HTML blocks) and splice replacements in. Internal to the io module.
#pragma once

#include "types.h"

#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::io::detail {

/// A link found in a note.
struct FoundLink {
    LinkKind kind = LinkKind::Wiki; ///< Wiki, Embed, Markdown, BlockRef or BlockEmbed (no Url)
    bool image = false;             ///< Markdown image `![alt](dest)`
    QString target;                 ///< wiki target / decoded Markdown path / block id
    QString heading;
    QString blockId;
    QString display;           ///< wiki alias (after `|`); Markdown: the label as written
    QString destination;       ///< Markdown: destination as written (still encoded)
    QString title;             ///< Markdown: title part as written (with quotes), empty when none
    QString raw;               ///< the whole link as written
    bool hasUrlScheme = false; ///< Markdown links to "https:", "mailto:", … (target = URL)
};

using LinkReplacer = std::function<std::optional<QString>(const FoundLink &link)>;

/// Replace links of `content`: `replace` returns the new source of a link or nullopt to keep it.
/// Everything else is preserved byte for byte.
QString replaceLinks(const QString &content, const LinkReplacer &replace);

/// `[[target#heading|display]]` / `![[target#^block]]`. Empty parts are left out.
QString wikiLink(const QString &target, const QString &heading = {}, const QString &blockId = {},
                 const QString &display = {}, bool embed = false);

/// `[label](destination "title")` / `![label](destination)`; `destination` is written as given.
QString markdownLink(const QString &label, const QString &destination, bool image = false, const QString &title = {});

/// Percent-encode a path for a Markdown link destination (spaces, parentheses, `<>`, `%`, `#`,
/// `?`, `\`, control characters); `/` and non-ASCII letters are kept.
QString encodeLinkPath(QStringView path);

/// Decode %XX sequences (UTF-8); invalid sequences stay as written.
QString percentDecode(QStringView text);

/// Relative path from folder `fromDir` to `to` ("a/b", "a/c/x.png" -> "../c/x.png").
QString relativePath(const QString &fromDir, const QString &to);

/// "#heading" / "#^id" fragment for Markdown destinations (encoded), empty when none.
QString markdownFragment(const QString &heading, const QString &blockId);

} // namespace brulion::io::detail
