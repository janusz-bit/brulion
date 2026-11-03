// Internal helpers of the block editor's rendering (BlockListModel, its transclusions and the
// query results): one block body to rich text, rich text back to its visible text.
#pragma once

#include "blocktext.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"

#include <QString>
#include <QVariantMap>

namespace brulion::editor::detail {

/// Rich text of a block body as the editor shows it unfocused (text, headings incl. Logseq
/// "- # Title" items, quotes, callouts, escaped HTML); "" for views drawn by their own delegate
/// (code, calc, query, table, rule, frontmatter). `renderOffset` receives the offset of the
/// rendered text in the body (maps `reminder:` ranges).
QString renderBodyHtml(const markdown::BlockEntry &entry, const BlockShape &shape,
                       const markdown::InlineRenderer &renderer, int *renderOffset = nullptr);

/// The visible text of renderer output (tags removed, entities decoded) — for measuring.
QString richTextToPlain(const QString &html);

/// The renderer's colours from QML's map (PageController.palette, LinkPreview.palette: link,
/// unresolved, muted, codeBackground, highlightBackground, <chip>Fg / <chip>Bg — colour names or
/// QColors; a missing or invalid one keeps the default). Defined in linkpreview.cpp.
markdown::Palette rendererPalette(const QVariantMap &map);

} // namespace brulion::editor::detail
