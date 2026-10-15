pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SyncView — route "sync" ({tab?: "overview" | "syncthing" | "nextcloud" | "conflicts", copy?: a
// conflict copy to open}): the sync center.
//   Przegląd   the two ways explained in plain words (Syncthing: a folder kept the same by
//              Syncthing on every device; Nextcloud: your own server over WebDAV, also in the
//              browser), their state, waiting conflicts and the mass-deletion guard
//   Syncthing  steps, live status, the recommended .stignore, REST access (qml/io/SyncthingPanel)
//   Nextcloud  status with "Synchronizuj teraz", progress, the last report, offline / error
//              explanations, the deletion guard, the account form (qml/io/NextcloudForm)
//   Konflikty  conflict copies (list) and the resolver (qml/io/SyncConflictResolver): side by side
//              on wide windows, one after the other on narrow ones
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Sync")
    readonly property var tabIds: ["overview", "syncthing", "nextcloud", "conflicts"]
    readonly property var sync: Workspace.sync
    readonly property int conflictCount: sync ? sync.conflictCount : 0
    readonly property int tabPadding: Theme.compact ? 9 : (Theme.touchUi ? 14 : 12)
    /// The conflict the user (or the route) opened.
    property string chosenCopy: ""
    readonly property var conflicts: sync ? sync.conflicts : []
    readonly property bool chosenListed: chosenCopy !== "" && conflicts.some(c => c.copy === chosenCopy)
    /// The conflict shown in the resolver: the chosen one while it exists; wide windows show the
    /// first one otherwise, narrow ones the list.
    readonly property string currentCopy: chosenListed ? chosenCopy
                                        : (!Theme.narrow && conflicts.length > 0 ? conflicts[0].copy : "")

    function showTab(id) {
        const i = tabIds.indexOf(id)
        if (i >= 0)
            tabs.currentIndex = i
    }
    function applyRoute() {
        if (!route)
            return
        if (route.copy) {
            chosenCopy = route.copy
            showTab("conflicts")
        } else if (route.tab) {
            showTab(route.tab)
        }
    }

    onRouteChanged: applyRoute()
    Component.onCompleted: applyRoute()

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    /// A scrolling page with a centred column.
    component Page: Flickable {
        id: page
        default property alias content: column.data
        readonly property int margin: Theme.compact ? Theme.space4 : Theme.space8
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * margin + (Theme.narrow ? 56 + Theme.space4 : 0)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}
        ColumnLayout {
            id: column
            x: Math.max(page.margin, Math.round((page.width - width) / 2))
            y: page.margin
            width: Math.min(page.width - 2 * page.margin, 860)
            spacing: Theme.space5
        }
    }

    /// A short line with a dot.
    component Bullet: RowLayout {
        id: bullet
        property string text
        Layout.fillWidth: true
        spacing: Theme.space2
        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Math.round(Theme.lineHeightPx(Theme.textBase) / 2) - 2
            implicitWidth: 5
            implicitHeight: 5
            radius: 2.5
            color: Theme.fgFaint
        }
        BText {
            Layout.fillWidth: true
            text: bullet.text
            wrapMode: Text.Wrap
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        BTabBar {
            id: tabs
            Layout.fillWidth: true
            leftPadding: Theme.compact ? Theme.space1 : Theme.space6
            fadeColor: Theme.bg
            // Phones: four tabs without icons fit the width (the conflict count stays visible).
            BTabButton { text: qsTr("Overview"); iconName: Theme.compact ? "" : "folder-sync"; leftPadding: root.tabPadding; rightPadding: root.tabPadding }
            BTabButton { text: "Syncthing"; iconName: Theme.compact ? "" : "laptop"; leftPadding: root.tabPadding; rightPadding: root.tabPadding }
            BTabButton { text: "Nextcloud"; iconName: Theme.compact ? "" : "cloud"; leftPadding: root.tabPadding; rightPadding: root.tabPadding }
            BTabButton { text: qsTr("Conflicts"); iconName: Theme.compact ? "" : "git-merge"; count: root.conflictCount; leftPadding: root.tabPadding; rightPadding: root.tabPadding }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            // ---- Overview ---------------------------------------------------------------------
            Page {
                BText {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    variant: "muted"
                    text: Workspace.isOpen
                          ? qsTr("Brulion keeps your notes as plain Markdown files in the folder “%1”. To have the same notes on several devices, choose one of two ways:").arg(Workspace.name)
                          : qsTr("Open a notebook to set up syncing.")
                }

                SyncDeletionGuard {}

                GridLayout {
                    Layout.fillWidth: true
                    columns: Theme.compact ? 1 : 2
                    columnSpacing: Theme.space4
                    rowSpacing: Theme.space4

                    IoCard {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        iconName: "laptop"
                        title: "Syncthing"
                        subtitle: qsTr("Without a server: your devices exchange the files directly.")
                        trailing: [
                            BBadge {
                                visible: !AppController.isWasm && (Workspace.syncthingFolder || !!(root.sync && root.sync.syncthing.enabled))
                                text: root.sync && root.sync.syncthing.state === "idle" ? qsTr("Up to date") : qsTr("Shared")
                                variant: "success"
                            }
                        ]
                        Bullet { text: qsTr("For computers and Android phones (Syncthing-Fork).") }
                        Bullet { text: qsTr("Works in your home Wi-Fi even without internet.") }
                        Bullet { text: AppController.isWasm ? qsTr("Not available in the browser.") : qsTr("Nothing to sign up for; your notes never leave your devices.") }
                        Item { Layout.fillHeight: true }
                        BButton {
                            enabled: !AppController.isWasm
                            text: Workspace.syncthingFolder ? qsTr("Show the status") : qsTr("Set up Syncthing")
                            iconName: "arrow-right"
                            iconRight: true
                            onClicked: root.showTab("syncthing")
                        }
                    }
                    IoCard {
                        Layout.preferredWidth: 1
                        Layout.fillHeight: true
                        iconName: "cloud"
                        title: "Nextcloud"
                        subtitle: qsTr("Through your own server (WebDAV) – also in the browser.")
                        trailing: [
                            BBadge {
                                visible: !!root.sync && root.sync.state !== "disabled"
                                text: {
                                    switch (root.sync ? root.sync.state : "") {
                                    case "syncing": return qsTr("Syncing")
                                    case "offline": return qsTr("Offline")
                                    case "error": return qsTr("Problem")
                                    default: return qsTr("On")
                                    }
                                }
                                variant: {
                                    switch (root.sync ? root.sync.state : "") {
                                    case "syncing": return "info"
                                    case "offline": return "warning"
                                    case "error": return "danger"
                                    default: return "success"
                                    }
                                }
                            }
                        ]
                        Bullet { text: qsTr("Needs a Nextcloud account and an app password.") }
                        Bullet { text: qsTr("Syncs every few minutes and right after your edits.") }
                        Bullet { text: qsTr("Changes from several devices are merged line by line.") }
                        BText {
                            Layout.fillWidth: true
                            visible: !!root.sync && root.sync.statusText !== ""
                            text: root.sync ? root.sync.statusText : ""
                            variant: "small"
                            color: root.sync && root.sync.state === "error" ? Theme.dangerText : Theme.fgMuted
                            wrapMode: Text.Wrap
                        }
                        Item { Layout.fillHeight: true }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            BButton {
                                text: root.sync && root.sync.configured ? qsTr("Settings") : qsTr("Set up Nextcloud")
                                iconName: "arrow-right"
                                iconRight: true
                                onClicked: root.showTab("nextcloud")
                            }
                            BButton {
                                visible: !!root.sync && root.sync.active
                                text: qsTr("Sync now")
                                iconName: "refresh-cw"
                                variant: "primary"
                                busy: !!root.sync && root.sync.state === "syncing"
                                onClicked: root.sync.syncNow()
                            }
                        }
                    }
                }

                IoCard {
                    visible: root.conflictCount > 0
                    tone: "warning"
                    iconName: "git-merge"
                    iconColor: Theme.warningText
                    title: qsTr("%n note(s) changed on two devices at once", "", root.conflictCount)
                    subtitle: qsTr("Both versions were kept. Merge them so that only one remains.")
                    BButton {
                        text: qsTr("Merge")
                        iconName: "git-merge"
                        variant: "primary"
                        onClicked: root.showTab("conflicts")
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon { Layout.alignment: Qt.AlignTop; name: "info"; size: Theme.iconSize; color: Theme.fgFaint }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Use one way per notebook: a folder synced by both Syncthing and Nextcloud sends every change twice and causes needless conflicts.")
                        variant: "small"
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                }
            }

            // ---- Syncthing --------------------------------------------------------------------
            Page {
                SyncthingPanel {
                    Layout.fillWidth: true
                }
            }

            // ---- Nextcloud --------------------------------------------------------------------
            Page {
                SyncDeletionGuard {}
                SyncStatusPanel {
                    visible: !!root.sync && (root.sync.configured || root.sync.state !== "disabled")
                    onShowConflicts: root.showTab("conflicts")
                }
                NextcloudForm {}
            }

            // ---- Conflicts --------------------------------------------------------------------
            Item {
                id: conflictsPage

                BEmptyState {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                    visible: root.conflictCount === 0
                    iconName: "git-merge"
                    title: qsTr("No conflicts")
                    description: qsTr("When a note changes on two devices at the same time, the other version is kept as a copy and appears here to be merged.")
                }

                RowLayout {
                    anchors.fill: parent
                    visible: root.conflictCount > 0
                    spacing: 0

                    // The list: always on wide windows, alone on narrow ones until one is opened.
                    Rectangle {
                        Layout.fillHeight: true
                        Layout.fillWidth: Theme.narrow
                        Layout.preferredWidth: Theme.narrow ? -1 : 300
                        visible: !Theme.narrow || root.currentCopy === ""
                        color: Theme.narrow ? Theme.bg : Theme.bgSunken

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            BText {
                                Layout.fillWidth: true
                                Layout.margins: Theme.space4
                                Layout.bottomMargin: Theme.space2
                                text: qsTr("%n note(s) with two versions", "", root.conflictCount)
                                variant: "label"
                                color: Theme.fgMuted
                                wrapMode: Text.Wrap
                            }
                            SyncConflictList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.leftMargin: Theme.space2
                                Layout.rightMargin: Theme.space2
                                currentCopy: root.currentCopy
                                onActivated: (copy) => root.chosenCopy = copy
                            }
                        }
                        Rectangle {
                            anchors.right: parent.right
                            width: 1
                            height: parent.height
                            visible: !Theme.narrow
                            color: Theme.borderSubtle
                        }
                    }

                    SyncConflictResolver {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.margins: Theme.compact ? Theme.space4 : Theme.space6
                        // The floating capture button sits at the right; the resolver's buttons
                        // start at the left, so only a normal margin is needed.
                        Layout.bottomMargin: Theme.compact ? Theme.space4 : Theme.space6
                        visible: root.currentCopy !== ""
                        copy: root.currentCopy
                        showBack: Theme.narrow
                        onClosed: root.chosenCopy = ""
                    }
                    Item {
                        Layout.fillWidth: true
                        visible: !Theme.narrow && root.currentCopy === ""
                    }
                }
            }
        }
    }
}
