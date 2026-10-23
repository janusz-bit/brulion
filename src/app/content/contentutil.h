// Helpers shared by the content models (journals, pages, backlinks, tags, search, history):
// ISO dates for QML, the UI locale, the renderer palette from QML colours, rendering an indexed
// block as escaped rich text, search-term highlighting positions, the pure "turn a plain mention
// into a [[link]]" edit, wiki links for "Kopiuj odnośnik", human sizes and times.
// GUI-free (QtCore + core modules). Namespace brulion::content.
#pragma once

#include "dates/names.h"
#include "markdown/inlinerenderer.h"
#include "types.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace brulion {
class VaultIndex;
}

namespace brulion::content {

/// Strict "YYYY-MM-DD" -> QDate (invalid otherwise).
QDate parseIsoDate(const QString &text);
/// QDate -> "YYYY-MM-DD" ("" when invalid).
QString isoDate(QDate date);
/// "en" -> English, anything else Polish (the default UI language).
dates::UiLocale uiLocale(const QString &locale);

/// Renderer palette from a QML map of colours (keys as BlockEditor.rendererPalette: link,
/// unresolved, muted, codeBackground, highlightBackground, <chip>Fg / <chip>Bg); missing keys keep
/// the light defaults.
markdown::Palette paletteFromMap(const QVariantMap &map);

/// Render context resolving links and block references against `index`, seen from `fromPath`.
/// The leading task marker is a clickable `task:toggle` glyph.
markdown::RenderContext renderContext(const VaultIndex *index, const VaultPath &fromPath, QDate today,
                                      dates::UiLocale locale, const markdown::Palette &palette);

/// One block of an indexed note as escaped rich text (the block's own lines, not its children):
/// the body the editor shows (list marker and indentation removed) rendered by
/// markdown::InlineRenderer::renderBlock. "" when the line holds no block.
QString renderBlockAt(const QString &content, int line, const markdown::RenderContext &context);

/// Readable one-line text of inline Markdown (lists, accessibility): wiki links and embeds show
/// their display text or target ("Strona › Nagłówek" for a heading link), Markdown links their
/// text, block references the referenced text (via `index`, else "((id))"), emphasis and code
/// markers are dropped, whitespace collapsed. Tags stay ("#praca").
QString plainText(const QString &markdown, const VaultIndex *index = nullptr);

/// Whitespace collapsed to single spaces, cut at `max` characters with "…".
QString oneLine(const QString &text, int max = 300);

/// Words of a search query for highlighting: split on whitespace, quotes and search operators
/// removed, folded (lower case, no diacritics), empty and duplicate terms dropped.
QStringList searchTerms(const QString &query);
/// UTF-16 positions (ascending, for TextHelper.markMatches) of the characters of `text` that
/// match `terms` (from searchTerms()) at the start of a word, case- and diacritic-insensitively
/// ("zol" marks "Żół" in "Żółw"); with `wordStart` false anywhere in a word (list filters).
QList<int> matchPositions(const QString &text, const QStringList &terms, bool wordStart = true);
/// Every folded term of `terms` occurs in `text` (case- and diacritic-insensitive substring).
bool containsAll(const QString &text, const QStringList &terms);
/// matchPositions() for the whole-word, case-insensitive occurrences of `names` (a page's title
/// and aliases) — the mention in an unlinked reference's excerpt.
QList<int> mentionPositions(const QString &text, const QStringList &names);

/// A plain-text mention turned into a wiki link (see linkMention()).
struct MentionEdit {
    QString content;      ///< the new content (unchanged when nothing was found)
    qsizetype from = -1;  ///< replaced range in the old content, -1 when nothing was found
    qsizetype to = -1;
    QString mention;      ///< the text that was linked, as written
    QString replacement;  ///< what replaced it ("[[Dom]]", "[[Projekty/Dom|dom]]")

    bool ok() const { return from >= 0; }
};

/// Turn the first plain mention of one of `names` (title, aliases; whole words, case-insensitive,
/// longest name first at the same position) on line `line` (0-based) of `content` into a wiki
/// link. Never inside inline code, links, URLs or tags. `linkFor(mention)` gives the link text
/// for the mention as written (default: "[[<mention>]]"). Everything else — other lines, line
/// endings, the rest of the line — is kept byte for byte.
MentionEdit linkMention(const QString &content, int line, const QStringList &names,
                        const std::function<QString(const QString &mention)> &linkFor = {});

/// Link text for a page: "[[Dom]]", or "[[Projekty/Dom]]" when "Dom" alone would resolve to
/// another page (or not at all), as seen from `fromPath` ("" = the vault root).
QString wikiLinkFor(const VaultIndex *index, const VaultPath &path, const VaultPath &fromPath = {});
/// The link a mention should become so that it resolves to `target` from `fromPath`:
/// "[[<mention>]]" when that already resolves there, else "[[<target without .md>|<mention>]]".
QString mentionLink(const VaultIndex *index, const QString &mention, const VaultPath &target, const VaultPath &fromPath);

/// "512 B", "1,2 kB", "3,4 MB" (Polish decimal comma; "1.2 kB" in English).
QString humanSize(qint64 bytes, dates::UiLocale locale);
/// File time (ms since epoch, local) for lists: "dziś 09:12", "wczoraj 18:00", "pt 17 paź",
/// "17 paź 2025" — dates::formatHuman relative to `today`. "" for 0.
QString humanTime(qint64 msecsSinceEpoch, QDate today, dates::UiLocale locale);

/// Canonical tag key: trimmed, without a leading '#' (or "#[[…]]" brackets), NFC, lower case
/// ("#Praca" -> "praca").
QString normalizeTag(const QString &tag);
/// Tags given to the page itself (frontmatter `tags:` / `tag:`, Logseq `tags::` page properties),
/// as canonical keys in the order written — not the tags of its blocks.
QStringList pageTags(const ParsedNote &note);

/// Case-insensitive comparison of names in Polish alphabetical order with digit runs by value,
/// then code units so the order is total — text::compareNames(), the order of the sidebar's tree
/// too (the same on every platform: no ICU).
int compareNames(const QString &a, const QString &b);

} // namespace brulion::content
