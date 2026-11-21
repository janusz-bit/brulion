pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import Brulion

// ShellLiveIndicator — the top bar's "Live" chip (docs/UI.md "Extension items in the UI", "Live
// changes"): shown while someone changed the running app live and has not been undone —
// AppScripting.liveClients, an AI agent over MCP or ACP, the remote control, an extension's
// b.app.set. An accent pill with a bolt and "Live" (a phone's narrow bar: the bolt alone); its
// tooltip says who and how much. A click, a tap or Space open its menu: one row per client ("Claude Code — 3 changes", for reading), "Undo changes
// of <name>" per client when there are several, "Undo all live changes" (AppScripting.revert("")),
// and "Open the console" (route "console": what the agents ran, the Luau console).
//
//   ShellLiveIndicator { Layout.alignment: Qt.AlignVCenter }
T.Button {
    id: chip

    readonly property int clientCount: AppScripting.liveClients.length
    /// The live changes of every client.
    readonly property int changeCount: {
        let n = 0
        for (let i = 0; i < AppScripting.liveClients.length; ++i)
            n += UiModels.numberField(AppScripting.liveClients[i], "changes", 0)
        return n
    }
    /// The clients' names, in the order they changed something first.
    readonly property list<string> names: UiModels.stringFields(AppScripting.liveClients, "name")
    /// Only the bolt (phones' narrow top bar).
    readonly property bool compactChip: Theme.narrow
    readonly property string toolTipText: qsTr("Changed live by %1 (%n change(s))", "", chip.changeCount)
                                          .arg(chip.names.join(", "))

    visible: chip.clientCount > 0
    // The pill stays small inside a touch target (`slack`: the room around it on each side).
    readonly property real pillPaddingLeft: chip.compactChip ? Theme.space2 : Theme.space2 + 2
    readonly property real pillPaddingRight: chip.compactChip ? Theme.space2 : Theme.space3
    readonly property real slack: Math.max(0, ((Theme.touchUi ? Theme.touchTarget : 0) - implicitContentWidth
                                               - chip.pillPaddingLeft - chip.pillPaddingRight) / 2)
    implicitHeight: Math.max(Theme.controlHeightSm, Theme.touchUi ? Theme.touchTarget : 0)
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    topInset: (implicitHeight - Theme.controlHeightSm + 6) / 2
    bottomInset: topInset
    leftInset: chip.slack
    rightInset: chip.slack
    leftPadding: chip.pillPaddingLeft + chip.slack
    rightPadding: chip.pillPaddingRight + chip.slack
    hoverEnabled: !Theme.touchUi
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Live changes")
    Accessible.description: chip.toolTipText
    onClicked: chip.openMenu()

    background: Rectangle {
        radius: Theme.radiusFull
        color: chip.down ? Theme.accentBg.darker(1.15) : chip.hovered ? Theme.accentBg.darker(1.08) : Theme.accentBg
        border.width: 1
        border.color: Theme.accent
        BFocusRing {
            visible: chip.visualFocus
            targetRadius: Theme.radiusFull
        }
    }
    contentItem: Row {
        spacing: Theme.space1
        BIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: "zap"
            size: Theme.iconSizeSm
            color: Theme.accentText
        }
        BText {
            anchors.verticalCenter: parent.verticalCenter
            visible: !chip.compactChip
            text: qsTr("Live")
            variant: "small"
            font.weight: Theme.weightSemiBold
            color: Theme.accentText
            Accessible.ignored: true
        }
    }

    // Touch: a long press shows the tooltip (there is no hover).
    property bool touchTip: false
    onPressAndHold: if (Theme.touchUi) chip.touchTip = true
    /// Its menu is open (no tooltip over it).
    readonly property bool menuOpen: chip.menuPopup !== null && chip.menuPopup.visible
    Loader {
        active: (chip.hovered || chip.visualFocus || chip.touchTip) && !chip.menuOpen
        sourceComponent: BToolTip {
            parent: chip
            text: chip.toolTipText
            below: true
            visible: ((chip.hovered && !chip.down) || chip.visualFocus) && !Theme.touchUi || chip.touchTip
            onClosed: chip.touchTip = false
        }
    }

    property BMenu menuPopup: null
    function openMenu(): void {
        if (!chip.menuPopup)
            chip.menuPopup = menuComponent.createObject(chip) as BMenu
        chip.menuPopup.popup(chip, chip.width - chip.menuPopup.width, chip.height)
    }
    Component {
        id: menuComponent
        BMenu {
            id: menu
            objectName: "liveMenu"
            // Who changed what (for reading).
            Instantiator {
                model: AppScripting.liveClients
                delegate: BMenuItem {
                    id: clientRow
                    required property string name
                    required property int changes
                    objectName: "liveClient"
                    text: qsTr("%1 — %n change(s)", "", clientRow.changes).arg(clientRow.name)
                    iconName: "zap"
                    enabled: false
                }
                onObjectAdded: (index, object) => menu.insertItem(index, object as Item)
                onObjectRemoved: (index, object) => {
                    for (let i = 0; i < menu.count; ++i) {
                        if (menu.itemAt(i) === object as Item) {
                            menu.takeItem(i)
                            return
                        }
                    }
                }
            }
            BMenuSeparator { id: undoAt }
            // One client's changes undone (with two clients or more; one: "Undo all" is that).
            Instantiator {
                model: chip.clientCount > 1 ? AppScripting.liveClients : chip.noClients
                delegate: BMenuItem {
                    id: undoOne
                    required property string who
                    required property string name
                    objectName: "liveUndoClient"
                    text: qsTr("Undo changes of %1").arg(undoOne.name)
                    iconName: "undo-2"
                    onTriggered: AppScripting.revert(undoOne.who)
                }
                onObjectAdded: (index, object) => menu.insertItem(menu.indexAfter(undoAt) + index, object as Item)
                onObjectRemoved: (index, object) => {
                    for (let i = 0; i < menu.count; ++i) {
                        if (menu.itemAt(i) === object as Item) {
                            menu.takeItem(i)
                            return
                        }
                    }
                }
            }
            /// The index after `item` in the menu (its count when it is none of its items).
            function indexAfter(item: Item): int {
                for (let i = 0; i < menu.count; ++i) {
                    if (menu.itemAt(i) === item)
                        return i + 1
                }
                return menu.count
            }
            BMenuItem {
                objectName: "liveUndoAll"
                text: qsTr("Undo all live changes")
                iconName: "undo-2"
                onTriggered: AppScripting.revert("")
            }
            BMenuSeparator {}
            BMenuItem {
                objectName: "liveConsole"
                text: qsTr("Open the console")
                iconName: "terminal"
                onTriggered: AppController.navigate({ view: "console" })
            }
        }
    }
    readonly property list<var> noClients: []
}
