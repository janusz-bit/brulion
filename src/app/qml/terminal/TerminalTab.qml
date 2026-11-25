pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TerminalTab — one tab of TerminalPanel's strip (docs/CODE-TERMINAL.md "The panel"): the
// session's title (the program's own, else the panel's label, else the command or the shell's
// name), dimmed once the program ended, and its close button. TerminalPanel's ListView makes one
// per TerminalSessionModel row.
//
//   TerminalTab { current: index === panel.currentIndex; onClicked: …; onCloseRequested: … }
T.AbstractButton {
    id: tab

    required property TerminalSession session
    required property int index
    /// The tab whose terminal is shown.
    property bool current: false

    /// The × was clicked.
    signal closeRequested()

    objectName: "terminalTab_" + tab.index
    implicitWidth: Math.min(Math.max(implicitContentWidth + leftPadding + rightPadding, 96), 240)
    implicitHeight: Theme.controlHeightSm
    leftPadding: Theme.space3
    rightPadding: Theme.space1
    hoverEnabled: !Theme.touchUi
    focusPolicy: Qt.TabFocus

    Accessible.role: Accessible.PageTab
    Accessible.name: tab.session !== null ? tab.session.title : ""

    contentItem: RowLayout {
        spacing: Theme.space2

        BIcon {
            Layout.alignment: Qt.AlignVCenter
            name: "terminal"
            size: Theme.iconSizeSm
            color: tab.current ? Theme.accentText : Theme.fgMuted
        }
        Text {
            objectName: "terminalTabTitle_" + tab.index
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            textFormat: Text.PlainText
            text: tab.session !== null ? tab.session.title : ""
            elide: Text.ElideRight
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            font.weight: tab.current ? Theme.weightSemiBold : Theme.weightNormal
            color: tab.current || tab.hovered ? Theme.fg : Theme.fgMuted
            opacity: tab.session !== null && tab.session.hasExited ? 0.6 : 1
        }
        BIconButton {
            objectName: "terminalTabClose_" + tab.index
            Layout.alignment: Qt.AlignVCenter
            iconName: "x"
            size: "small"
            label: qsTr("Close terminal")
            onClicked: tab.closeRequested()
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: tab.current ? Theme.bgElevated : tab.down ? Theme.bgPressed : tab.hovered ? Theme.bgHover : "transparent"

        BFocusRing {
            visible: tab.visualFocus
            targetRadius: Theme.radiusSm
            inset: true
        }
    }
}
