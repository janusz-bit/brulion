// HTML -> Brulion Markdown (docs/SYNTAX.md) for importers (Evernote ENML, HTML files, rich text
// of other apps). Part of the io module — see io/io.h for the overview. Uses QtGui.
#pragma once

#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::io {

/// An `<img>` met while converting.
struct HtmlImage {
    QString src; ///< as written: a relative path, a URL or a data: URI
    QString alt;
};

struct HtmlToMarkdownOptions {
    /// Markdown written for an image. Default: `![alt](src)` for http(s) and relative sources;
    /// data: URIs and other schemes are dropped. Return an empty string to drop the image.
    std::function<QString(const HtmlImage &image)> image;
    /// Markdown for a link with target `href` (as written) and already converted label
    /// `labelMarkdown`; nullopt for the default (`[label](href)`, a bare URL when the label is
    /// the URL; javascript:/data:/vbscript: links keep only their label).
    std::function<std::optional<QString>(const QString &href, const QString &labelMarkdown)> link;
};

struct HtmlMarkdown {
    QString markdown; ///< ends with "\n" unless empty
    QString title;    ///< the document's <title>, empty when none
};

/// Convert HTML to Markdown. The HTML is parsed by QTextDocument (never loading any resource:
/// no files, no network; scripts and styles are dropped) and Markdown is written from the
/// document model: headings, paragraphs (`<div>` lines stay lines), bullet / numbered / nested
/// lists, checkboxes (`<input type="checkbox">` -> `- [ ]` / `- [x]`), **bold**, *italic*,
/// ~~strike~~, ==highlight== (`<mark>`, background colour), `code`, fenced code (`<pre>`, with
/// the language of `class="language-x"`), blockquotes, rules, GFM tables (`|` escaped), links
/// and images. Text is escaped so it is not read as Markdown syntax. Lines are never wrapped;
/// code blocks keep their lines exactly (blank-line runs, indentation, trailing spaces).
HtmlMarkdown htmlToMarkdown(const QString &html, const HtmlToMarkdownOptions &options = {});

/// Plain text of HTML: block boundaries and `<br>` become line breaks, entities are decoded,
/// images and scripts dropped. Parsed like htmlToMarkdown() (no resource is ever loaded).
QString htmlToPlainText(const QString &html);

/// Escape plain text for Markdown: characters that would start emphasis, code, links, embeds,
/// block references, comments, highlights or HTML are backslash-escaped; with `lineStart` also
/// block syntax at the start (headings, list markers, quotes, fences, rules, task checkboxes).
/// With `linkLabel` every `[` and `]` is escaped too (text inside `[…](…)` or `![…](…)`).
QString escapeMarkdownText(QStringView text, bool lineStart = true, bool linkLabel = false);

} // namespace brulion::io
