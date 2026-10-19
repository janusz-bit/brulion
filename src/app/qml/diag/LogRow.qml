pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// LogRow — one entry of the event log (LogsView's list): the time, the level (a coloured letter
// with an icon — colour is never the only signal), the area and the message as plain text
// (never rich text: messages may quote note names and server answers). Repeats show "×37".
// Wide: the meta data in a fixed-width column in front of the message; phones: above it. All
// geometry is bindings, so the height is final at creation (docs/UI.md "Scrolling performance").
// Long press / right click: copyRequested() (the view offers "Copy line").
Item {
    id: row

    required property int index
    required property string timeText
    required property string level
    required property string letter
    required property string message
    required property int repeat
    required property bool earlier

    property string levelText
    property string groupText
    property bool stacked: Theme.compact
    /// Width of "00:00:00.000" in the mono font (measured once by the view).
    property real timeWidth: 80
    property real groupWidth: 96

    signal copyRequested(int index)

    readonly property bool problem: level === "error" || level === "fatal" || level === "warning"
    readonly property color levelColor: level === "error" || level === "fatal" ? Theme.dangerText
                                      : level === "warning" ? Theme.warningText
                                      : level === "debug" ? Theme.fgFaint : Theme.infoText
    readonly property string levelIcon: level === "error" || level === "fatal" ? "circle-alert"
                                      : level === "warning" ? "triangle-alert"
                                      : level === "debug" ? "" : "info"
    readonly property int pad: Theme.touchUi ? Theme.space2 : 3
    readonly property int metaHeight: Theme.lineHeightPx(Theme.textSm)
    readonly property real levelX: Theme.space3 + timeWidth + Theme.space2
    readonly property real groupX: levelX + Theme.iconSizeSm + 14 + Theme.space2
    readonly property real badgeX: groupX + (stacked ? groupLabel.implicitWidth : groupWidth) + Theme.space2
    readonly property real metaEnd: badgeX + (repeat > 1 ? badge.width + Theme.space2 : 0)

    width: ListView.view ? ListView.view.width : 0
    height: (stacked ? metaHeight + 2 + messageText.height : Math.max(metaHeight, messageText.height)) + 2 * pad

    Accessible.role: Accessible.ListItem
    Accessible.name: levelText + ", " + timeText + ", " + groupText + ": " + message + (repeat > 1 ? ", ×" + repeat : "")

    Rectangle {
        anchors.fill: parent
        color: row.level === "error" || row.level === "fatal" ? Theme.dangerBg
             : row.level === "warning" ? Theme.warningBg
             : hover.hovered ? Theme.bgHover : "transparent"
        opacity: row.problem ? 0.6 : 1
    }

    Text {
        x: Theme.space3
        y: row.pad
        width: row.timeWidth
        height: row.metaHeight
        verticalAlignment: Text.AlignVCenter
        text: row.timeText
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        color: row.earlier ? Theme.fgFaint : Theme.fgMuted
    }
    BIcon {
        x: row.levelX
        y: row.pad + (row.metaHeight - height) / 2
        name: row.levelIcon
        visible: row.levelIcon !== ""
        size: Theme.iconSizeSm
        color: row.levelColor
    }
    Text {
        x: row.levelX + Theme.iconSizeSm + 2
        y: row.pad
        height: row.metaHeight
        verticalAlignment: Text.AlignVCenter
        text: row.letter
        font.family: Theme.fontMono
        font.pixelSize: Theme.textXs
        font.weight: Theme.weightSemiBold
        color: row.levelColor
    }
    Text {
        id: groupLabel
        x: row.groupX
        y: row.pad
        width: row.stacked ? implicitWidth : row.groupWidth
        height: row.metaHeight
        verticalAlignment: Text.AlignVCenter
        text: row.groupText
        font.family: Theme.fontUi
        font.pixelSize: Theme.textXs
        color: Theme.fgMuted
        elide: Text.ElideRight
    }
    Rectangle {
        id: badge
        x: row.badgeX
        y: row.pad + (row.metaHeight - height) / 2
        visible: row.repeat > 1
        width: repeatText.implicitWidth + 8
        height: repeatText.implicitHeight + 2
        radius: Theme.radiusSm
        color: Theme.chipNeutralBg
        Text {
            id: repeatText
            anchors.centerIn: parent
            text: "×" + row.repeat
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            color: Theme.fgMuted
        }
    }

    Text {
        id: messageText
        x: row.stacked ? Theme.space3 : row.metaEnd + Theme.space1
        y: row.stacked ? row.pad + row.metaHeight + 2 : row.pad
        width: Math.max(40, row.width - x - Theme.space3)
        text: row.message
        textFormat: Text.PlainText
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        maximumLineCount: 12
        elide: Text.ElideRight
        font.family: Theme.fontMono
        font.pixelSize: Theme.textSm
        color: row.earlier ? Theme.fgMuted : Theme.fg
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.lineHeightPx(Theme.textSm)
    }

    HoverHandler {
        id: hover
        enabled: !Theme.touchUi
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        onTapped: row.copyRequested(row.index)
    }
    TapHandler {
        onLongPressed: row.copyRequested(row.index)
    }
}
