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
    /// Embedded players and frames — `<iframe>`, `<video>`, `<audio>` with an http(s) source (also
    /// a `<source>` inside), a YouTube or Vimeo player — become a paragraph with a link to them
    /// (its label: the element's `title`, else the address); off: they are dropped with their
    /// content (the Web Clipper turns it on).
    bool embedsAsLinks = false;
    /// Math formulas of web pages — KaTeX, MathJax 2 and 3, Wikipedia's and any `<math>` (MathML
    /// converted to TeX) — become `$…$` (inline) and `$$…$$` (display, a paragraph of its own)
    /// with their TeX source, the rendering they come with dropped (docs/CLIPPER.md "Math
    /// formulas"); off: `<math>` is dropped with its content. A formula the editor would not read
    /// as one (TeX with a backtick, a closing `$` before a letter…) is put in a code span; a letter
    /// or digit touching a formula gets a space. The formula marks of htmlutil_p.h that callbacks
    /// return are removed from the result when this is on.
    bool mathFormulas = false;
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
/// the language of `class="language-x"` / `lang-x` on the `<code>` or the `<pre>`, `data-lang`,
/// `data-language`, `lang`, GitHub's `highlight-source-x` around it), blockquotes, rules, GFM
/// tables (`|` escaped), links and images (a lazily loaded picture by its `data-src`,
/// `data-lazy-src`, `data-original` or the largest `srcset` candidate when `src` is empty or a
/// data: placeholder). The HTML5 elements QTextDocument does not know — `article`, `section`,
/// `header`, `footer`, `nav`, `aside`, `main`, `figure`, `details`, `address`, `hgroup`, `search`
/// — are blocks, so their texts never run together; a `<figcaption>` is a paragraph of its own in
/// italics, a `<summary>` one in bold. Text is escaped so it is not read as Markdown syntax, also where its dollars
/// would pair into a formula across styles and lines ("Cena $10, <b>Netflix</b> 15$"; nothing in
/// code, link destinations or what the callbacks return as a formula — see escapeTextFormulas()
/// in htmlutil_p.h). Lines are never wrapped; code blocks keep their lines exactly (blank-line
/// runs, indentation, trailing spaces).
HtmlMarkdown htmlToMarkdown(const QString &html, const HtmlToMarkdownOptions &options = {});

/// Plain text of HTML: block boundaries and `<br>` become line breaks, entities are decoded,
/// images and scripts dropped. Parsed like htmlToMarkdown() (no resource is ever loaded).
QString htmlToPlainText(const QString &html);

/// Escape plain text for Markdown: characters that would start emphasis, code, links, embeds,
/// block references, comments, highlights or HTML are backslash-escaped, and so are the dollars
/// that would make a formula of the text (docs/SYNTAX.md §11): the opening `$` of "Cena $10,
/// Netflix 15$" or "$x$" (the closing one when the opening one is in a bare URL) — dollars that
/// pair with nothing ("$5 i $10") stay as they are. With `lineStart` also block syntax at the
/// start (headings, list markers, quotes, fences, rules, task checkboxes). With `linkLabel` every
/// `[` and `]` is escaped too (text inside `[…](…)` or `![…](…)`). Dollars of several pieces put
/// together can still pair across them: importers check the whole note (htmlToMarkdown() does so
/// for its result).
QString escapeMarkdownText(QStringView text, bool lineStart = true, bool linkLabel = false);

} // namespace brulion::io
