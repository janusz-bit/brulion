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

    readonly property var route: AppController.currentRoute
    /// The views offered, without the ones hidden in the settings.
    readonly property var entries: [
        { view: "journals", also: "journal", title: qsTr("Journals"), icon: "book-open" },
        { view: "calendar", title: qsTr("Calendar"), icon: "calendar" },
        { view: "tasks", title: qsTr("Tasks"), icon: "list-todo" },
        { view: "habits", title: qsTr("Habits"), icon: "flame" },
        { view: "allPages", title: qsTr("All pages"), icon: "files" },
        { view: "tags", also: "tag", title: qsTr("Tags"), icon: "tags" },
        { view: "graph", title: qsTr("Graph"), icon: "waypoints" },
        { view: "search", title: qsTr("Search"), icon: "search" }
    ].filter(e => AppSettings.sidebarHidden.indexOf(e.view) < 0)
    /// The buttons' squares and the room on both sides of them.
    readonly property int buttonSize: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight
    readonly property int sidePadding: Theme.space2

    function go(route) {
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
                        text: (Workspace.name || "B").trim().charAt(0).toUpperCase()
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
                        required property var modelData
                        objectName: "sidebarNav_" + modelData.view
                        label: modelData.title
                        iconName: modelData.icon
                        shortcut: ShortcutMap.sequences["go." + modelData.view] || ""
                        selected: rail.route.view === modelData.view
                                  || (modelData.also !== undefined && rail.route.view === modelData.also)
                        onClicked: rail.go({ view: modelData.view })
                    }
                }
                // Import (the drawer on phones: notes from other apps, docs/UI.md).
                RailButton {
                    objectName: "drawerImportNotes"
                    visible: Theme.narrow
                    label: qsTr("Import notes")
                    iconName: "download"
                    selected: rail.route.view === "importExport"
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
            selected: rail.route.view === "settings"
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
            model: AppSettings.recentVaults.filter(v => v.path !== notebookMenu.docsPath)
            delegate: BMenuItem {
                required property var modelData
                objectName: "notebookMenuRecent"
                // Not `checked`: a checkable item has already toggled it when onTriggered runs,
                // so the first click on another notebook would do nothing.
                readonly property bool isCurrent: modelData.path === notebookMenu.current
                text: modelData.name
                iconName: "folder"
                checkable: true
                checked: isCurrent
                onTriggered: {
                    checked = Qt.binding(() => isCurrent)
                    if (!isCurrent)
                        AppController.openRecentVault(modelData.path)
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
            readonly property bool isCurrent: notebookMenu.docsPath === notebookMenu.current
            checkable: true
            checked: isCurrent
            onTriggered: {
                checked = Qt.binding(() => isCurrent)
                if (!isCurrent)
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
