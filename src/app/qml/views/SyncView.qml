pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// SyncView — route "sync" ({tab?: "overview" | "syncthing" | "nextcloud" | "github" | "calendars" | "conflicts",
// copy?: a conflict copy to open, run?: a sync run whose details to open}): the sync center.
//   Overview   the two ways explained in plain words (Syncthing: a folder kept the same by
//              Syncthing on every device; Nextcloud: your own server over WebDAV, also in the
//              browser), their state, a notebook kept on GitHub, waiting conflicts and the
//              mass-deletion guard
//   Syncthing  Android, a notebook in the app's storage: the folder of the phone it syncs with
//              (qml/io/DeviceFolderPanel; with one, the sync's status and history below it);
//              steps, live status, the recommended .stignore, REST access (qml/io/SyncthingPanel)
//   Nextcloud  status with "Sync now", live progress, the last report, offline / error
//              explanations, the deletion guard, the history of runs with their details
//              (qml/io/SyncHistoryList, SyncRunDetails), the way to the event log, the account
//              form (qml/io/NextcloudForm)
//   GitHub     a notebook kept in a folder of a GitHub repository: get the newest version, commit,
//              propose changes as a pull request, the notes not on GitHub yet, the token
//              (qml/io/GitHubPanel; docs/UI.md "Notebooks from GitHub")
//   Calendars  ICS subscriptions and CalDAV accounts: their state, colours, what each shows and
//              syncs, what the runs did (qml/io/CalendarsPanel; docs/UI.md "Calendars")
//   Conflicts  conflict copies (list) and the resolver (qml/io/SyncConflictResolver): side by side
//              on wide windows, one after the other on narrow ones — there a conflict opens
//              through the route ({tab: "conflicts", copy}), so Back (the top bar's ←, Android's)
//              returns to the list, not out of the sync center, and a resolved one goes back too
//
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string title: qsTr("Sync")
    readonly property list<string> tabIds: ["overview", "syncthing", "nextcloud", "github", "calendars", "conflicts"]
    readonly property WorkspaceGitHub github: Workspace.github
    readonly property WorkspaceSync sync: Workspace.sync
    readonly property int conflictCount: sync ? sync.conflictCount : 0
    readonly property string syncState: sync ? sync.state : ""
    readonly property string syncStatusText: sync ? sync.statusText : ""
    /// The notebook syncs with a folder of this phone (not Nextcloud): its status is the sync's.
    readonly property bool deviceFolder: sync ? sync.deviceFolder : false
    /// Nextcloud's card and tab: the sync's state only while it is Nextcloud's.
    readonly property string nextcloudState: deviceFolder ? "disabled" : syncState
    readonly property bool githubLinked: github ? github.linked : false
    readonly property string githubState: github ? github.state : ""
    readonly property int githubChanges: github ? github.changeCount : 0
    readonly property string githubStatusText: github ? github.statusText : ""
    readonly property string githubLinkText: github ? github.linkText : ""
    readonly property int tabPadding: Theme.compact ? 9 : (Theme.touchUi ? 14 : 12)
    /// The conflict the user (or the route) opened.
    property string chosenCopy: ""
    /// The conflict copies (paths), as Workspace.sync.conflicts lists them.
    readonly property list<string> conflictCopies: sync ? sync.conflictCopies : []
    readonly property bool chosenListed: chosenCopy !== "" && conflictCopies.indexOf(chosenCopy) >= 0
    /// The conflict shown in the resolver: the chosen one while it exists; wide windows show the
    /// first one otherwise, narrow ones the list.
    readonly property string currentCopy: chosenListed ? chosenCopy
                                        : (!Theme.narrow && conflictCopies.length > 0 ? conflictCopies[0] : "")

    function showTab(id: string): void {
        const i = tabIds.indexOf(id)
        if (i >= 0)
            tabs.currentIndex = i
    }
    /// Open conflict copy `copy`: beside the list on wide windows; on narrow ones through the
    /// route, the entry Back returns to showing the list (the tab may have been chosen here).
    function openCopy(copy: string): void {
        if (!Theme.narrow) {
            chosenCopy = copy
            return
        }
        if (AppController.routeText(route, "tab") !== "conflicts" || AppController.routeText(route, "copy") !== "")
            AppController.replaceRoute({ view: "sync", tab: "conflicts" })
        AppController.navigate({ view: "sync", tab: "conflicts", copy: copy })
    }
    /// The resolver is done with its conflict (resolved): a narrow window that opened it through
    /// the route goes back (to the list); otherwise the list's next conflict or the list.
    function closeCopy(): void {
        if (Theme.narrow && chosenCopy !== "" && AppController.routeText(route, "copy") === chosenCopy)
            AppController.back()
        else
            chosenCopy = ""
    }
    function applyRoute(): void {
        // The route's conflict copy, every time: a route without one shows the list (Back from a
        // conflict on a narrow window).
        chosenCopy = AppController.routeText(route, "copy")
        if (chosenCopy !== "") {
            showTab("conflicts")
        } else if (AppController.routeFlag(route, "run")) {
            // A run's details (the event log's links, screenshots): on the tab of the sync's way.
            showTab(deviceFolder ? "syncthing" : "nextcloud")
            openRunLater.run = AppController.routeInt(route, "run", 0)
            openRunLater.restart()
        } else if (AppController.routeFlag(route, "tab")) {
            showTab(AppController.routeText(route, "tab"))
        }
    }
    // (Qt.callLater(historyList.open, run))
    Later {
        id: openRunLater
        property int run
        onTriggered: root.deviceFolder ? folderHistoryList.open(run) : historyList.open(run)
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
        TouchFlick.enabled: true
        default property alias content: column.data
        readonly property int margin: Theme.compact ? Theme.space4 : Theme.space8
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * margin
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
            Layout.topMargin: Math.round(Theme.lineHeightPx(Theme.textBase, Theme.lineHeight) / 2) - 2
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

    /// A tab of the bar: no icon on phones.
    component Tab: BTabButton {
        property string symbol
        iconName: Theme.compact ? "" : symbol
        leftPadding: root.tabPadding
        rightPadding: root.tabPadding
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        BTabBar {
            id: tabs
            Layout.fillWidth: true
            leftPadding: Theme.compact ? Theme.space1 : Theme.space6
            fadeColor: Theme.bg
            // Phones: the tabs without icons (the bar scrolls on the narrowest; the conflict count
            // stays visible).
            Tab { text: qsTr("Overview"); symbol: "folder-sync" }
            Tab { text: "Syncthing"; symbol: "laptop" }
            Tab { text: "Nextcloud"; symbol: "cloud" }
            Tab { objectName: "syncTabGitHub"; text: "GitHub"; symbol: "git-branch" }
            Tab { objectName: "syncTabCalendars"; text: qsTr("Calendars"); symbol: "calendar-range" }
            Tab { text: qsTr("Conflicts"); symbol: "git-merge"; count: root.conflictCount }
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
                                visible: !AppController.isWasm && (root.deviceFolder || Workspace.syncthingFolder
                                                                   || (root.sync !== null && AppController.routeFlag(root.sync.syncthing, "enabled")))
                                text: root.deviceFolder ? (root.syncState === "error" ? qsTr("Problem") : qsTr("On"))
                                      : root.sync !== null && AppController.routeText(root.sync.syncthing, "state") === "idle" ? qsTr("Up to date") : qsTr("Shared")
                                variant: root.deviceFolder && root.syncState === "error" ? "danger" : "success"
                            }
                        ]
                        Bullet { text: qsTr("For computers and Android phones (Syncthing-Fork).") }
                        Bullet { text: qsTr("Works in your home Wi-Fi even without internet.") }
                        Bullet { text: AppController.isWasm ? qsTr("Not available in the browser.") : qsTr("Nothing to sign up for; your notes never leave your devices.") }
                        BText {
                            Layout.fillWidth: true
                            visible: root.deviceFolder && root.syncStatusText !== ""
                            text: qsTr("Folder “%1” on this phone: %2").arg(root.sync !== null ? root.sync.deviceFolderName : "").arg(root.syncStatusText)
                            variant: "small"
                            color: root.syncState === "error" ? Theme.dangerText : Theme.fgMuted
                            wrapMode: Text.Wrap
                        }
                        Item { Layout.fillHeight: true }
                        BButton {
                            enabled: !AppController.isWasm
                            text: root.deviceFolder || Workspace.syncthingFolder ? qsTr("Show the status") : qsTr("Set up Syncthing")
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
                                visible: root.sync !== null && root.nextcloudState !== "disabled"
                                text: {
                                    switch (root.nextcloudState) {
                                    case "syncing": return qsTr("Syncing")
                                    case "offline": return qsTr("Offline")
                                    case "error": return qsTr("Problem")
                                    default: return qsTr("On")
                                    }
                                }
                                variant: {
                                    switch (root.nextcloudState) {
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
                            visible: !root.deviceFolder && root.syncStatusText !== ""
                            text: root.syncStatusText
                            variant: "small"
                            color: root.syncState === "error" ? Theme.dangerText : Theme.fgMuted
                            wrapMode: Text.Wrap
                        }
                        Item { Layout.fillHeight: true }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space2
                            BButton {
                                // Another notebook syncs with this one's server folder: nothing
                                // syncs until one of them gets another folder (the form below
                                // the status on the Nextcloud tab).
                                readonly property bool folderShared: root.sync !== null && root.sync.folderSharedWith !== ""
                                text: folderShared ? qsTr("Choose another folder")
                                    : root.sync !== null && root.sync.configured ? qsTr("Settings") : qsTr("Set up Nextcloud")
                                variant: folderShared ? "primary" : "secondary"
                                iconName: "arrow-right"
                                iconRight: true
                                onClicked: root.showTab("nextcloud")
                            }
                            BButton {
                                visible: root.sync !== null && root.sync.active && !root.deviceFolder
                                text: qsTr("Sync now")
                                iconName: "refresh-cw"
                                variant: "primary"
                                busy: root.sync !== null && root.sync.state === "syncing"
                                onClicked: root.sync.syncNow()
                            }
                        }
                    }
                }

                // A notebook kept in a GitHub repository (not a way between devices: a source).
                IoCard {
                    Layout.fillWidth: true
                    iconName: "git-branch"
                    title: "GitHub"
                    subtitle: root.githubLinked
                              ? root.githubLinkText
                              : qsTr("Notes kept in a GitHub repository, as a notebook — also in the browser.")
                    trailing: [
                        BBadge {
                            visible: root.githubLinked
                            text: root.githubChanges > 0 ? qsTr("%n change(s)", "", root.githubChanges)
                                  : root.githubState === "error" ? qsTr("Problem") : qsTr("Linked")
                            variant: root.githubChanges > 0 ? "info" : root.githubState === "error" ? "danger" : "success"
                        }
                    ]
                    BText {
                        Layout.fillWidth: true
                        visible: root.githubLinked
                        text: root.githubStatusText
                        variant: "small"
                        color: root.githubState === "error" ? Theme.dangerText : Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                    Bullet {
                        visible: !root.githubLinked
                        text: qsTr("Brulion gets the newest version and sends your changes back as commits or pull requests.")
                    }
                    BButton {
                        text: root.githubLinked ? qsTr("Show") : qsTr("Set up GitHub")
                        iconName: "arrow-right"
                        iconRight: true
                        onClicked: root.showTab("github")
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
                // Android: the folder of the phone a notebook in the app's storage syncs with.
                DeviceFolderPanel {}
                SyncDeletionGuard { visible: root.deviceFolder && root.sync !== null && root.sync.needsConfirmation }
                SyncStatusPanel {
                    visible: root.deviceFolder
                    onShowConflicts: root.showTab("conflicts")
                }
                SyncHistoryList {
                    id: folderHistoryList
                    visible: root.deviceFolder && folderHistoryList.runs.length > 0
                }
                SyncthingPanel {
                    Layout.fillWidth: true
                }
            }

            // ---- Nextcloud --------------------------------------------------------------------
            Page {
                // A notebook syncing with a folder of the phone: Nextcloud waits (one way per notebook).
                IoCard {
                    visible: root.deviceFolder
                    iconName: "folder-sync"
                    title: qsTr("This notebook syncs with a folder of the phone")
                    subtitle: qsTr("It syncs with the folder “%1”, so Nextcloud is off for it. To sync it with Nextcloud instead, stop syncing with the folder on the Syncthing tab.").arg(root.sync !== null ? root.sync.deviceFolderName : "")
                    BButton {
                        text: qsTr("Show the folder")
                        iconName: "arrow-right"
                        iconRight: true
                        onClicked: root.showTab("syncthing")
                    }
                }
                SyncDeletionGuard { visible: !root.deviceFolder && root.sync !== null && root.sync.needsConfirmation }
                SyncStatusPanel {
                    visible: root.sync !== null && !root.deviceFolder && (root.sync.configured || root.sync.state !== "disabled")
                    onShowConflicts: root.showTab("conflicts")
                }
                SyncHistoryList {
                    id: historyList
                    visible: !root.deviceFolder && historyList.runs.length > 0
                }
                // What the app and the sync did, step by step — to check or to send to us.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon { Layout.alignment: Qt.AlignVCenter; name: "scroll-text"; size: Theme.iconSize; color: Theme.fgMuted }
                    BText {
                        Layout.fillWidth: true
                        text: qsTr("Sync takes long or something fails? The event log shows every step.")
                        variant: "small"
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                    BButton {
                        objectName: "syncOpenLog"
                        text: qsTr("Event log")
                        size: "small"
                        onClicked: AppController.navigate({ view: "logs", group: "sync" })
                    }
                }
                NextcloudForm { visible: !root.deviceFolder }
                SyncEncryptionCard { visible: !root.deviceFolder && AppSettings.nextcloudEnabled }
            }

            // ---- GitHub -----------------------------------------------------------------------
            Page {
                GitHubPanel {
                    onShowConflicts: root.showTab("conflicts")
                }
            }

            // ---- Calendars ----------------------------------------------------------------------
            Page {
                CalendarsPanel {}
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
                                onActivated: (copy) => root.openCopy(copy)
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
                        visible: root.currentCopy !== ""
                        copy: root.currentCopy
                        onClosed: root.closeCopy()
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
