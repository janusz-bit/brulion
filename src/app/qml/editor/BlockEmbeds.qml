pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BlockEmbeds — a block's pictures, recordings, PDFs and files under its text (EmbedView) and
// what tapping them does (BlockDelegate's `embedView`). Loaded from its file only for a block that
// has embeds (docs/PERFORMANCE.md "QML files loaded when first used": a QML file named in the
// delegate is loaded with the editor, and most blocks have none).
EmbedView {
    id: view

    /// The block (an Item: BlockDelegate passes itself).
    property Item blockItem
    final property BlockDelegate block: blockItem as BlockDelegate

    embeds: view.block.hasEmbeds ? view.block.embeds : view.block.noItems
    controller: view.block.controller
    editor: view.block.editor
    readOnly: view.block.editor.readOnly
    onClicked: (target) => {
        const link = "page:" + Js.encodeURIComponent(target)
        view.block.controller.activateLink(view.block.index, link, false)
    }
    onDrawingClicked: (path) => view.block.controller.openDrawing(path)
    onPdfClicked: (path, page) => view.block.controller.openPdf(view.block.index, path, page)
    onBackgroundClicked: view.block.editOnTap(-1)
    onBackgroundLongPressed: view.block.editor.longPressText(view.block.index)
    onShowWeb: (url) => view.block.controller.showWebPicture(url)
    onDownloadWeb: (url) => view.block.editor.downloadWebPictures(url)
    onWebMenu: (url, host, item, x, y) => view.block.editor.openWebPictureMenu(view.block.index, url, host, item, x, y)
    onWebOpen: (picture) => view.block.editor.openWebPictureViewer(view.block.index, picture)
    onViewToast: (text, kind, undo, model) => view.block.editor.notice(text, kind, undo ? view.block.editor.viewUndoAction(model) : null)
    onTranscribeRequested: (path, target) => view.block.editor.transcribeRecording(view.block.index, path, target)
    onCopyRequested: (text) => view.block.editor.copyPlainText(text)
}
