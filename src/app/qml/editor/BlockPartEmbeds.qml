pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockPartEmbeds — the pictures and files of a block that has one, made by a BlockDelegate Loader through
// BlockParts.part("Embeds") (blockviews.h: one component for every block of every editor, not one in
// each block). It finds its block once, when it is made: BlockViews.ancestor() (the Loader, then
// the block).
EmbedView {
    id: embedPart
    final property BlockDelegate block: BlockViews.ancestor(embedPart, 2) as BlockDelegate
    embeds: embedPart.block.hasEmbeds ? embedPart.block.embeds : embedPart.block.noItems
    controller: embedPart.block.controller
    editor: embedPart.block.editor
    readOnly: embedPart.block.editor.readOnly
    onClicked: (target) => {
        // (The arguments first: compiled code reads nothing again after a call.)
        const link = "page:" + Js.encodeURIComponent(target)
        embedPart.block.controller.activateLink(embedPart.block.index, link, false)
    }
    onDrawingClicked: (path) => embedPart.block.controller.openDrawing(path)
    onPdfClicked: (path, page) => embedPart.block.controller.openPdf(embedPart.block.index, path, page)
    onBackgroundClicked: embedPart.block.editOnTap(-1)
    onBackgroundLongPressed: embedPart.block.editor.longPressText(embedPart.block.index)
    onShowWeb: (url) => embedPart.block.controller.showWebPicture(url)
    onDownloadWeb: (url) => embedPart.block.editor.downloadWebPictures(url)
    onWebMenu: (url, host, item, x, y) => embedPart.block.editor.openWebPictureMenu(embedPart.block.index, url, host, item, x, y)
    onWebOpen: (picture) => embedPart.block.editor.openWebPictureViewer(embedPart.block.index, picture)
    onViewToast: (text, kind, undo, model) => embedPart.block.editor.notice(text, kind, undo ? embedPart.block.editor.viewUndoAction(model) : null)
    onTranscribeRequested: (path, target) => embedPart.block.editor.transcribeRecording(embedPart.block.index, path, target)
    onCopyRequested: (text) => embedPart.block.editor.copyPlainText(text)
}
