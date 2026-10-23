// HTML of note blocks copied to the clipboard (its text/html flavour next to the Markdown text,
// docs/UI.md "Selecting and copying"). Part of the io module — see io/io.h for the overview.
#pragma once

#include "types.h"

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

/// `markdown` (blocks copied from a note) as an HTML fragment for mail and office apps: the HTML
/// exporters' semantic HTML (headings, nested lists, task checkboxes, code, tables, quotes,
/// callouts, emphasis, external links, date chips) with everything escaped — raw HTML in a note
/// is text —; links to notes and tags as their text, block references as their block's text,
/// pictures and other files as links to them (never data: URLs). Starts with
/// `<meta charset="utf-8">`; "" for empty Markdown.
QString clipboardHtml(const QString &markdown, const ClipboardHtmlOptions &options = {});

} // namespace brulion::io
