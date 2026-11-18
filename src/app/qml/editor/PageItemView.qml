pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// PageItemView — a page's blocks as one PageItem over a plain Flickable (Z10 stage C, behind
// BRULION_PAGE_ITEM=1: EditorInput.pageItem; docs/UI.md "One item per page (Z10 stage C)"). The
// item draws the text rows it can from a cache of layouts and hosts every other row — code, tables,
// tasks, the edited row — as the editor's BlockDelegate; the Flickable scrolls (TouchFlick,
// SmoothScroll), the item's anchor keeps what is on screen while rows are measured. BlockEditor
// shows it instead of its ListView's rows (the list stays, empty, as the holder of the keyboard).
// Taps: a link opens, elsewhere the block is edited (its row is then hosted); hosted rows handle
// their own pointer events.
Item {
    id: root

    final property BlockEditor editor: editorItem as BlockEditor
    /// The editor as an Item (a file cannot give its own root type to a property in compiled code).
    property Item editorItem
    // (final: looked into from BlockEditor; a member of a property that may be overridden does
    // not compile, docs/UI.md "Compiled QML".)
    final property PageItem page: page
    final property Flickable flickable: flick
    final property PageController controller: editor ? editor.controller : null
    /// Where a tap was in the tapped row's text (a method's result: read through a property), where
    /// a right-click was in the scene.
    property point tapAt
    property point menuScene

    /// Keep `rect` of `item` (a hosted row's editor, its caret) in view (BlockEditor.ensureVisible()).
    function ensureVisible(item: Item, rect: rect, margin: real): void {
        page.ensureItemVisible(item, rect, margin)
    }

    Flickable {
        id: flick
        TouchFlick.enabled: true
        anchors.fill: parent
        contentWidth: width
        boundsBehavior: Flickable.StopAtBounds
        pressDelay: 0
        // A mouse's left drag does not scroll (the wheel and the touchpad do), as in the list.
        acceptedButtons: root.editor.touch ? Qt.LeftButton : Qt.NoButton
        // (contentHeight and contentY are the PageItem's.) The bar from contentY and contentHeight:
        // the page's height is exact for the rows shown once, estimated for the others.
        T.ScrollBar.vertical: BScrollBar { z: 2 }
    }

    // A child of the Flickable itself, not of its content: it stays over the viewport, and the
    // Flickable turns drags on it into flicks as it does for a list's delegates.
    PageItem {
        id: page
        objectName: "pageItem"
        parent: flick
        anchors.fill: parent
        flickable: flick
        model: root.controller ? root.controller.blocks : null
        delegate: Component {
            BlockDelegate { editorItem: root.editor }
        }
        header: Column {
            width: page.width
            Loader {
                width: parent.width
                active: root.editor.header !== null
                sourceComponent: root.editor.header
            }
            Loader {
                x: root.editor.contentX
                width: root.editor.contentWidth
                active: root.controller !== null && root.controller.zoomBlock >= 0
                visible: active
                sourceComponent: ZoomBreadcrumb {
                    controller: root.controller
                }
            }
        }
        footer: Item {
            width: page.width
            height: root.editor.bottomSpace
            TapHandler {
                onTapped: {
                    if (!root.controller || root.editor.selectionMode || root.editor.readOnly)
                        return
                    const row = root.controller.appendEmptyBlock()
                    root.editor.startEditing(row, -1)
                }
            }
        }

        documentMode: root.editor.documentMode
        contentX: root.editor.contentX
        contentWidth: root.editor.contentWidth
        gutter: root.editor.gutter
        indentStep: root.editor.indentStep
        bulletColumn: root.editor.bulletColumn
        listIndent: root.editor.listIndent
        rowPad: Theme.touchUi ? 4 : 2
        paragraphGap: root.editor.paragraphGap
        listGap: root.editor.listGap
        headingGapBelow: root.editor.headingGapBelow
        headingGapsAbove: [root.editor.paragraphGap, root.editor.headingGapAbove(1), root.editor.headingGapAbove(2),
            root.editor.headingGapAbove(3), root.editor.headingGapAbove(4), root.editor.headingGapAbove(5),
            root.editor.headingGapAbove(6)]
        fontSizes: [root.editor.headingSize(0), root.editor.headingSize(1), root.editor.headingSize(2),
            root.editor.headingSize(3), root.editor.headingSize(4), root.editor.headingSize(5), root.editor.headingSize(6)]
        lineHeights: [root.editor.headingLineHeight(0), root.editor.headingLineHeight(1),
            root.editor.headingLineHeight(2), root.editor.headingLineHeight(3), root.editor.headingLineHeight(4),
            root.editor.headingLineHeight(5), root.editor.headingLineHeight(6)]
        firstLineCenters: [root.editor.firstLineCenter(0), root.editor.firstLineCenter(1),
            root.editor.firstLineCenter(2), root.editor.firstLineCenter(3), root.editor.firstLineCenter(4),
            root.editor.firstLineCenter(5), root.editor.firstLineCenter(6)]
        spaces: [Theme.space1, Theme.space2, Theme.space3, Theme.space4, Theme.space5, Theme.space6]
        fontFamily: Theme.fontEditor
        fontWeight: Theme.weightNormal
        headingWeight: Theme.weightSemiBold
        textColor: Theme.fg
        mutedColor: Theme.fgMuted
        guideColor: Theme.guide
        linkColor: Theme.link
        boxRadius: Theme.radiusXs
        titleRow: root.editor.titleFirst && root.controller !== null ? root.controller.titleRow : -1
        findQuery: root.editor.find !== null ? root.editor.find.appliedQuery : ""
        findMatchCase: root.editor.find !== null && root.editor.find.matchCase
        findMatchDiacritics: root.editor.find !== null && root.editor.find.matchDiacritics
        findWholeWord: root.editor.find !== null && root.editor.find.wholeWord
        findColor: root.editor.findHitColor

        Accessible.role: Accessible.List
        Accessible.name: root.controller ? root.controller.title : ""

        HoverHandler {
            id: hover
            enabled: !Theme.touchUi
            cursorShape: page.hoveredLink !== "" ? Qt.PointingHandCursor
                                                 : root.editor.readOnly ? Qt.ArrowCursor : Qt.IBeamCursor
            onPointChanged: page.setPointer(hover.point.position.x, hover.point.position.y)
            onHoveredChanged: if (!hovered) page.setPointer(-1, -1)
        }
        // A tap on a row the item draws: its link, else editing at the tapped place (the row is then
        // hosted by its delegate, which shows the inline editor). Hosted rows take their own taps.
        TapHandler {
            id: tap
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: (eventPoint) => root.tapped(eventPoint.position.x, eventPoint.position.y, tap.point.modifiers)
        }
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            acceptedButtons: Qt.RightButton
            onTapped: (eventPoint) => root.menuAt(eventPoint.position.x, eventPoint.position.y,
                                                  eventPoint.scenePosition)
        }
    }

    function tapped(x: real, y: real, modifiers: int): void {
        const row = page.rowAt(x, y)
        if (row < 0 || page.rowItem(row) !== null || !root.editor)
            return
        if (root.editor.blockTappedWith(row, modifiers))
            return
        const link = page.linkAt(x, y)
        if (root.editor.openDateDialogAt(row, link))
            return
        if (link !== "" && !root.editor.editsOnClick(link)) {
            root.editor.activateLink(row, link)
            return
        }
        root.tapAt = page.textPoint(row, x, y)
        root.editor.editAtWith(row, root.tapAt.x, root.tapAt.y, modifiers)
    }
    function menuAt(x: real, y: real, scene: point): void {
        root.menuScene = scene
        const row = page.rowAt(x, y)
        if (row >= 0 && page.rowItem(row) === null && root.editor)
            root.editor.openBlockMenuAt(row, root.menuScene)
    }
}
