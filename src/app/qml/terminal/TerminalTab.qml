pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TerminalTab — one tab of TerminalPanel's strip (docs/CODE-TERMINAL.md "The panel"): a
// TerminalGroup's title (its focused terminal's: the program's own, else the panel's label, else
// the command or the shell's name) and its marks — a dot when output came while it was not shown, a
// bell when a program rang one, the number of terminals of a split tab, the exit code once its
// program ended (dimmed then) — and its close button. TerminalPanel's ListView makes one per
// TerminalThread row.
//
//   TerminalTab { current: index === thread.currentIndex; onClicked: …; onCloseRequested: … }
//
// objectNames: terminalTab_<n>, terminalTabTitle_<n>, terminalTabActivity_<n>, terminalTabBell_<n>,
// terminalTabExit_<n>, terminalTabClose_<n>.
T.AbstractButton {
    id: tab

    required property TerminalGroup group
    required property int index
    required property string title
    required property bool activity
    required property bool bellRung
    required property bool hasExited
    required property int exitCode
    required property int paneCount
    /// The tab whose terminals are shown.
    property bool current: false

    /// The × was clicked.
    signal closeRequested()

    objectName: "terminalTab_" + tab.index
    implicitWidth: Math.min(Math.max(implicitContentWidth + leftPadding + rightPadding, 96), 260)
    implicitHeight: Theme.controlHeightSm
    leftPadding: Theme.space3
    rightPadding: Theme.space1
    hoverEnabled: !Theme.touchUi
    focusPolicy: Qt.TabFocus

    Accessible.role: Accessible.PageTab
    Accessible.name: tab.title
    Accessible.description: tab.hasExited ? qsTr("Exited with code %1").arg(tab.exitCode)
                            : tab.bellRung ? qsTr("Rang the bell")
                            : tab.activity ? qsTr("New output") : ""

    contentItem: RowLayout {
        spacing: Theme.space2

        Item {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: Theme.iconSizeSm
            implicitHeight: Theme.iconSizeSm
            BIcon {
                objectName: tab.bellRung ? "terminalTabBell_" + tab.index : ""
                anchors.centerIn: parent
                name: tab.bellRung ? "bell" : "terminal"
                size: Theme.iconSizeSm
                color: tab.bellRung ? Theme.warningText : tab.current ? Theme.accentText : Theme.fgMuted
            }
            // New output since it was shown last.
            Rectangle {
                objectName: "terminalTabActivity_" + tab.index
                visible: tab.activity && !tab.current
                width: 7
                height: 7
                radius: 3.5
                x: parent.width - 4
                y: -2
                color: Theme.accent
                border.width: 1
                border.color: Theme.bg
            }
        }
        Text {
            objectName: "terminalTabTitle_" + tab.index
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            textFormat: Text.PlainText
            text: tab.title
            elide: Text.ElideRight
            font.family: Theme.fontUi
            font.pixelSize: Theme.textSm
            font.weight: tab.current ? Theme.weightSemiBold : Theme.weightNormal
            color: tab.current || tab.hovered ? Theme.fg : Theme.fgMuted
            opacity: tab.hasExited ? 0.6 : 1
        }
        // A split tab: how many terminals.
        Text {
            visible: tab.paneCount > 1
            Layout.alignment: Qt.AlignVCenter
            textFormat: Text.PlainText
            text: "" + tab.paneCount
            font.family: Theme.fontUi
            font.pixelSize: Theme.textXs
            color: Theme.fgMuted
        }
        BBadge {
            objectName: "terminalTabExit_" + tab.index
            visible: tab.hasExited
            Layout.alignment: Qt.AlignVCenter
            //: A terminal tab whose program ended: its exit code
            text: qsTr("exit %1").arg(tab.exitCode)
            variant: tab.exitCode === 0 ? "success" : "danger"
            solid: false
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
