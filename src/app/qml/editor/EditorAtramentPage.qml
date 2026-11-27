pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// EditorAtramentPage — a page's blocks drawn by Atrament (AppSettings.editorAtrament, Settings →
// Editor; docs/ATRAMENT.md "The block editor in Atrament (M2)", step 1 "Reading"). BlockEditor
// loads it by file name where Atrament is built (DeviceInfo.atrament) and shows it instead of its
// ListView's rows; the list stays, empty, as the holder of the keyboard (as for PageItemView).
// Reading only for now: text rows are Atrament's; every other row (code, tables, math, pictures,
// queries, the properties…) is the editor's BlockDelegate placed over its place in the list, as are
// the page's header (the title, the first property's draft, the zoom's breadcrumb) and footer
// (docs/ATRAMENT.md "Rows Atrament does not draw yet").
AtramentView {
    id: root

    /// The editor as an Item (a file cannot give its own root type to a property in compiled code).
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor

    pageModel: editor && editor.controller ? editor.controller.blocks : null
    fontSize: editor ? editor.fontSize : 16
    lineHeight: editor ? editor.lineHeight : 24
    indentStep: editor ? editor.indentStep : 26
    gutter: editor ? editor.gutter : 16
    bulletColumn: editor ? editor.bulletColumn : 20
    documentMode: editor ? editor.documentMode : false
    listIndent: editor ? editor.listIndent : 27
    contentX: editor ? editor.contentX : 0
    contentWidth: editor ? editor.contentWidth : 0
    textColor: Theme.fg
    mutedColor: Theme.fgMuted
    linkColor: Theme.link
    guideColor: Theme.guide
    backgroundColor: Theme.bg
    view: "page"
    onLinkActivated: (row, link) => {
        if (root.editor)
            root.editor.activateLink(row, link)
    }
    onTaskToggled: row => {
        if (root.editor && root.editor.controller)
            root.editor.controller.toggleTask(row)
    }
    activeFocusOnTab: true
    Accessible.name: qsTr("Page")

    // ---- The rows Atrament does not draw yet, the header and the footer (QML items over the list)
    delegate: Component {
        BlockDelegate {
            editorItem: root.editor
        }
    }
    header: Column {
        width: root.width
        Loader {
            width: parent.width
            active: root.editor !== null && root.editor.header !== null
            sourceComponent: root.editor ? root.editor.header : null
        }
        // The first property of a page that shows none (BlockEditor.addProperty()): only the add row.
        Loader {
            objectName: "propertyDraft"
            x: root.editor ? root.editor.contentX : 0
            width: root.editor ? root.editor.contentWidth : 0
            active: root.editor !== null && root.editor.propertyDraft && root.editor.controller !== null
            visible: active
            sourceComponent: Rectangle {
                implicitHeight: draftPanel.implicitHeight + 2 * Theme.space1
                radius: Theme.radius
                color: Theme.bgSunken
                border.width: 1
                border.color: Theme.borderSubtle
                // (PropertiesPanel, loaded from its file: docs/PERFORMANCE.md "QML files loaded when
                // first used".)
                Loader {
                    id: draftPanel
                    x: Theme.space1
                    y: Theme.space1
                    width: parent.width - 2 * Theme.space1
                    Component.onCompleted: {
                        root.editor._popupValues = { editorItem: root.editor, controller: root.editor.controller,
                            draft: true }
                        draftPanel.setSource(root.editor._propertiesSource, root.editor._popupValues)
                    }
                }
            }
        }
        Loader {
            x: root.editor ? root.editor.contentX : 0
            width: root.editor ? root.editor.contentWidth : 0
            active: root.editor !== null && root.editor.controller !== null && root.editor.controller.zoomBlock >= 0
            // An inactive Loader keeps the implicit height of the item it had: hidden, the Column
            // leaves no room for it after zooming out.
            visible: active
            sourceComponent: ZoomBreadcrumb {
                controller: root.editor.controller
            }
        }
    }
    // The room under the last block: a tap there appends an empty block and edits it.
    footer: Item {
        width: root.width
        height: root.editor ? root.editor.bottomSpace : 0
        TapHandler {
            onTapped: {
                // (Reading mode: a tap under the blocks, often the end of a scroll, does nothing.)
                if (!root.editor || !root.editor.controller || root.editor.selectionMode || root.editor.readOnly)
                    return
                const row = root.editor.controller.appendEmptyBlock()
                root.editor.startEditing(row, -1)
            }
        }
        HoverHandler {
            cursorShape: root.editor && root.editor.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor
        }
    }
}
