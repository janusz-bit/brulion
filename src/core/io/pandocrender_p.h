// Rendering of notes as Pandoc Markdown for the "pandoc" exporter: the blocks of a ParsedNote
// become what `pandoc -f markdown` reads with its default extensions — ATX headings with
// identifiers, tight and loose lists, task lists, pipe tables, fenced code, `$…$` / `$$…$$` TeX
// math (also from ```math blocks), fenced divs for callouts and embeds, bracketed spans for block
// ids (`[]{#id}`), tags, dates and properties, footnotes. Brulion's own syntax (wiki links, block
// references, emoji metadata, `==highlight==`, statuses other than todo / done) is translated;
// text is escaped so that pandoc reads it literally (no raw HTML, citations, sub/superscripts).
// Internal to the io module.
#pragma once

#include "types.h"

#include <QString>
#include <QStringView>

#include <functional>
#include <optional>
#include <utility>

namespace brulion::io::detail {

/// How links of the rendered note are written (all optional; an unset callback: plain text).
struct PandocLinks {
    /// Destination of a page link ("#anchor"); nullopt when the page is missing, "" when it is
    /// exported but has no anchor (the link is its text).
    std::function<std::optional<QString>(const QString &target, const QString &heading, const QString &blockId)> page;
    /// Destination of an attachment (a relative, encoded path); nullopt when missing.
    std::function<std::optional<QString>(const QString &target)> attachment;
    /// Destination and text of a referenced block; nullopt when unknown.
    std::function<std::optional<std::pair<QString, QString>>(const QString &id)> block;
    /// Destination of the page a tag names; nullopt: the tag is a span.
    std::function<std::optional<QString>(const QString &tag)> tag;
    /// Pandoc Markdown of an embedded page (`![[Page]]` alone in a block); empty when unavailable.
    std::function<QString(const QString &target, const QString &heading, const QString &blockId)> embed;
    /// Prefix of identifiers (several notes in one document need distinct ones).
    QString idPrefix;
    /// Prefix of footnote labels, for the same reason.
    QString footnotePrefix;
    /// Write identifiers of headings and blocks (an embedded copy must not repeat them).
    bool ids = true;
    /// Added to every heading level (notes under a title heading), clamped to 1…6.
    int headingShift = 0;
    /// A block not rendered (the note's first heading when it is the note's title), or -1.
    int skipBlock = -1;
};

class PandocRenderer {
public:
    explicit PandocRenderer(PandocLinks links = {});

    /// The note's blocks (frontmatter is not rendered), separated by blank lines.
    QString renderNote(const ParsedNote &note) const;
    /// Blocks `indices` (siblings, in order) of `note` with their children.
    QString renderBlocks(const ParsedNote &note, const QList<int> &indices) const;
    /// Inline Markdown of the Brulion dialect as Pandoc Markdown; line breaks become hard breaks
    /// (`\` at the line end). `insideLink`: the text sits in a link label (no links or tags).
    QString renderInline(QStringView text, bool insideLink = false) const;

    /// Text escaped so that pandoc reads it literally.
    static QString escape(QStringView text);
    /// Identifier of a heading, without prefix — the one HtmlRenderer gives it.
    static QString headingAnchor(const QString &heading);
    /// Identifier of a block ("block-abc123"), without prefix.
    static QString blockAnchor(const QString &id);
    /// `#tag` as Pandoc Markdown.
    static QString tagSpan(const QString &tag);

private:
    QString renderBlock(const ParsedNote &note, int index) const;
    QString renderListItem(const ParsedNote &note, int index) const;
    QString renderTable(const QString &source) const;
    QString metaSpans(const Block &block) const;
    QString idSpan(const Block &block) const;
    QString embeddedContent(const QString &text) const;
    QString link(const QString &label, const std::optional<QString> &destination) const;

    PandocLinks m_links;
};

} // namespace brulion::io::detail
