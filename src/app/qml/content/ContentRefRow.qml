pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentRefRow — one block that refers to something (a backlink, a tagged block, an unlinked
// mention): its parents as a small breadcrumb, then the block's text as rendered by the core
// InlineRenderer (escaped rich text; links, tags and chips are clickable), optionally an action
// button on the right ("Link").
//
//   ContentRefRow {
//       html: model.html; plain: model.text; breadcrumb: model.breadcrumb
//       sourcePath: model.path; line: model.line; taskStatus: model.taskStatus
//       onOpened: (newPane) => AppController.openPage(model.path, model.line, newPane)
//       onTaskToggled: refs.toggleTask(index)
//       onTaskStatusRequested: (status) => refs.setTaskStatus(index, status)
//   }
//
// A click on a link follows it (`tools`: PageTools.openLink, resolved from `sourcePath`); the mouse
// pointer resting on a [[page]] or ((block)) link shows its preview (LinkPreviews). Tags and chips
// are rounded shapes (BRichText), the text cut after 12 lines (6 when compact) with "…". A click
// on the task checkbox emits taskToggled() (a right-click or long press on it: the status menu,
// taskStatusRequested()), a click anywhere else opens the block in its page — a Ctrl+click or a middle
// click in a new tab (OpenIn: opened(false) emitted between OpenIn.begin("tab") and OpenIn.end()), links too.
// Keyboard: focusable, Enter opens, Ctrl+Enter opens in the side panel (wide windows).
FocusScope {
    id: row

    property string html
    property string plain
    property string breadcrumb
    property string taskStatus
    property string sourcePath
    property int line: -1
    /// Fewer lines before the text is cut (side panel).
    property bool compact: false
    /// Text of the action button on the right ("" = none).
    property string actionText
    property string actionIcon
    property bool actionBusy: false
    /// Follows links in the text (one shared instance per list).
    property PageTools tools: null

    signal opened(bool newPane)
    signal taskToggled()
    /// The checkbox's status menu (right-click / long press): "todo" … "cancelled", "" = none.
    signal taskStatusRequested(string status)
    signal actionTriggered()

    readonly property bool closed: taskStatus === "done" || taskStatus === "cancelled"
    // Quoted note text at the UI's base size everywhere, so the page's own editor text (textEditor)
    // stays the only larger size and the scale has no step between the UI and the editor.
    readonly property int fontSize: Theme.textBase
    /// The link of the text under the mouse or the pressing finger, underlined (as in the editor).
    property string hoveredLink: ""
    /// The last point trackLink() looked at, in textItem's coordinates.
    property point linkPoint

    implicitHeight: layout.implicitHeight + 2 * Theme.space2
    activeFocusOnTab: true

    Accessible.role: Accessible.Link
    Accessible.name: (breadcrumb !== "" ? breadcrumb + ": " : "") + plain
    Accessible.description: qsTr("Opens the block in its page")
    Accessible.onPressAction: row.opened(false)

    Keys.onReturnPressed: (event) => row.opened((event.modifiers & Qt.ControlModifier) !== 0 && !Theme.narrow)
    Keys.onEnterPressed: (event) => row.opened((event.modifiers & Qt.ControlModifier) !== 0 && !Theme.narrow)
    Keys.onSpacePressed: if (row.taskStatus !== "") row.taskToggled()

    Rectangle {
        id: background
        anchors.fill: parent
        radius: Theme.radius
        color: tap.pressed ? Theme.bgPressed : hover.hovered ? Theme.bgHover : "transparent"
        BFocusRing { target: background; visible: row.activeFocus; targetRadius: Theme.radius; inset: true }
    }

    RowLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: Theme.space2
        spacing: Theme.space2

        // A bullet like the editor's, or for tasks the editor's checkbox (same column).
        Item {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 14 // the checkbox (16 px) overhangs a little, the text stays aligned
            Layout.preferredHeight: textItem.lineHeight
            Layout.topMargin: crumb.visible ? crumb.height + 2 : 0
            Rectangle {
                visible: row.taskStatus === ""
                anchors.centerIn: parent
                width: 6
                height: 6
                radius: 3
                color: Theme.fgFaint
            }
            TaskBox {
                objectName: "refTaskBox"
                visible: row.taskStatus !== ""
                anchors.centerIn: parent
                status: row.taskStatus !== "" ? row.taskStatus : "todo"
                onToggled: row.taskToggled()
                onStatusChosen: (status) => {
                    if (status !== row.taskStatus)
                        row.taskStatusRequested(status)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            BText {
                id: crumb
                Layout.fillWidth: true
                visible: row.breadcrumb !== ""
                text: row.breadcrumb
                variant: "caption"
                color: Theme.fgFaint
                elide: Text.ElideMiddle
                maximumLineCount: 1
            }
            BRichText {
                id: textItem
                Layout.fillWidth: true
                html: row.html
                hoveredLink: row.hoveredLink
                maximumLineCount: row.compact ? 6 : 12
                font.family: Theme.fontEditor
                font.pixelSize: row.fontSize
                font.strikeout: row.taskStatus === "cancelled"
                lineHeight: Theme.lineHeightPx(row.fontSize, Theme.lineHeight)
                color: row.closed ? Theme.fgFaint : Theme.fg
                // Screen readers: the text's links as link items of their own (the row stays the
                // link to the reference; the task checkbox is TaskBox).
                BLinkTargets {
                    textItem: textItem.textItem
                    html: row.html
                    lineHeight: textItem.lineHeight
                    maxHeight: textItem.height
                    onActivated: (href, x, y) => {
                        if (!row.tools || !row.tools.openLink(href, row.sourcePath, false))
                            row.opened(false)
                    }
                }
            }
        }

        BButton {
            visible: row.actionText !== ""
            Layout.alignment: Qt.AlignTop
            size: "small"
            variant: "secondary"
            text: row.actionText
            iconName: row.actionIcon
            busy: row.actionBusy
            onClicked: row.actionTriggered()
        }
    }

    function trackLink(pos: point): void {
        row.linkPoint = textItem.mapFromItem(row, pos.x, pos.y)
        const link = textItem.linkAt(row.linkPoint.x, row.linkPoint.y)
        const h = Js.startsWith(link, "task:") ? "" : link
        if (h !== row.hoveredLink)
            row.hoveredLink = h
    }

    HoverHandler {
        id: hover
        enabled: !Theme.touchUi
        cursorShape: Qt.PointingHandCursor
        onPointChanged: {
            // The link under the resting pointer: its preview (LinkPreviews, docs/UI.md "Link previews").
            row.trackLink(hover.point.position)
            LinkPreviews.hover(textItem, row.hoveredLink, row.sourcePath, Workspace, row.linkPoint.x, row.linkPoint.y)
        }
        onHoveredChanged: {
            if (!hovered) {
                LinkPreviews.leave(textItem)
                if (!tap.pressed)
                    row.hoveredLink = ""
            }
        }
    }
    TapHandler {
        id: tap
        gesturePolicy: TapHandler.ReleaseWithinBounds
        acceptedButtons: OpenIn.tabs ? Qt.LeftButton | Qt.MiddleButton : Qt.LeftButton
        onPressedChanged: {
            if (pressed)
                row.trackLink(tap.point.position)
            else if (!hover.hovered)
                row.hoveredLink = ""
        }
        onTapped: (eventPoint, button) => {
            row.linkPoint = textItem.mapFromItem(row, eventPoint.position.x, eventPoint.position.y)
            const link = textItem.linkAt(row.linkPoint.x, row.linkPoint.y)
            if (link === "task:toggle") {
                if (button === Qt.LeftButton)
                    row.taskToggled()
                return
            }
            // Ctrl+click, a middle click: a new tab.
            const target = OpenIn.clickTarget(button, tap.point.modifiers)
            let followed = false
            if (link !== "" && row.tools) {
                OpenIn.begin(target)
                followed = row.tools.openLink(link, row.sourcePath, false)
                OpenIn.end()
            }
            if (!followed) {
                OpenIn.begin(target)
                row.opened(false)
                OpenIn.end()
            }
        }
    }
}
