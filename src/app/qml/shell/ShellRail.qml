pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellRail — the sidebar's narrow column of views (docs/UI.md "Sidebar"). At the top the
// notebook: its first letter, a click opens the notebook switcher (the recent notebooks, the
// documentation, another folder). Then the views as icon buttons, their names in tooltips:
// Dziennik, Kalendarz, Zadania, Nawyki, Wszystkie strony, Tagi, Graf, Szukaj (without the ones
// hidden in Ustawienia → Wygląd → Układ; on phones also Importuj notatki). They scroll when the
// window is too low for all. At the bottom the sync status (ShellSyncFooter, compact) and
// Ustawienia. On wide windows the rail stays at the window's left edge when the page panel
// (ShellSidebar) is hidden; on phones it is the drawer's left column (`navigated()` closes it).
Rectangle {
    id: rail

    property real topInset: 0
    property real bottomInset: 0

    signal navigated()

    readonly property string routeView: AppController.currentView
    /// The views offered (the ones hidden in the settings are not shown: AppSettings.sidebarHidden).
    readonly property list<var> entries: [
        { entryView: "journals", entryAlso: "journal", entryTitle: qsTr("Journals"), entryIcon: "book-open" },
        { entryView: "calendar", entryAlso: "", entryTitle: qsTr("Calendar"), entryIcon: "calendar" },
        { entryView: "tasks", entryAlso: "", entryTitle: qsTr("Tasks"), entryIcon: "list-todo" },
        { entryView: "habits", entryAlso: "", entryTitle: qsTr("Habits"), entryIcon: "flame" },
        { entryView: "allPages", entryAlso: "", entryTitle: qsTr("All pages"), entryIcon: "files" },
        { entryView: "tags", entryAlso: "tag", entryTitle: qsTr("Tags"), entryIcon: "tags" },
        { entryView: "graph", entryAlso: "", entryTitle: qsTr("Graph"), entryIcon: "waypoints" },
        { entryView: "search", entryAlso: "", entryTitle: qsTr("Search"), entryIcon: "search" }
    ]
    /// The buttons' squares and the room on both sides of them.
    readonly property int buttonSize: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight
    readonly property int sidePadding: Theme.space2

    function go(route: var): void {
        AppController.navigate(route)
        rail.navigated()
    }

    implicitWidth: buttonSize + 2 * sidePadding
    color: Theme.bgSunken
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Views")

    // The line between the rail and the page panel (or the page, the panel hidden).
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.borderSubtle
    }

    /// One view: its icon, the selected one tinted with the accent's bar at the rail's edge.
    component RailButton: BIconButton {
        id: button
        property bool selected: false
        checked: selected
        toolTipBeside: true
        Layout.alignment: Qt.AlignHCenter
        iconColor: selected || hovered || down ? Theme.fg : Theme.fgMuted
        Accessible.checked: selected
        Rectangle {
            visible: button.selected
            // (Out to the rail's edge: the button is centred in it.)
            x: -button.x
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: Math.round(parent.height * 0.5)
            radius: 2
            color: Theme.accent
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: rail.topInset + Theme.space2
        anchors.bottomMargin: rail.bottomInset + Theme.space2
        anchors.rightMargin: 1
        spacing: Theme.space1

        // ---- The notebook: its switcher ----------------------------------------------------
        T.Button {
            id: notebookButton
            objectName: "notebookSwitch"
            /// The accessible name and what UI tests look for.
            readonly property string label: qsTr("Switch notebook")
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.space1
            implicitWidth: rail.buttonSize
            implicitHeight: rail.buttonSize
            hoverEnabled: !Theme.touchUi
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: label
            Accessible.description: Workspace.name
            onClicked: notebookMenu.popup(notebookButton, notebookButton.width + Theme.space1, 0)

            background: Rectangle {
                radius: Theme.radius
                color: notebookButton.down ? Theme.bgPressed : notebookButton.hovered ? Theme.bgHover : "transparent"
                BFocusRing {
                    visible: notebookButton.visualFocus
                    targetRadius: Theme.radius
                }
            }
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: Math.round(rail.buttonSize * 0.72)
                    height: width
                    radius: Theme.radius
                    color: Theme.accentSolid
                    BText {
                        anchors.centerIn: parent
                        text: Js.toUpperCase(Js.charAt(Js.trim(Workspace.name !== "" ? Workspace.name : "B"), 0))
                        color: Theme.fgOnAccent
                        font.weight: Theme.weightSemiBold
                        font.pixelSize: Math.round(parent.height * 0.52)
                        Accessible.ignored: true
                    }
                }
            }
            // No tooltip: the panel beside it names the notebook, and the window's corner is where
            // a pointer rests when the app starts (a tooltip would greet every start).
        }

        // ---- The views (scrolling when the window is too low) -------------------------------
        Flickable {
            id: entriesView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: entriesColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height

            ColumnLayout {
                id: entriesColumn
                width: entriesView.width
                spacing: Theme.space1

                Repeater {
                    model: rail.entries
                    delegate: RailButton {
                        id: entry
                        required property string entryView
                        required property string entryAlso
                        required property string entryTitle
                        required property string entryIcon
                        objectName: "sidebarNav_" + entryView
                        // Hidden in the settings: not shown (the layout leaves no gap).
                        visible: AppSettings.sidebarHidden.indexOf(entryView) < 0
                        label: entryTitle
                        iconName: entryIcon
                        shortcut: AppController.routeText(ShortcutMap.sequences, "go." + entryView)
                        selected: rail.routeView === entryView || (entryAlso !== "" && rail.routeView === entryAlso)
                        onClicked: rail.go({ view: entry.entryView })
                    }
                }
                // Import (the drawer on phones: notes from other apps, docs/UI.md).
                RailButton {
                    objectName: "drawerImportNotes"
                    visible: Theme.narrow
                    label: qsTr("Import notes")
                    iconName: "download"
                    selected: rail.routeView === "importExport"
                    onClicked: rail.go({ view: "importExport", tab: "import" })
                }
            }
        }

        // ---- Sync status and settings ------------------------------------------------------
        ShellSyncFooter {
            compact: true
            Layout.alignment: Qt.AlignHCenter
            onClicked: rail.go({ view: "sync" })
        }
        RailButton {
            objectName: "railSettings"
            label: qsTr("Settings")
            iconName: "settings"
            shortcut: ShortcutMap.sequences["go.settings"] || ""
            selected: rail.routeView === "settings"
            onClicked: rail.go({ view: "settings" })
        }
    }

    // Quick switch between notebooks: the recent ones, the documentation, any other folder.
    BMenu {
        id: notebookMenu
        objectName: "notebookMenu"

        readonly property string current: AppSettings.currentVault
        readonly property string docsPath: AppController.docsVaultPath

        Repeater {
            // (The documentation is the item below: its row is not shown.)
            model: AppSettings.recentVaults
            delegate: BMenuItem {
                id: recent
                required property string path
                required property string name
                objectName: "notebookMenuRecent"
                visible: path !== notebookMenu.docsPath
                height: visible ? implicitHeight : 0
                // Not `checked`: a checkable item has already toggled it when onTriggered runs,
                // so the first click on another notebook would do nothing. (A Binding: it
                // follows isCurrent again after the toggle; onTriggered puts it back at once.)
                readonly property bool isCurrent: path === notebookMenu.current
                text: name
                iconName: "folder"
                checkable: true
                Binding on checked { value: recent.isCurrent }
                onTriggered: {
                    recent.checked = recent.isCurrent
                    if (!recent.isCurrent)
                        AppController.openRecentVault(recent.path)
                    rail.navigated()
                }
            }
        }
        BMenuSeparator {}
        BMenuItem {
            objectName: "notebookMenuDocs"
            visible: notebookMenu.docsPath !== ""
            height: visible ? implicitHeight : 0
            text: qsTr("Brulion documentation")
            iconName: "book-open"
            id: docsItem
            readonly property bool isCurrent: notebookMenu.docsPath === notebookMenu.current
            checkable: true
            Binding on checked { value: docsItem.isCurrent }
            onTriggered: {
                docsItem.checked = docsItem.isCurrent
                if (!docsItem.isCurrent)
                    AppController.openDocsVault()
                rail.navigated()
            }
        }
        BMenuItem {
            text: qsTr("Open another notebook…")
            iconName: "folder-open"
            onTriggered: {
                rail.navigated()
                AppController.closeVault()
            }
        }
    }
}
