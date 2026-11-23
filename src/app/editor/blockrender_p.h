// Internal helpers of the block editor's rendering (BlockListModel, its transclusions and the
// query results): one block body to rich text, rich text back to its visible text.
#pragma once

#include "blocktext.h"
#include "editorhost.h"
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

/// renderBodyHtml() with the source anchors of its pieces (positions in the block's body).
markdown::TracedHtml renderBodyTraced(const markdown::BlockEntry &entry, const BlockShape &shape,
                                      const markdown::InlineRenderer &renderer, int *renderOffset = nullptr);

/// The visible text of renderer output (tags removed, entities decoded) — for measuring.
QString richTextToPlain(const QString &html);

/// The renderer's colours from QML's map (PageController.palette, LinkPreview.palette: link,
/// unresolved, muted, codeBackground, highlightBackground, <chip>Fg / <chip>Bg — colour names or
/// QColors; a missing or invalid one keeps the default). Defined in linkpreview.cpp.
markdown::Palette rendererPalette(const QVariantMap &map);

/// The macros of extensions `ctx` renders (one BlockListModel::renderContextFor() gave it, so the
/// host renders macros) as `host` shows them for the note `path` — the note the rendered text is
/// in, not the page that shows it (a query's result) —, setting `*shows` when one was asked for:
/// such a row is rendered again on EditorHost::macrosChanged. The context is used right away.
inline void showMacrosOf(markdown::RenderContext &ctx, EditorHost *host, const QString &path, bool *shows)
{
    if (!ctx.macro || !host)
        return; // the host computes no macros: they show as written
    ctx.macro = [host, path, shows](const markdown::MacroSpan &macro) {
        if (shows)
            *shows = true;
        return host->macroView(macro, path);
    };
}

} // namespace brulion::editor::detail
