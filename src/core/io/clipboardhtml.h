// HTML of note blocks copied to the clipboard (its text/html flavour next to the Markdown text,
// docs/UI.md "Selecting and copying"). Part of the io module — see io/io.h for the overview.
#pragma once

#include "types.h"

#include <QLatin1StringView>
#include <QString>

#include <functional>

namespace brulion::io {

/// How clipboardHtml() resolves what the blocks point to (all optional).
struct ClipboardHtmlOptions {
    VaultConfig config;
    /// URL of an attachment named by a note (target as written): a file URL, or "" when it is
    /// missing or has no file URL (the browser's data: URLs) — the picture is its name then.
    std::function<QString(const QString &target)> attachmentUrl;
    /// Text of a referenced block (`((id))`); "" when unknown.
    std::function<QString(const QString &id)> blockText;
};

/// Marks HTML that clipboardHtml() made: a paste of it is this app's own copy, whose text flavour
/// is the Markdown (the HTML would lose block ids, properties, formulas) — also after a restart.
inline constexpr QLatin1StringView ClipboardHtmlMarker{"<meta name=\"generator\" content=\"Brulion\">"};

/// `markdown` (blocks copied from a note) as an HTML fragment for mail and office apps: the HTML
/// exporters' semantic HTML (headings, nested lists, task checkboxes, code, tables, quotes,
/// callouts, emphasis, external links, date, recurrence and priority chips — of any block, a
/// task or not) with everything escaped — raw HTML in a note is text —; links to notes and tags
/// as their text, block references as their block's text, pictures and other files as links to
/// them (never data: URLs). Starts with `<meta charset="utf-8">` and ClipboardHtmlMarker; "" for
/// empty Markdown.
QString clipboardHtml(const QString &markdown, const ClipboardHtmlOptions &options = {});

} // namespace brulion::io
