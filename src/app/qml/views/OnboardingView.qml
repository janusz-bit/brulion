pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// OnboardingView — the start screen: the first run, a return without an open notebook, and
// "Open another notebook…" (docs/UI.md "The start screen").
//
//   * Recent notebooks (AppController.recentNotebooks, recentnotebooks.h), first when there are
//     any: the name, the folder ("~/…") and when it was last opened; a folder that is gone or does
//     not answer is marked ("Folder not found" / "Not available") with "Remove from list" — every
//     row has it (its × shows on hover and keyboard focus with a mouse; Delete on a focused row).
//     The list is one tab stop (Up / Down within it). The folders are checked again when the
//     screen shows and when the window comes back to the front.
//   * Open or create a notebook — the one primary action: the folder field with Browse… (desktop:
//     FilePicker, the system's own dialog), Android's "Use app storage" and its wait for "All files
//     access", the browser's one notebook in its storage (IndexedDB).
//   * Other ways to start, one compact list: Import notes (opens the chosen notebook on its import
//     screen), Open from GitHub… (qml/io/GitHubOpenDialog: the sync center's GitHub tab), Try the
//     sample notebook (in the UI language: "Sample notebook", in Polish "Przykładowy brulion").
//   * What Brulion is and how notes get to other devices — for a first run only (no recent
//     notebook).
//
// Wide windows (≥ 880 px): two columns that fit the window — a returning user's recent notebooks
// on the left (their list scrolls inside its card), the start actions on the right; a first run
// has the start actions on the left and what Brulion is on the right. Narrow windows and phones:
// one column that scrolls, in the same order (recent notebooks first; past five, "Show all").
Rectangle {
    id: root

    property var route: ({})

    readonly property real safeTop: SafeArea.margins.top
    readonly property real safeBottom: SafeArea.margins.bottom
    readonly property RecentNotebooks recents: AppController.recentNotebooks
    /// Someone coming back: recent notebooks to show.
    readonly property bool returning: recents.count > 0
    /// Two columns.
    readonly property bool wide: flick.width >= 880
    /// The rows a narrow window shows before "Show all".
    readonly property int compactRows: 5
    property bool showAllRecents: false
    /// The recent row the Tab key reaches (one tab stop for the list; Up / Down move it).
    property int recentCursor: 0
    /// After Delete: the row to give the focus to once the removed one is gone.
    property int focusAfterRemove: -1
    readonly property bool pending: AppController.pendingVaultPath !== ""
    /// The app for Google Play: a folder of the phone only through the system's dialog (synced
    /// with a notebook in the app's storage), no path to type.
    readonly property bool pickedFolders: AppController.deviceFoldersAvailable && AppController.playStore
    /// What Brulion is ({ icon, title, text }).
    readonly property list<var> features: [
        { icon: "file-text", title: qsTr("Plain files"),
          text: qsTr("Every page is a .md file in a folder you choose — readable by Obsidian, Logseq or any editor.") },
        { icon: "bell", title: qsTr("Journal and reminders"),
          text: qsTr("A page for every day, tasks with dates, reminders and alarms that ring on time.") }
    ]
    /// The two ways to sync ({ icon, title, text }).
    readonly property list<var> syncWays: [
        { icon: "folder-sync", title: "Syncthing",
          text: qsTr("Share the notebook folder between computers and phones (on Android with Syncthing-Fork) and pick that folder in Brulion on every device. Edits arrive within seconds; conflicting copies can be merged in the sync center.") },
        { icon: "cloud", title: "Nextcloud",
          text: qsTr("Keep the notebook anywhere — also in app storage or in the browser — and connect your Nextcloud in Settings → Sync with an app password. Edits from several devices are merged, nothing is lost.") }
    ]

    color: Theme.bg

    function chosenPath(): string {
        return AppController.isWasm ? AppController.defaultVaultPath : Js.trim(pathField.text)
    }
    function openChosen(): void {
        AppController.openVault(root.chosenPath())
    }
    /// Open the chosen notebook on its import screen (notes from another app).
    function openChosenForImport(): void {
        AppController.openVaultAt(root.chosenPath(), { view: "importExport", tab: "import" })
    }
    /// Keyboard users start on the most likely choice: the last notebook, else "Open notebook".
    function focusFirst(): void {
        if (Theme.touchUi)
            return
        if (recentRows.count > 0)
            recentRows.itemAt(0).forceActiveFocus()
        else
            openButton.forceActiveFocus()
    }
    /// Keep a recent row that got the keyboard focus inside its scrolled list.
    function revealRecent(row: Item): void {
        if (!recentFlick.interactive)
            return
        const top = row.y
        const bottom = row.y + row.height
        if (top < recentFlick.contentY)
            recentFlick.contentY = top
        else if (bottom > recentFlick.contentY + recentFlick.height)
            recentFlick.contentY = bottom - recentFlick.height
    }
    /// Move the keyboard focus to the recent row `index` (Up / Down).
    function focusRecent(index: int): void {
        if (index < 0 || index >= recentRows.count)
            return
        const row = recentRows.itemAt(index)
        if (row && row.visible)
            row.forceActiveFocus(Qt.TabFocusReason)
    }
    /// "Remove from list" of row `index`; a row removed with the keyboard hands the focus on to
    /// the next one (the previous one at the end).
    function removeRecent(index: int, path: string, keyboard: bool): void {
        root.focusAfterRemove = keyboard ? index : -1
        root.recents.remove(path)
        if (keyboard)
            refocusLater.restart()
    }
    Later {
        id: refocusLater
        onTriggered: {
            const next = Math.min(root.focusAfterRemove, recentRows.count - 1)
            root.focusAfterRemove = -1
            if (next >= 0)
                root.focusRecent(next)
            else
                openButton.forceActiveFocus(Qt.TabFocusReason)
        }
    }

    // The folders again whenever the screen shows and when the window comes back (a drive
    // plugged in, a share mounted meanwhile).
    readonly property bool windowActive: Window.active
    onWindowActiveChanged: if (windowActive) root.recents.refresh()
    Component.onCompleted: {
        root.recents.refresh()
        focusLater.restart()
    }
    Later {
        id: focusLater
        onTriggered: root.focusFirst()
    }

    // ---- Pieces --------------------------------------------------------------------------------

    /// An icon in a rounded square (feature highlights, the rows of "Other ways to start").
    component IconTile: Rectangle {
        id: tile
        required property string iconName
        property color tint: Theme.fgMuted
        Layout.alignment: Qt.AlignTop
        implicitWidth: Theme.touchUi ? 40 : 36
        implicitHeight: implicitWidth
        radius: Theme.radiusLg
        color: Theme.bgSunken
        BIcon {
            anchors.centerIn: parent
            name: tile.iconName
            size: Theme.iconSize
            color: tile.tint
        }
    }

    /// A heading inside a card.
    component CardHeading: BText {
        Layout.fillWidth: true
        variant: "title"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    /// One way to start: an icon, the title and a line about it; the whole row is the button.
    component ActionRow: T.ItemDelegate {
        id: action
        property string iconName
        property color iconTint: Theme.fgMuted
        property string description
        Layout.fillWidth: true
        implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(Theme.touchUi ? 64 : 56, implicitContentHeight + topPadding + bottomPadding)
        leftPadding: Theme.space4
        rightPadding: Theme.space3
        topPadding: Theme.space2
        bottomPadding: Theme.space2
        focusPolicy: Qt.StrongFocus
        hoverEnabled: !Theme.touchUi
        font.family: Theme.fontUi
        font.pixelSize: Theme.textBase
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: description
        Keys.onReturnPressed: action.click()
        Keys.onEnterPressed: action.click()

        contentItem: RowLayout {
            spacing: Theme.space3
            IconTile {
                Layout.alignment: Qt.AlignVCenter
                iconName: action.iconName
                tint: action.iconTint
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 1
                BText {
                    Layout.fillWidth: true
                    text: action.text
                    variant: "label"
                    elide: Text.ElideRight
                    color: Theme.fg
                }
                BText {
                    Layout.fillWidth: true
                    text: action.description
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    lineHeight: Theme.lineHeightPx(Theme.textSm, Theme.lineHeight)
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
            BIcon {
                Layout.alignment: Qt.AlignVCenter
                name: "chevron-right"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
        }
        background: Rectangle {
            color: action.down ? Theme.bgPressed : action.hovered || action.visualFocus ? Theme.bgHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationInstant } }
            BFocusRing {
                visible: action.visualFocus
                targetRadius: Theme.radius
                inset: true
            }
        }
        opacity: enabled ? 1 : Theme.disabledOpacity
    }

    /// A hairline between the rows of a list in a card.
    component RowDivider: Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space4
        Layout.rightMargin: Theme.space4
        implicitHeight: 1
        color: Theme.borderSubtle
    }

    /// One recent notebook: its folder icon, name and path, when it was last opened, and "Remove
    /// from list". A folder that is gone is marked: a crossed folder, the reason after the name.
    component RecentRow: T.ItemDelegate {
        id: recent
        required property int index
        required property string path
        required property string name
        required property string displayPath
        required property string lastOpenedText
        required property string availability
        readonly property bool unavailable: availability === "missing" || availability === "unreachable"
        readonly property string reason: availability === "missing" ? qsTr("Folder not found")
                                       : availability === "unreachable" ? qsTr("Not available") : ""
        /// The row's own × (available folders): with a mouse while it is hovered or focused.
        readonly property bool showRemove: Theme.touchUi || recent.hovered || recent.visualFocus
                                           || removeIcon.hovered

        objectName: "onboardingRecent"
        text: name
        Layout.fillWidth: true
        // (Narrow windows show the first rows until "Show all".)
        visible: root.wide || root.showAllRecents || index < root.compactRows
        implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(Theme.touchUi ? 64 : 56, implicitContentHeight + topPadding + bottomPadding)
        leftPadding: Theme.space3
        rightPadding: Theme.space2
        topPadding: Theme.space2
        bottomPadding: Theme.space2
        // One tab stop for the whole list: the row last focused (Up / Down move within it).
        focusPolicy: index === Math.min(root.recentCursor, recentRows.count - 1) ? Qt.StrongFocus : Qt.ClickFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.ListItem
        Accessible.name: name
        Accessible.description: (unavailable ? reason + ". " : "") + displayPath
                                + (lastOpenedText !== "" ? ". " + qsTr("Last opened: %1").arg(lastOpenedText) : "")

        onClicked: AppController.openRecentVault(recent.path)
        onActiveFocusChanged: {
            if (!activeFocus)
                return
            root.recentCursor = recent.index
            root.revealRecent(recent)
        }
        Keys.onReturnPressed: recent.click()
        Keys.onEnterPressed: recent.click()
        Keys.onDeletePressed: root.removeRecent(recent.index, recent.path, true)
        Keys.onUpPressed: root.focusRecent(recent.index - 1)
        Keys.onDownPressed: root.focusRecent(recent.index + 1)

        contentItem: RowLayout {
            spacing: Theme.space3

            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Theme.touchUi ? 40 : 36
                implicitHeight: implicitWidth
                radius: Theme.radiusLg
                color: recent.unavailable ? Theme.warningBg : Theme.bgSunken
                BIcon {
                    anchors.centerIn: parent
                    name: recent.unavailable ? "folder-x" : "folder"
                    size: Theme.iconSize
                    color: recent.unavailable ? Theme.warningText : Theme.fgMuted
                }
            }
            // The name, on its right when it was last opened (or why it cannot open); the folder
            // under it.
            Item {
                id: texts
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: Math.max(nameText.implicitWidth + Theme.space3 + sideText.implicitWidth, pathText.implicitWidth)
                implicitHeight: nameText.implicitHeight + 1 + pathText.implicitHeight

                BText {
                    id: nameText
                    width: Math.min(implicitWidth, texts.width - (sideText.text !== "" ? sideText.implicitWidth + Theme.space3 : 0))
                    text: recent.name
                    variant: "label"
                    elide: Text.ElideRight
                    color: recent.unavailable ? Theme.fgMuted : Theme.fg
                }
                BText {
                    id: sideText
                    objectName: recent.unavailable ? "onboardingRecentReason" : "onboardingRecentWhen"
                    anchors.right: parent.right
                    anchors.baseline: nameText.baseline
                    text: recent.unavailable ? recent.reason : recent.lastOpenedText
                    font.pixelSize: Theme.textSm
                    font.weight: recent.unavailable ? Theme.weightMedium : Theme.weightNormal
                    color: recent.unavailable ? Theme.warningText : Theme.fgMuted
                }
                BText {
                    id: pathText
                    y: nameText.height + 1
                    width: texts.width
                    text: recent.displayPath
                    variant: "muted"
                    font.pixelSize: Theme.textSm
                    elide: Text.ElideMiddle
                }
            }
            BButton {
                objectName: "onboardingRecentRemove"
                Layout.alignment: Qt.AlignVCenter
                // (Phones: the row's × says it, its name is "Remove from list".)
                visible: recent.unavailable && !Theme.compact
                text: qsTr("Remove from list")
                size: "small"
                variant: "ghost"
                // (The keyboard has Delete on the row: the list is one tab stop.)
                focusPolicy: Qt.NoFocus
                Accessible.description: recent.name
                onClicked: root.removeRecent(recent.index, recent.path, false)
            }
            BIconButton {
                id: removeIcon
                objectName: "onboardingRecentForget"
                Layout.alignment: Qt.AlignVCenter
                visible: !recent.unavailable || Theme.compact
                opacity: recent.showRemove || recent.unavailable ? 1 : 0
                iconName: "x"
                size: "small"
                label: qsTr("Remove from list")
                focusPolicy: Qt.NoFocus
                Accessible.description: recent.name
                onClicked: root.removeRecent(recent.index, recent.path, false)
            }
        }
        background: Rectangle {
            radius: Theme.radius
            color: recent.down ? Theme.bgPressed : recent.hovered || recent.visualFocus ? Theme.bgHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationInstant } }
            BFocusRing {
                visible: recent.visualFocus
                targetRadius: Theme.radius
                inset: true
            }
        }
        BToolTip {
            visible: recent.hovered && pathText.truncated && !removeIcon.hovered
            text: recent.displayPath
            delay: 700
        }
    }

    // ---- The page ------------------------------------------------------------------------------

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: root.safeTop
        // Ends at the on-screen keyboard (it covers the navigation bar too): the window's
        // BKeyboardFocusKeeper scrolls the focused folder field above it.
        anchors.bottomMargin: Math.max(root.safeBottom, KeyboardInsets.overlap)
        contentWidth: width
        contentHeight: Math.max(height, page.height + 2 * vMargin)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: BScrollBar {}

        readonly property int hMargin: Theme.compact ? Theme.space4 : root.wide ? Theme.space12 : Theme.space8
        readonly property int vMargin: Theme.compact ? Theme.space5 : root.wide ? Theme.space10 : Theme.space8

        ColumnLayout {
            id: page
            objectName: "onboardingPage"
            readonly property real available: flick.height - 2 * flick.vMargin
            /// Wide: the header and the tallest column that cannot shrink (a long list of recent
            /// notebooks scrolls inside its card down to three rows).
            readonly property real minimumWide: header.implicitHeight + spacing
                                                + Math.max(actions.implicitHeight,
                                                           root.returning ? recentsCard.minimumHeight : about.implicitHeight)
            x: Math.round((flick.width - width) / 2)
            // Wide: centred in the window; else from the top.
            y: root.wide ? Math.max(flick.vMargin, Math.round((flick.height - height) / 2)) : flick.vMargin
            width: Math.min(flick.width - 2 * flick.hMargin, root.wide ? 1120 : 640)
            height: root.wide ? Math.max(minimumWide, Math.min(available, implicitHeight)) : implicitHeight
            spacing: root.wide ? Theme.space8 : Theme.space6

            // ---- Welcome ---------------------------------------------------------------------
            RowLayout {
                id: header
                Layout.fillWidth: true
                spacing: Theme.compact ? Theme.space3 : Theme.space4

                Image {
                    Layout.alignment: root.returning ? Qt.AlignVCenter : Qt.AlignTop
                    source: "qrc:/images/logo.svg"
                    readonly property int side: Theme.compact ? 48 : 64
                    sourceSize: Qt.size(side, side)
                    Layout.preferredWidth: side
                    Layout.preferredHeight: side
                    Accessible.ignored: true
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: Theme.space1
                    BText {
                        objectName: "onboardingTitle"
                        Layout.fillWidth: true
                        text: qsTr("Welcome to Brulion")
                        variant: "display"
                        font.pixelSize: Theme.compact ? Theme.text2xl : Theme.text3xl
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    BText {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 640
                        visible: !root.returning
                        text: qsTr("Your notes as plain Markdown files: an outliner with a daily journal, tasks, reminders and calculations. Everything works offline and stays yours.")
                        variant: "muted"
                        font.pixelSize: Theme.textMd
                        lineHeight: Theme.lineHeightPx(Theme.textMd, Theme.lineHeight)
                        wrapMode: Text.Wrap
                    }
                }
            }

            GridLayout {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: root.wide
                columns: root.wide ? 2 : 1
                columnSpacing: Theme.space8
                rowSpacing: Theme.compact ? Theme.space6 : Theme.space8
                /// The first column's width on wide windows (the second takes the rest).
                readonly property real leftWidth: Math.round((width - columnSpacing) * (root.returning ? 0.56 : 0.5))

                // ---- Recent notebooks ----------------------------------------------------
                BCard {
                    id: recentsCard
                    objectName: "onboardingRecents"
                    visible: root.returning
                    Layout.row: 0
                    Layout.column: 0
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: !root.wide
                    Layout.preferredWidth: root.wide ? grid.leftWidth : -1
                    Layout.fillHeight: root.wide
                    Layout.maximumHeight: root.wide ? implicitHeight : -1
                    elevation: 1
                    padding: Theme.space2
                    topPadding: Theme.compact ? Theme.space3 : Theme.space4
                    accessibleName: qsTr("Recent notebooks")
                    /// The card's height with three rows: the least a wide window shrinks it to.
                    readonly property real minimumHeight: topPadding + bottomPadding + recentsHeading.implicitHeight
                                                          + recentsColumn.spacing + 3 * (Theme.touchUi ? 64 : 56)

                    ColumnLayout {
                        id: recentsColumn
                        anchors.fill: parent
                        spacing: Theme.space2

                        RowLayout {
                            id: recentsHeading
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.space3
                            Layout.rightMargin: Theme.space3
                            spacing: Theme.space2
                            CardHeading {
                                Layout.fillWidth: false
                                text: qsTr("Recent notebooks")
                            }
                            BText {
                                text: "" + root.recents.count
                                variant: "faint"
                                font.pixelSize: Theme.textBase
                                Accessible.ignored: true
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: root.wide
                            Layout.preferredHeight: recentList.implicitHeight
                            implicitHeight: recentList.implicitHeight

                            Flickable {
                                id: recentFlick
                                objectName: "onboardingRecentList"
                                anchors.fill: parent
                                contentWidth: width
                                contentHeight: recentList.implicitHeight
                                // Wide: scrolls inside the card when the window is too low for the list.
                                interactive: root.wide && contentHeight > height + 1
                                clip: interactive
                                boundsBehavior: Flickable.StopAtBounds
                                // Shown all the time: the list goes on below the card's edge.
                                T.ScrollBar.vertical: BScrollBar {
                                    policy: recentFlick.interactive ? T.ScrollBar.AlwaysOn : T.ScrollBar.AsNeeded
                                }
                                Accessible.role: Accessible.List
                                Accessible.name: qsTr("Recent notebooks")

                                ColumnLayout {
                                    id: recentList
                                    width: recentFlick.width
                                    spacing: 0
                                    Repeater {
                                        id: recentRows
                                        model: root.recents
                                        delegate: RecentRow {}
                                    }
                                }
                            }
                            // More rows below: the last one fades into the card.
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: Theme.space8
                                visible: recentFlick.interactive && !recentFlick.atYEnd
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: Theme.alpha(Theme.bgElevated, 0) }
                                    GradientStop { position: 1.0; color: Theme.bgElevated }
                                }
                            }
                        }

                        BButton {
                            objectName: "onboardingRecentShowAll"
                            Layout.alignment: Qt.AlignLeft
                            Layout.leftMargin: Theme.space1
                            visible: !root.wide && !root.showAllRecents && root.recents.count > root.compactRows
                            text: qsTr("Show all (%1)").arg(root.recents.count)
                            iconName: "chevron-down"
                            variant: "ghost"
                            size: "small"
                            onClicked: root.showAllRecents = true
                        }
                    }
                }

                // ---- Open or create a notebook; other ways to start ------------------------
                ColumnLayout {
                    id: actions
                    Layout.row: root.wide || !root.returning ? 0 : 1
                    Layout.column: root.wide && root.returning ? 1 : 0
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.wide ? (root.returning ? grid.width - grid.leftWidth - grid.columnSpacing
                                                                       : grid.leftWidth) : -1
                    spacing: Theme.space6

                    BCard {
                        id: startCard
                        objectName: "onboardingStart"
                        Layout.fillWidth: true
                        elevation: 1
                        padding: Theme.compact ? Theme.space4 : Theme.space5
                        accessibleName: heading.text

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Theme.space3

                            CardHeading {
                                id: heading
                                text: AppController.isWasm ? qsTr("Your notebook in this browser") : qsTr("Open or create a notebook")
                            }
                            BText {
                                Layout.fillWidth: true
                                text: AppController.isWasm
                                      ? qsTr("Notes are kept in this browser's storage on this device and work offline. Move them with import and export, or sync them with Nextcloud.")
                                      : root.pickedFolders
                                        ? qsTr("Keep the notes in the app's own storage, or choose a folder on the phone — for example the one Syncthing-Fork syncs. Brulion keeps its copy of the notebook in sync with that folder.")
                                      : AppController.isAndroid
                                        ? qsTr("To sync with Syncthing, use a folder on the phone's storage — Brulion then asks for “All files access”. Without it, keep the notes in the app's own storage and sync them with Nextcloud.")
                                        : qsTr("Choose a folder: a new one, one you sync with Syncthing, or an Obsidian vault — it opens as it is, no import needed. A missing folder is created.")
                                variant: "muted"
                                lineHeight: Theme.lineHeightPx(Theme.textBase, Theme.lineHeight)
                                wrapMode: Text.Wrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.space1
                                visible: !AppController.isWasm && !root.pickedFolders
                                spacing: Theme.space2

                                BTextField {
                                    id: pathField
                                    objectName: "onboardingVaultPath"
                                    Layout.fillWidth: true
                                    text: AppSettings.currentVault !== "" ? AppSettings.currentVault : AppController.defaultVaultPath
                                    leadingIcon: "folder"
                                    accessibleName: qsTr("Notebook folder")
                                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                                    onAccepted: root.openChosen()
                                }
                                BButton {
                                    objectName: "onboardingBrowse"
                                    visible: AppController.isDesktop
                                    text: Theme.compact ? "" : qsTr("Browse…")
                                    iconName: "folder-open"
                                    Accessible.name: qsTr("Browse…")
                                    onClicked: folderDialog.open()
                                }
                            }

                            // Android: waiting for "All files access".
                            BCard {
                                Layout.fillWidth: true
                                visible: root.pending
                                tone: "warning"
                                padding: Theme.space3

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: Theme.space2
                                    BText {
                                        Layout.fillWidth: true
                                        text: qsTr("Brulion needs “All files access” to keep notes in %1. Allow it in the system settings and come back — the notebook opens by itself.")
                                              .arg(AppController.pendingVaultPath)
                                        wrapMode: Text.Wrap
                                    }
                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: Theme.space2
                                        BButton {
                                            text: qsTr("Open system settings")
                                            iconName: "settings"
                                            variant: "primary"
                                            onClicked: AppController.requestPermission("allFilesAccess")
                                        }
                                        BButton {
                                            text: qsTr("Use app storage instead")
                                            variant: "ghost"
                                            onClicked: AppController.openVault(AppController.appStorageVaultPath)
                                        }
                                    }
                                }
                            }

                            // The app for Google Play: a folder of the phone, picked in the system's dialog.
                            BButton {
                                objectName: "onboardingChooseFolder"
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.space1
                                visible: root.pickedFolders && !root.pending
                                fill: true
                                size: "large"
                                variant: "primary"
                                text: qsTr("Choose a folder…")
                                iconName: "folder-open"
                                onClicked: folderDialog.open()
                            }
                            BButton {
                                id: openButton
                                objectName: "onboardingOpen"
                                Layout.fillWidth: true
                                Layout.topMargin: Theme.space1
                                visible: !root.pending && !root.pickedFolders
                                fill: true
                                size: "large"
                                variant: "primary"
                                text: AppController.isWasm ? qsTr("Start with an empty notebook") : qsTr("Open notebook")
                                iconName: AppController.isWasm ? "square-pen" : "notebook-pen"
                                enabled: root.chosenPath() !== ""
                                onClicked: root.openChosen()
                            }
                            BButton {
                                objectName: "onboardingAppStorage"
                                Layout.fillWidth: true
                                visible: (AppController.isAndroid || root.pickedFolders) && !root.pending
                                fill: true
                                text: qsTr("Use app storage")
                                iconName: "smartphone"
                                onClicked: AppController.openVault(AppController.appStorageVaultPath)
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2

                        BText {
                            Layout.fillWidth: true
                            Layout.leftMargin: Theme.compact ? 0 : Theme.space1
                            text: qsTr("Other ways to start")
                            variant: "label"
                            color: Theme.fgMuted
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        BCard {
                            id: moreCard
                            objectName: "onboardingMore"
                            Layout.fillWidth: true
                            padding: 0
                            accessibleName: qsTr("Other ways to start")

                            ColumnLayout {
                                anchors.fill: parent
                                spacing: 0

                                ActionRow {
                                    objectName: "onboardingImport"
                                    iconName: "download"
                                    text: qsTr("Import notes")
                                    // An Obsidian vault needs no import (the start card says so):
                                    // its folder opens as the notebook (docs/IMPORT.md).
                                    description: AppController.isWasm
                                                 ? qsTr("Obsidian, Logseq, Notion, Evernote, Google Keep, Joplin and more, into the notebook above")
                                                 : qsTr("Logseq, Notion, Evernote, Google Keep, Joplin and more, into the folder above")
                                    enabled: root.chosenPath() !== ""
                                    onClicked: root.openChosenForImport()
                                }
                                RowDivider {}
                                ActionRow {
                                    objectName: "onboardingGitHub"
                                    iconName: "git-branch"
                                    text: qsTr("Open from GitHub…")
                                    description: qsTr("Markdown notes from a repository; your changes go back as commits")
                                    onClicked: githubDialog.openWith("", "")
                                }
                                RowDivider {}
                                ActionRow {
                                    objectName: "onboardingSample"
                                    iconName: "sparkles"
                                    iconTint: Theme.warningText
                                    text: qsTr("Try the sample notebook")
                                    description: qsTr("A guided tour with live examples of every feature")
                                    onClicked: AppController.openDemoVault()
                                }
                            }
                        }
                    }
                }

                // ---- What Brulion is (a first run) ------------------------------------------
                BCard {
                    id: about
                    objectName: "onboardingAbout"
                    visible: !root.returning
                    Layout.row: root.wide ? 0 : 1
                    Layout.column: root.wide ? 1 : 0
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.preferredWidth: root.wide ? grid.width - grid.leftWidth - grid.columnSpacing : -1
                    tone: "sunken"
                    flat: Theme.compact
                    padding: Theme.space5

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: Theme.space4

                        Repeater {
                            model: root.features
                            delegate: RowLayout {
                                id: feature
                                required property string icon
                                required property string title
                                required property string text
                                Layout.fillWidth: true
                                spacing: Theme.space3
                                IconTile {
                                    iconName: feature.icon
                                    color: Theme.compact ? Theme.bgSunken : Theme.bg
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    BText {
                                        Layout.fillWidth: true
                                        text: feature.title
                                        variant: "label"
                                        wrapMode: Text.Wrap
                                    }
                                    BText {
                                        Layout.fillWidth: true
                                        text: feature.text
                                        variant: "muted"
                                        lineHeight: Theme.lineHeightPx(Theme.textBase, Theme.lineHeight)
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.space1
                            Layout.bottomMargin: Theme.space1
                            implicitHeight: 1
                            color: Theme.border
                        }

                        BText {
                            Layout.fillWidth: true
                            text: qsTr("How do notes get to my other devices?")
                            variant: "label"
                            wrapMode: Text.Wrap
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        Repeater {
                            model: root.syncWays
                            delegate: RowLayout {
                                id: syncItem
                                required property string icon
                                required property string title
                                required property string text
                                Layout.fillWidth: true
                                spacing: Theme.space3
                                IconTile {
                                    iconName: syncItem.icon
                                    color: Theme.compact ? Theme.bgSunken : Theme.bg
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    BText {
                                        Layout.fillWidth: true
                                        text: syncItem.title
                                        variant: "label"
                                    }
                                    BText {
                                        Layout.fillWidth: true
                                        text: syncItem.text
                                        variant: "muted"
                                        lineHeight: Theme.lineHeightPx(Theme.textBase, Theme.lineHeight)
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    GitHubOpenDialog {
        id: githubDialog
    }

    FilePicker {
        id: folderDialog
        objectName: "onboardingFolderDialog"
        title: qsTr("Choose the notebook folder")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: {
            if (root.pickedFolders)
                AppController.openDeviceFolder(selectedFolder)
            else
                pathField.text = AppController.localPath(selectedFolder)
        }
    }
}
