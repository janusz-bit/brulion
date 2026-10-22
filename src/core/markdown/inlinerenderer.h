// InlineRenderer — renders a block body (inline Markdown of the Brulion dialect, docs/SYNTAX.md)
// to the Qt rich-text HTML subset shown by QML `Text { textFormat: Text.RichText }`. Part of the
// markdown module — see markdown/markdown.h for the module overview.
//
// Security: notes come from anywhere, so everything that is not recognised syntax is escaped —
// raw HTML in a note is shown as text, never interpreted. The output only contains <b>, <i>,
// <s>, <code>, <sup>, <sub>, <br/>, <span style>, <a href> and <hr/> (renderBlock) with attribute
// values built by the renderer. No <img> is ever emitted (embeds and images become placeholder
// links, the editor delegate shows the pictures), so rendering never loads anything.
//
// Links (handled by the QML delegate's onLinkActivated):
//   page:<target>[#<heading> | #^<id>]  wiki links, embeds, relative Markdown links
//                                        (target and fragment percent-encoded, as written)
//   tag:<name>                           #tags (percent-encoded, without '#')
//   block:<id>                           ((id)) block references and !((id)) block embeds
//   ext:<url>                            http(s)/mailto URLs only (other schemes stay text)
//   task:toggle                          the leading task checkbox (RenderContext::taskCheckbox)
//   reminder:<from>-<to>                 a ⏰ / ⏰! / (@…) chip: UTF-16 range of the token in the text
//                                        passed to render() / the body passed to renderBlock()
#pragma once

#include "dates/names.h"
#include "types.h"

#include <QDate>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>
#include <utility>

namespace brulion::markdown {

/// Text and background colour of a chip. Colours are CSS colour strings accepted by Qt rich
/// text, e.g. QColor::name(QColor::HexArgb) ("#AARRGGBB") or "#RRGGBB".
struct ChipColors {
    QString foreground;
    QString background;
};

/// Colours used by the renderer (defaults: the light theme of docs/UI.md / Theme.qml).
struct Palette {
    QString link = QStringLiteral("#2c6aa3");
    QString unresolved = QStringLiteral("#7a5ea3"); ///< links to pages that do not exist yet
    QString muted = QStringLiteral("#535a6a");      ///< property keys, Obsidian Tasks ids
    QString codeBackground = QStringLiteral("#f3eee2");
    QString highlightBackground = QStringLiteral("#61f2b33d"); ///< ==highlight==
    ChipColors tag{QStringLiteral("#2b5f8f"), QStringLiteral("#529fc3e6")};
    ChipColors due{QStringLiteral("#2b5f8f"), QStringLiteral("#479fc3e6")};
    ChipColors overdue{QStringLiteral("#b23c31"), QStringLiteral("#1fe2574c")};
    ChipColors scheduled{QStringLiteral("#6a4f93"), QStringLiteral("#1f7a5ea3")};
    ChipColors reminder{QStringLiteral("#7a4f00"), QStringLiteral("#3df2b33d")};
    ChipColors alarm{QStringLiteral("#1f2430"), QStringLiteral("#f2b33d")};
    ChipColors recurrence{QStringLiteral("#2a6e44"), QStringLiteral("#1f3f8f5a")};
    ChipColors done{QStringLiteral("#535a6a"), QStringLiteral("#0f1f2430")};
    ChipColors neutral{QStringLiteral("#535a6a"), QStringLiteral("#0f1f2430")};
};

/// Everything the renderer needs besides the text. All callbacks are optional.
struct RenderContext {
    /// Page link target (as written, e.g. "Projekty/Dom") -> resolved vault path, or nullopt
    /// when the page does not exist (the link gets Palette::unresolved). Unset: all resolved.
    std::function<std::optional<QString>(const QString &target)> resolvePage;
    /// Block id -> the referenced block's text (shown escaped), nullopt when unknown.
    std::function<std::optional<QString>(const QString &id)> blockText;
    /// Inline calculation `= expr` -> result text; an empty result shows the code as written.
    std::function<QString(const QString &expr)> inlineCalc;
    /// Text shown for a wiki link or page embed without an alias, from its target as written
    /// (`[[2026-10-13]]` -> "wt 13 paź", see journalLinkTitle()); nullopt / unset: the target as
    /// written. Display only — the link and its `page:` URL keep the target.
    std::function<std::optional<QString>(const QString &target)> linkTitle;
    QDate today; ///< for "dziś"/"jutro" and overdue due dates (invalid: absolute dates)
    dates::UiLocale locale = dates::UiLocale::Polish;
    Palette palette;
    /// Render a leading task marker ("[ ] ", "TODO ") as a clickable task:toggle glyph; when
    /// false it is hidden (the delegate draws its own checkbox).
    bool taskCheckbox = true;
    /// Embedded pages, sections and blocks (`![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]`,
    /// `!((id))`) are shown by the caller next to the text (the editor's embed frames): their
    /// syntax is hidden instead of rendered as a placeholder link. Attachment embeds
    /// (`![[photo.png]]`) still render as links.
    bool hideTransclusions = false;
};

/// A RenderContext::linkTitle for daily notes: a link naming a day (journalDateFromLinkTarget():
/// `[[2026-10-13]]`, the vault's journalFileFormat) shows that date as dates::formatDateShort()
/// does — "wt 13 paź" ("Tue, Oct 13"), with the year when it is not `today`'s year ("13 paź
/// 2025"; an invalid `today`: always). Any other target: nullopt (shown as written).
std::function<std::optional<QString>(const QString &target)> journalLinkTitle(const VaultConfig &config,
                                                                              dates::UiLocale locale, QDate today);

class InlineRenderer {
public:
    InlineRenderer() = default;
    explicit InlineRenderer(RenderContext context);

