// Trilium Notes import, content part: the note content of Trilium's ZIP exports in the Brulion
// dialect (docs/SYNTAX.md). Text notes of the HTML export hold CKEditor 5 markup (to-do lists
// with task states, admonitions, math, footnotes, collapsible blocks, include-note boxes, link
// previews, code blocks with MIME classes); the Markdown export holds Turndown's Markdown with
// HTML islands. References into the export are resolved by the importer (import_trilium.cpp)
// through Resolver. Internal to the io module.
#pragma once

#include <QString>

#include <functional>
#include <optional>

namespace brulion::io::detail::trilium {

/// The importer's side of a conversion: what references found in a note become.
struct Resolver {
    /// Markdown for a link to `href` as written in the export (a relative, percent-encoded path, a
    /// raw attachment name, "#root/…/<noteId>") with the converted label `labelMarkdown`;
    /// `reference`: CKEditor's reference link (its text is a snapshot of the target's title).
    /// nullopt: not a reference into the export (an external URL) — the link is written as usual.
    std::function<std::optional<QString>(const QString &href, const QString &labelMarkdown, bool reference)> link;
    /// Markdown for an image `src` as written (a file of the export, a URL, a data: URI).
    std::function<QString(const QString &src, const QString &alt)> image;
    /// Markdown embedding the note `noteId` (an include-note box); empty to drop the box.
    std::function<QString(const QString &noteId)> include;
};

/// The content of a text note's page of the HTML export: the inside of `<div class="ck-content">`
/// (else of `<body>`, else everything — a note whose content is a whole HTML document).
QString noteBody(const QString &page);

/// HTML of a text note (noteBody()) in the dialect: to-do items `- [ ]` / `- [x]` / `- [/]`
/// (doing) / `- [-]` (cancelled), empty ones left out, admonitions `> [!tip]`, collapsible
/// blocks `> [!note]- Summary`, every quote a block of its own, footnotes `[^1]` (also those of
/// notes Trilium imported from Markdown) with their definitions at the end, inline math as code
/// `` `$E=mc^2$` `` and display math as a ```` ```math ```` block (the dialect has no math),
/// include-note boxes and links through `resolver`, link previews as links, code blocks with the
/// language of their MIME class, icons dropped. List items holding several blocks (code, images,
/// quotes, paragraphs) keep them, indented below the marker; lists next to each other stay
/// apart (`-` / `*`, `1.` / `1)`). `#` before tag-like words is escaped (Trilium has no inline
/// tags). Everything else as htmlToMarkdown() writes it (sanitised, escaped).
QString htmlNoteToMarkdown(const QString &html, const Resolver &resolver);

/// A note of the Markdown export (or a Markdown code note) in the dialect: the title line Trilium
/// adds removed (when it is `# <title>`), the HTML Turndown kept (reference links, figures — also
/// inside table rows —, include-note boxes, footnotes, `<details>`, `<sub>` …) converted like
/// htmlNoteToMarkdown(), links and images resolved, math (0.6x: its TeX unescaped), task states
/// and hashtags as above, admonition types lower-cased, fence languages mapped, stray `<` escaped
/// (old exports wrote `<` of the text unescaped), ordered lists that follow each other kept apart.
QString markdownNoteToMarkdown(const QString &markdown, const QString &title, const Resolver &resolver);

/// Fence language of a code note's MIME type: "text/x-python" -> "python",
/// "application/javascript;env=frontend" -> "javascript"; "" for plain text.
QString languageForMime(const QString &mime);

/// Fence language of a CKEditor code-block class suffix or a fence info string of Trilium's
/// Markdown export: "text-x-python" -> "python", "csrc" -> "c", "text-x-trilium-auto" -> "".
QString languageForCodeClass(const QString &cls);

} // namespace brulion::io::detail::trilium
