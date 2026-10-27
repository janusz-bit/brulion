// Full HTML rendering of notes for the HTML exporters: blocks of a ParsedNote become semantic
// HTML (headings, paragraphs, nested lists, task checkboxes, code, tables, quotes, callouts,
// rules), inline Markdown of the Brulion dialect becomes <strong>/<em>/<del>/<mark>/<code>, links,
// images, tags, block references and date chips. Everything not recognised is escaped — raw
// HTML in notes is shown as text, never interpreted. Internal to the io module.
#pragma once

#include "types.h"

#include <QString>
#include <QStringView>

#include <functional>
#include <optional>
#include <utility>

namespace brulion::io::detail {

/// How links of the rendered note are turned into URLs (all optional).
struct HtmlLinks {
    /// href of a page link (target as written, heading / block id fragment); nullopt when the
    /// page does not exist (rendered as a "missing" span). An empty target is a same-page link.
    std::function<std::optional<QString>(const QString &target, const QString &heading, const QString &blockId)> page;
    /// URL of an attachment (target as written); nullopt when missing.
    std::function<std::optional<QString>(const QString &target)> attachment;
    /// href of a tag page; unset: tags are plain spans.
    std::function<QString(const QString &tag)> tag;
    /// href and text of a referenced block (an empty href: the text alone); nullopt when unknown.
    std::function<std::optional<std::pair<QString, QString>>(const QString &id)> block;
    /// HTML of an embedded page (`![[Page]]` alone in a block); empty when unavailable.
    std::function<QString(const QString &target, const QString &heading, const QString &blockId)> embed;
    /// src of the copy of a picture from the internet (the URL as the link target gives it) the
    /// export includes; nullopt or unset: the picture keeps its URL.
    std::function<std::optional<QString>(const QString &url)> webPicture;
    /// Prefix of element ids (several notes in one document need distinct ids).
    QString idPrefix;
    /// Pictures, audio and video as links to them instead of <img> / <audio> / <video> (the
    /// clipboard: clipboardHtml()).
    bool mediaAsLinks = false;
};

class HtmlRenderer {
public:
    explicit HtmlRenderer(HtmlLinks links = {});

    /// The note's blocks as HTML (no <html>/<body>; frontmatter is not rendered).
    QString renderNote(const ParsedNote &note) const;
    /// Blocks `indices` (siblings, in order) of `note` with their children.
    QString renderBlocks(const ParsedNote &note, const QList<int> &indices) const;
    /// Inline Markdown as HTML (line breaks -> <br>). `insideLink`: the text sits inside a link (a
    /// link label, a block reference): links show their visible text without an anchor and tags
    /// are plain text — the parser sees no tags there and anchors must not nest.
    QString renderInline(QStringView text, bool insideLink = false) const;

    /// HTML escaping of text (& < > " ').
    static QString escape(QStringView text);
    /// Element id of a heading ("Plan na jutro" -> "plan-na-jutro"), without prefix.
    static QString headingAnchor(const QString &heading);
    /// Element id of a block ("block-abc123"), without prefix.
    static QString blockAnchor(const QString &id);

private:
    QString renderBlock(const ParsedNote &note, int index) const;
    QString renderListItem(const ParsedNote &note, int index) const;
    QString renderTable(const QString &source) const;
    QString metaChips(const Block &block) const;
    /// HTML of the page embedded by `text` when it is a single page embed (`![[Page]]`), else "".
    QString embeddedContent(const QString &text) const;
    /// A picture from the internet (`url` an http(s) URL): <img> of the URL or of its copy
    /// (HtmlLinks::webPicture), a link with mediaAsLinks.
    QString webPictureHtml(const QString &url, const QString &alt) const;

    HtmlLinks m_links;
};

} // namespace brulion::io::detail