    const RenderContext &context() const { return m_ctx; }
    void setContext(RenderContext context) { m_ctx = std::move(context); }

    /// Render inline Markdown (one or more lines, e.g. BlockEntry::body of a list item or a
    /// paragraph): **bold**, *italic*, ~~strike~~, ==highlight==, `code`, links, tags, block
    /// references, embeds (placeholders), task tokens as chips, reminders as amber chips,
    /// `key:: value` with a muted key; formulas (`$…$`, `$$…$$`; `\(` and `\[` are escaped brackets)
    /// as rich text (markdown/math.h); block ids, `id::`/`collapsed::` lines, Logseq drawers and
    /// %%comments%% are hidden; line breaks become <br/>. Polish typography: the space after a
    /// one-letter word (a, i, o, u, w, z — "w domu", "i dalej") is a no-break space (U+00A0) in
    /// the output, so such a word never ends a line (display only; the note keeps its spaces).
    QString render(QStringView text) const;

    /// Render a whole block body of the given kind: heading markers, quote prefixes and the
    /// callout header syntax are removed first (callout title in bold); code, tables, HTML and
    /// frontmatter are shown as escaped preformatted text; a rule is <hr/>. Only a list item's
    /// leading `[ ]` / `TODO` is a task marker (SYNTAX §5, like parseNote()); in a paragraph,
    /// heading, quote or callout it is shown as text. render() always treats it as a marker, so
    /// paragraphs should go through renderBlock().
    QString renderBlock(QStringView body, BlockKind kind) const;

    /// HTML-escape plain text for rich text (& < > " escaped, line breaks as <br/>).
    static QString escape(QStringView text);

private:
    /// Verbatim runs of a text derived from a block body: {position in the text, position in the
    /// body} in increasing order (empty: the text is the body). Maps `reminder:` ranges back.
    using SourceRuns = QList<std::pair<qsizetype, qsizetype>>;

    /// `taskMarker`: a leading task marker is recognised (list item bodies only).
    QString renderInline(QStringView text, bool topLevel, int depth, const SourceRuns &runs = {},
                         bool taskMarker = true) const;

    RenderContext m_ctx;
};

} // namespace brulion::markdown
