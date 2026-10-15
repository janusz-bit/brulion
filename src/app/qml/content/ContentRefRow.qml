pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentRefRow — one block that refers to something (a backlink, a tagged block, an unlinked
// mention): its parents as a small breadcrumb, then the block's text as rendered by the core
// InlineRenderer (escaped rich text; links, tags and chips are clickable), optionally an action
// button on the right ("Połącz").
//
//   ContentRefRow {
//       html: model.html; plain: model.text; breadcrumb: model.breadcrumb
//       sourcePath: model.path; line: model.line; taskStatus: model.taskStatus
//       onOpened: (newPane) => AppController.openPage(model.path, model.line, newPane)
//       onTaskToggled: refs.toggleTask(index)
//   }
//
// A click on a link follows it (`tools`: PageTools.openLink, resolved from `sourcePath`), a click on the
// task checkbox emits taskToggled(), a click anywhere else opens the block in its page.
// Keyboard: focusable, Enter opens, Ctrl+Enter opens in the side panel (wide windows).
FocusScope {
    id: row

    property string html
    property string plain
    property string breadcrumb
    property string taskStatus
    property string sourcePath
    property int line: -1
    /// Smaller text (side panel).
    property bool compact: false
    /// Text of the action button on the right ("" = none).
    property string actionText
    property string actionIcon
    property bool actionBusy: false
    /// Follows links in the text (one shared instance per list).
    property PageTools tools: null

    signal opened(bool newPane)
    signal taskToggled()
    signal actionTriggered()

    readonly property bool closed: taskStatus === "done" || taskStatus === "cancelled"
    readonly property int fontSize: compact ? Theme.textBase : Theme.textMd

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
        anchors.leftMargin: Theme.space2
        anchors.rightMargin: Theme.space2
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
                visible: row.taskStatus !== ""
                anchors.centerIn: parent
                status: row.taskStatus || "todo"
                onToggled: row.taskToggled()
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
            Text {
                id: textItem
                Layout.fillWidth: true
                textFormat: Text.RichText
                text: row.html
                wrapMode: Text.Wrap
                maximumLineCount: row.compact ? 6 : 12
                elide: Text.ElideRight
                font.family: Theme.fontEditor
                font.pixelSize: row.fontSize
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.lineHeightPx(row.fontSize, Theme.lineHeight)
                color: row.closed ? Theme.fgFaint : Theme.fg
                linkColor: Theme.link
                font.strikeout: row.taskStatus === "cancelled"
                Accessible.ignored: true
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

    HoverHandler {
        id: hover
        enabled: !Theme.touchUi
        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        id: tap
        gesturePolicy: TapHandler.ReleaseWithinBounds
        onTapped: (eventPoint, button) => {
            const p = textItem.mapFromItem(row, eventPoint.position.x, eventPoint.position.y)
            const link = textItem.linkAt(p.x, p.y)
            if (link === "task:toggle") {
                row.taskToggled()
                return
            }
            if (link !== "" && row.tools && row.tools.openLink(link, row.sourcePath, false))
                return
            row.opened((tap.point.modifiers & Qt.ControlModifier) !== 0 && !Theme.narrow)
        }
    }
}
