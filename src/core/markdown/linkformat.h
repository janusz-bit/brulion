// Links to pages, headings and blocks as Brulion writes them (docs/SYNTAX.md §2 "Copied links"):
// the fragment that names a heading in `[[Page#Heading]]`, the one matcher of such fragments
// (link navigation, `![[Page#Heading]]` embeds and copying all use findHeading()), and the text
// of each kind of reference. Pure functions, GUI-free — the editor's "Kopiuj odnośnik", the
// command palette and future command-line tools share them. Part of the markdown module — see
// markdown/markdown.h for the module overview.
#pragma once

#include "types.h"

#include <QList>
#include <QString>
#include <QStringView>

namespace brulion::markdown {

/// The fragment `[[Page#…]]` names a heading by (SYNTAX §2): the heading's text as shown —
/// inline markup removed (`**`, `*`, `~~`, `==`, backticks; a link shows its alias, else its
/// target; a Markdown link its label; `#tag` and `#[[tag]]` their name) — without a trailing
/// block id (` ^abc123`), with the characters that split or end a wiki link or that Obsidian
/// drops from heading links replaced by a space (`#`, `|`, `^`, `:`, `[`, `]`, `\`, `%%`),
/// whitespace (tabs, no-break spaces) collapsed, trimmed and NFC-normalized. Letters (Polish
/// ones too), digits, emoji and other punctuation stay as written. ATX markers (`## `, a closing
/// `##`) may be included: they go with the `#`. Only the first line of `headingText` counts.
/// "Zażółć **gęślą** jaźń" -> "Zażółć gęślą jaźń", "Plan #praca" -> "Plan praca",
/// "Q&A: test" -> "Q&A test", "C# i C++" -> "C i C++". Linear time; "" when nothing is left.
QString headingFragment(QStringView headingText);

/// The comparison key of a heading as written: headingFragment() folded (case, diacritics:
/// text::foldKey) — "Zażółć Gęślą" and "zazolc gesla" have the same key.
QString headingKey(QStringView headingText);

/// Does the link fragment `fragment` (`Heading`, an Obsidian heading path `Parent#Heading`, or
/// one written by hand with a `#` inside the heading's own text) name the heading `headingText`?
/// Its last `#` segment or the whole fragment has the heading's key (headingKey()). Heading paths
/// are checked against the parents by findHeading(); this compares one heading only.
bool headingMatches(QStringView fragment, QStringView headingText);

/// The heading of `headings` (in document order, Heading::text as written, Heading::level 1–6)
/// that the link fragment `fragment` names, or -1:
///   1. the first heading whose key is the whole fragment's (`[[P#C# i C++]]`, written by hand);
///   2. else, for an Obsidian heading path `A#B#C`, the first heading matching `C` whose
///      enclosing headings (each an earlier heading of a lower level) include `A` and `B` in that
///      order (levels may be skipped);
///   3. else the first heading matching the last segment.
/// Case- and diacritic-insensitive, markup and tag hashes ignored (headingKey()).
int findHeading(const QList<Heading> &headings, QStringView fragment);

/// The shortest fragment that names `headings[index]` and no other heading of `headings` (by
/// findHeading()'s rules): its own headingFragment() when no other heading has its key, else an
/// Obsidian heading path through its enclosing headings ("Rodzic#Nagłówek",
/// "Projekt#Rodzic#Nagłówek"). "" when none is unique (the same path twice), the heading has no
/// text, or the fragment would be longer than 300 characters — use a block id then.
QString uniqueHeadingFragment(const QList<Heading> &headings, int index);

/// What a copied reference points at and how (SYNTAX §2).
enum class ReferenceKind {
    PageLink,       ///< [[Page]]
    PageEmbed,      ///< ![[Page]]
    HeadingLink,    ///< [[Page#Heading]]
    HeadingEmbed,   ///< ![[Page#Heading]] (the section)
    BlockLink,      ///< [[Page#^id]] (Obsidian)
    BlockLinkEmbed, ///< ![[Page#^id]]
    BlockRef,       ///< ((id)) (Logseq; resolved in the whole notebook)
    BlockEmbed,     ///< !((id))
};

/// The text of a reference: `pageTarget` as a link names the page ("Dom", "Projekty/Dom",
/// "2026-10-22"; written as given), `fragment` a heading fragment (headingFragment(),
/// uniqueHeadingFragment(); written as given), `blockId` a block id without `^` (a generated one
/// or a Logseq UUID). A heading kind without a fragment, or a block kind without an id, falls
/// back to the page link / embed (a block embed `!((…))` to the page embed). An empty page target
/// gives the same-page forms `[[#Heading]]`, `[[#^id]]`; a page link or embed needs one ("").
QString formatReference(ReferenceKind kind, QStringView pageTarget, QStringView fragment = {},
                        QStringView blockId = {});

} // namespace brulion::markdown
