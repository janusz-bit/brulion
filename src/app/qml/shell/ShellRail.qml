pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellRail — the sidebar's narrow column of views (docs/UI.md "Sidebar"). At the top the
// notebook: its first letter, a click opens the notebook switcher (the recent notebooks, the
// documentation, another folder). Then the views as icon buttons in groups split by a line (the
// day: Journal, Calendar, Tasks; practice: Habits, Gym, Flashcards with the count of cards to study
// now; the notes: All pages, Tags, Graph; Search), without the ones hidden in Settings →
// Appearance → Layout (on phones also Import notes), then the extensions' entries
// (b.app.contribute("sidebar", …), docs/UI.md "Extension items in the UI"). They scroll when the
// window is too low for all. At the bottom the sync status (ShellSyncFooter, compact; a click
// syncs now, a right click or long press opens the sync center) and Settings. The selected view:
// tinted in the accent, a pill at the rail's edge (a short one under the pointer).
//
// On wide windows the rail stays at the window's left edge when the page panel (ShellSidebar) is
// hidden; with `expandable` (AppSettings.railExpands, not on touch screens) it widens over the
// page panel with each view's name while the pointer rests on it, or while the keyboard's focus is
// in it — the icons stay where they are; otherwise the names are tooltips. On phones it is the
// drawer's left column, each name under its icon (`navigated()` closes the drawer).
Item {
    id: rail

    property real topInset: 0
    property real bottomInset: 0
    /// Widens with the names under the pointer (AppShell: the wide windows' rail).
    property bool expandable: false

    signal navigated()

    readonly property string routeView: AppController.currentView
    /// The views offered (the ones hidden in the settings are not shown: rail.shows()).
    readonly property list<var> entries: [
        { entryView: "code", entryAlso: "", entryTitle: qsTr("Code"), entryShort: qsTr("Code"), entryIcon: "code" },
        { entryView: "journals", entryAlso: "journal", entryTitle: qsTr("Journals"), entryShort: qsTr("Journal"), entryIcon: "book-open" },
        { entryView: "calendar", entryAlso: "", entryTitle: qsTr("Calendar"), entryShort: qsTr("Calendar"), entryIcon: "calendar" },
        { entryView: "tasks", entryAlso: "", entryTitle: qsTr("Tasks"), entryShort: qsTr("Tasks"), entryIcon: "list-todo" },
        { entryView: "habits", entryAlso: "", entryTitle: qsTr("Habits"), entryShort: qsTr("Habits"), entryIcon: "flame" },
        { entryView: "gym", entryAlso: "", entryTitle: qsTr("Gym"), entryShort: qsTr("Gym"), entryIcon: "dumbbell" },
        { entryView: "flashcards", entryAlso: "", entryTitle: qsTr("Flashcards"), entryShort: qsTr("Flashcards"), entryIcon: "layers" },
        { entryView: "allPages", entryAlso: "", entryTitle: qsTr("All pages"), entryShort: qsTr("Pages"), entryIcon: "files" },
        { entryView: "tags", entryAlso: "tag", entryTitle: qsTr("Tags"), entryShort: qsTr("Tags"), entryIcon: "tags" },
        { entryView: "graph", entryAlso: "", entryTitle: qsTr("Graph"), entryShort: qsTr("Graph"), entryIcon: "waypoints" },
        { entryView: "search", entryAlso: "", entryTitle: qsTr("Search"), entryShort: qsTr("Search"), entryIcon: "search" }
    ]
    /// The entries' views in their order (typed, for startsGroup()): the settings' (AppSettings.railViews:
    /// the layout's #rail, docs/NOTEBOOK-SETTINGS.md), "code" first.
    readonly property list<string> entryViews: AppSettings.railViews
    /// One field of the entry of `view` ("" none).
    function entryField(view: string, field: string): string {
        for (let j = 0; j < rail.entries.length; ++j)
            if (AppController.routeText(rail.entries[j], "entryView") === view)
                return AppController.routeText(rail.entries[j], field)
        return ""
    }
    readonly property list<var> noRows: []
    /// The extensions' entries as text: made again only when they change.
    readonly property string itemsKey: Workspace.extensions !== null
                                       ? Workspace.extensions.contributionsKey("sidebar", Workspace.extensions.appRevision) : ""
    /// The drawer (narrow windows): each view's short name under its icon, as a navigation rail of
    /// Material 3 — a phone has no tooltips to tell the icons apart.
    readonly property bool labelled: Theme.narrow
    /// The buttons' squares and the room on both sides of them.
    readonly property int buttonSize: Theme.touchUi ? Theme.touchTarget : Theme.controlHeight
    readonly property int sidePadding: Theme.space2
    /// A short name's line under its button (labelled).
    readonly property int labelHeight: labelled ? Theme.textXs + 6 : 0
    readonly property int collapsedWidth: labelled ? 76 : buttonSize + 2 * sidePadding
    /// The width with the names (expandable).
    readonly property int expandedWidth: 216

    /// Widened: the pointer rested on it, the keyboard's focus is in it, or its notebook menu is
    /// open (the menu stays beside its button).
    readonly property bool expanded: expandable && (pointerOpened || keyboardItem !== null
                                                    || (notebookMenuPopup !== null && notebookMenuPopup.visible))
    /// The pointer has rested on the rail (openTimer) and not left it since.
    property bool pointerOpened: false
    /// The rail's control with the keyboard's focus (Tab), null: none.
    property Item keyboardItem: null
    /// The names are shown: widened, or widening/narrowing (the surface clips them meanwhile).
    readonly property bool namesShown: expandable && surface.overhang > 1
    /// The width it covers: wider than its place in the layout (implicitWidth) while expanded.
    /// The owner sizes it so (AppShell): a pointer is delivered only within an item's parents, so
    /// the part over the page panel would take no clicks otherwise.
    readonly property real faceWidth: surface.width

    function go(route: var): void {
        AppController.navigate(route)
        rail.navigated()
    }
    /// A view of the app is offered: not hidden in the settings, the built-in extensions' views
    /// only while switched on (Settings → Extensions).
    function shows(view: string): bool {
        // Code mode (docs/CODE.md): its threads alone; the notebook's views in Notes mode.
        if (AppSettings.appMode === "code")
            return view === "code"
        return view !== "code" && AppSettings.sidebarHidden.indexOf(view) < 0 && (view !== "gym" || AppSettings.gymEnabled)
                && (view !== "habits" || AppSettings.habitsEnabled)
                && (view !== "flashcards" || AppSettings.flashcardsEnabled)
    }
    function groupOf(view: string): int {
        return AppSettings.railGroup(view)
    }
    /// The entry at `index` is the first shown one of its group, after another group's: a line
    /// above it.
    function startsGroup(index: int): bool {
        const view = rail.entryViews[index]
        if (!rail.shows(view))
            return false
        const group = rail.groupOf(view)
        let after = false
        for (let j = index - 1; j >= 0; --j) {
            const before = rail.entryViews[j]
            if (!rail.shows(before))
                continue
            if (rail.groupOf(before) === group)
                return false
            after = true
        }
        return after
    }
    /// A control of the rail took or lost the keyboard's focus.
    function keyboardFocus(item: Item, on: bool): void {
        if (on)
            rail.keyboardItem = item
        else if (rail.keyboardItem === item)
            rail.keyboardItem = null
    }

    implicitWidth: collapsedWidth
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Views")

    Timer {
        id: openTimer
        interval: 250
        onTriggered: rail.pointerOpened = true
    }
    Timer {
        id: closeTimer
        interval: 150
        onTriggered: rail.pointerOpened = false
    }
    onExpandableChanged: if (!rail.expandable) rail.pointerOpened = false

    BShadow {
        target: surface
        level: rail.namesShown ? 2 : 0
    }
    // The rail's face: as wide as the rail, or wider over the page panel while expanded.
    Rectangle {
        id: surface
        objectName: "railSurface"
        /// The room it takes over the page panel; only this animates (the rail's own width, set
        /// as it is made, must not: its clipped buttons would take no clicks meanwhile).
        property real overhang: rail.expanded ? rail.expandedWidth - rail.collapsedWidth : 0
        width: rail.collapsedWidth + overhang
        height: rail.height
        clip: rail.expandable
        // The darkest layer on the graphite background (Discord's column of servers).
        color: Theme.paneLines ? Theme.bgSunken : Theme.bgSunken.darker(1.25)
        Behavior on overhang {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        // The pointer moving onto the rail and resting there widens it, leaving it narrows it after
        // a moment (a pointer crossing the window's edge on its way to the page panel does not open
        // it). It must have moved over it: a window that opens under a still pointer (in its
        // corner, where the notebook's mark is) stays narrow.
        HoverHandler {
            id: railHover
            /// Where the pointer came onto the rail (valid once `entered`).
            property point enteredAt
            property bool entered: false
            enabled: rail.expandable
            onHoveredChanged: {
                if (railHover.hovered) {
                    closeTimer.stop()
                } else {
                    railHover.entered = false
                    openTimer.stop()
                    if (rail.pointerOpened)
                        closeTimer.restart()
                }
            }
            onPointChanged: {
                if (!railHover.hovered || rail.pointerOpened || openTimer.running)
                    return
                const at = railHover.point.position
                if (!railHover.entered) {
                    railHover.enteredAt = at
                    railHover.entered = true
                } else if (Math.abs(at.x - railHover.enteredAt.x) + Math.abs(at.y - railHover.enteredAt.y) > 2) {
                    openTimer.start()
                }
            }
        }

        // Over the page panel: its clicks, wheel turns and hovering stay here.
        MouseArea {
            anchors.fill: parent
            enabled: rail.namesShown
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onWheel: (wheel) => wheel.accepted = true
        }

        // The line between the rail and the page panel (or the page, the panel hidden).
        Rectangle {
            visible: Theme.paneLines || rail.namesShown
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.borderSubtle
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: rail.topInset + Theme.space2
            anchors.bottomMargin: rail.bottomInset + Theme.space2
            anchors.rightMargin: 1
            spacing: Theme.space1

            // ---- The notebook: its switcher ------------------------------------------------
            T.Button {
                id: notebookButton
                objectName: "notebookSwitch"
                /// The accessible name and what UI tests look for.
                readonly property string label: qsTr("Switch notebook")
                Layout.alignment: rail.labelled ? Qt.AlignHCenter : Qt.AlignLeft
                Layout.leftMargin: rail.labelled ? 0 : rail.sidePadding
                Layout.rightMargin: rail.labelled ? 0 : rail.sidePadding
                Layout.fillWidth: rail.namesShown
                Layout.bottomMargin: Theme.space1
                implicitWidth: rail.buttonSize
                implicitHeight: rail.buttonSize
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: label
                Accessible.description: Workspace.name
                onVisualFocusChanged: rail.keyboardFocus(notebookButton, notebookButton.visualFocus)
                // (Expandable: the open menu widens the rail, so it opens beside the widened one.)
                onClicked: rail.notebookMenuNow().popup(notebookButton, (rail.expandable ? rail.expandedWidth - notebookButton.x
                                                                                         : notebookButton.width) + Theme.space1, 0)

                background: Item {
                    BFocusRing {
                        visible: notebookButton.visualFocus
                        targetRadius: Theme.radius
                    }
                }
                contentItem: Item {
                    // A circle that squares its corners under the pointer, as Discord's servers.
                    BAvatar {
                        x: Math.round((rail.buttonSize - width) / 2)
                        anchors.verticalCenter: parent.verticalCenter
                        size: Math.round(rail.buttonSize * 0.8)
                        name: Workspace.name
                        active: notebookButton.hovered || notebookButton.down
                    }
                    // Expanded: the notebook's name and the switcher's mark.
                    BText {
                        visible: rail.namesShown
                        x: rail.buttonSize + Theme.space2
                        width: parent.width - x - Theme.iconSizeSm - Theme.space2
                        anchors.verticalCenter: parent.verticalCenter
                        text: Workspace.name
                        elide: Text.ElideRight
                        font.weight: Theme.weightSemiBold
                        Accessible.ignored: true
                    }
                    BIcon {
                        visible: rail.namesShown
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        name: "chevrons-up-down"
                        size: Theme.iconSizeSm
                        color: Theme.fgMuted
                    }
                }
                // No tooltip: the panel beside it names the notebook, and the window's corner is
                // where a pointer rests when the app starts (a tooltip would greet every start).
            }

            // The mode: the notebook, or Code mode's projects and AI agents' threads (docs/CODE.md).
            RailButton {
                objectName: "modeSwitch"
                Layout.bottomMargin: Theme.space1
                label: AppSettings.appMode === "code" ? qsTr("Notes mode") : qsTr("Code mode")
                shortName: AppSettings.appMode === "code" ? qsTr("Notes", "the drawer's short name of Notes mode")
                                                          : qsTr("Code", "the drawer's short name of Code mode")
                iconName: AppSettings.appMode === "code" ? "notebook-pen" : "code"
                onClicked: {
                    AppSettings.appMode = AppSettings.appMode === "code" ? "notes" : "code"
                    rail.navigated()
                }
            }

            // ---- The views (scrolling when the window is too low) ---------------------------
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
                        model: rail.entryViews
                        delegate: RailButton {
                            id: entry
                            required property int index
                            required property string modelData
                            readonly property string entryView: entry.modelData
                            readonly property string entryAlso: rail.entryField(entry.modelData, "entryAlso")
                            readonly property string entryTitle: rail.entryField(entry.modelData, "entryTitle")
                            readonly property string entryIcon: rail.entryField(entry.modelData, "entryIcon")
                            readonly property string entryShort: rail.entryField(entry.modelData, "entryShort")
                            objectName: "sidebarNav_" + entryView
                            shortName: entryShort
                            // Hidden in the settings: not shown (the layout leaves no gap).
                            visible: rail.shows(entryView)
                            groupStart: rail.startsGroup(index)
                            badge: entryView === "flashcards" ? Workspace.extensions.flashcardsDue : 0
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
                        shortName: qsTr("Import", "the drawer's short name of Import notes")
                        iconName: "download"
                        selected: rail.routeView === "importExport"
                        onClicked: rail.go({ view: "importExport", tab: "import" })
                    }
                    // The extensions' entries (b.app.contribute("sidebar", …)) after the app's own,
                    // a group of their own; one that opens a view is selected while that view shows.
                    Repeater {
                        model: rail.itemsKey !== "" ? Workspace.extensions.contributions("sidebar", rail.itemsKey.length) : rail.noRows
                        delegate: RailButton {
                            id: extensionEntry
                            required property var modelData
                            required property int index
                            required property string extension
                            required property string tooltip
                            required property string view
                            readonly property string itemId: UiModels.stringField(extensionEntry.modelData, "id")
                            objectName: "sidebarNav_ext_" + extensionEntry.itemId
                            groupStart: extensionEntry.index === 0
                            iconName: UiModels.stringField(extensionEntry.modelData, "icon") !== ""
                                      ? UiModels.stringField(extensionEntry.modelData, "icon") : "puzzle"
                            toolTip: extensionEntry.tooltip
                            // (The drawer: its tooltip under the icon, elided; expanded: beside it.)
                            shortName: extensionEntry.tooltip
                            selected: extensionEntry.view !== "" && rail.routeView === "extension"
                                      && AppController.routeText(AppController.currentRoute, "extension") === extensionEntry.extension
                                      && AppController.routeText(AppController.currentRoute, "name") === extensionEntry.view
                            onClicked: {
                                AppScripting.activate("sidebar", extensionEntry.extension, extensionEntry.itemId, "", -1)
                                rail.navigated()
                            }
                        }
                    }
                }
            }

            // ---- Sync status and settings --------------------------------------------------
            // A click syncs now (with a sync engine, not while a sync runs); without one, while it
            // runs, a right click or a long press: the sync center. Expanded: its status beside.
            ShellSyncFooter {
                id: railSync
                objectName: "railSync"
                compact: true
                textShown: rail.namesShown
                syncOnClick: AppController.syncAvailable
                Layout.alignment: rail.labelled ? Qt.AlignHCenter : Qt.AlignLeft
                // (Its square is a little larger than the buttons': centred on their icons.)
                Layout.leftMargin: rail.labelled ? 0 : rail.sidePadding - Math.round((railSync.squareSize - rail.buttonSize) / 2)
                Layout.rightMargin: rail.labelled ? 0 : rail.sidePadding
                Layout.fillWidth: rail.namesShown
                onVisualFocusChanged: rail.keyboardFocus(railSync, railSync.visualFocus)
                onClicked: {
                    if (railSync.syncOnClick && railSync.syncState !== "syncing")
                        AppController.requestSync()
                    else
                        rail.go({ view: "sync" })
                }
                onPressAndHold: rail.go({ view: "sync" })
                // Only devices with buttons: Qt hands a finger to it whatever acceptedButtons.
                TapHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                    acceptedButtons: Qt.RightButton
                    onTapped: rail.go({ view: "sync" })
                }
            }
            RailButton {
                objectName: "railSettings"
                label: qsTr("Settings")
                shortName: qsTr("Settings")
                iconName: "settings"
                shortcut: ShortcutMap.sequences["go.settings"] || ""
                selected: rail.routeView === "settings"
                onClicked: rail.go({ view: "settings" })
            }
        }
    }

    /// One view: its icon (and its name beside it while expanded), the selected one tinted in the
    /// accent with a pill at the rail's edge.
    component RailButton: BIconButton {
        id: button
        property bool selected: false
        /// A count at the icon's corner (the flashcards to study now); 0: none.
        property int badge: 0
        /// The name under the icon in the drawer ("" none).
        property string shortName
        /// The first of a group: a line above it.
        property bool groupStart: false
        /// The name beside the icon (expanded): an extension's tooltip, else the label.
        readonly property string name: toolTip !== "" ? toolTip : label
        checked: selected
        toolTipBeside: true
        // (Expandable: the name shows beside the icon instead.)
        showToolTip: !rail.labelled && !rail.expandable
        Layout.alignment: rail.labelled ? Qt.AlignHCenter : Qt.AlignLeft
        Layout.leftMargin: rail.labelled ? 0 : rail.sidePadding
        Layout.rightMargin: rail.labelled ? 0 : rail.sidePadding
        Layout.fillWidth: rail.namesShown
        Layout.topMargin: groupStart ? Theme.space3 : 0
        Layout.bottomMargin: rail.labelHeight
        iconColor: selected ? Theme.accentText : hovered || down ? Theme.fg : Theme.fgMuted
        Accessible.checked: selected
        onVisualFocusChanged: rail.keyboardFocus(button, button.visualFocus)

        background: Rectangle {
            radius: Theme.radiusLg
            color: button.selected ? Theme.accentBg : button.down ? Theme.bgPressed : button.hovered ? Theme.bgHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            BFocusRing {
                visible: button.visualFocus
                targetRadius: Theme.radiusLg
            }
        }
        contentItem: Item {
            // At the same place narrow and expanded: the icons do not move as the rail widens.
            BIcon {
                x: Math.round((rail.buttonSize - width) / 2)
                anchors.verticalCenter: parent.verticalCenter
                name: button.iconName
                size: Theme.iconSize
                color: button.iconColor
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            }
            BText {
                visible: rail.namesShown
                x: rail.buttonSize
                width: (shortcutKeys.visible ? shortcutKeys.x - Theme.space2 : parent.width - Theme.space2) - x
                anchors.verticalCenter: parent.verticalCenter
                text: button.name
                elide: Text.ElideRight
                font.pixelSize: Theme.textSm
                font.weight: button.selected ? Theme.weightSemiBold : Theme.weightMedium
                color: button.selected ? Theme.fg : Theme.fgMuted
                Accessible.ignored: true
            }
            // The shortcut under the pointer (the name's place for the count of a badge).
            BKbd {
                id: shortcutKeys
                visible: rail.namesShown && (button.hovered || button.visualFocus) && button.badge <= 0 && button.shortcut !== ""
                anchors.right: parent.right
                anchors.rightMargin: Theme.space2
                anchors.verticalCenter: parent.verticalCenter
                sequence: button.shortcut
                small: true
            }
        }
        // The line above a group, across the rail (a short one while narrow).
        Rectangle {
            visible: button.groupStart
            width: rail.namesShown ? surface.width - 4 * rail.sidePadding : Math.round(rail.buttonSize * 0.6)
            height: 1
            x: -button.x + Math.round((surface.width - width) / 2)
            y: -Math.round((Theme.space3 + Theme.space1) / 2) - 1
            color: Theme.borderStrong
        }
        // Discord's pill at the rail's edge: tall for the selected view, short under the pointer.
        Rectangle {
            objectName: "railPill"
            visible: height > 0
            x: -button.x - 3
            anchors.verticalCenter: parent.verticalCenter
            width: 6
            height: button.selected ? Math.round(button.height * 0.6) : button.hovered ? 8 : 0
            radius: 3
            color: Theme.fg
            Behavior on height { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
        BBadge {
            objectName: "railBadge"
            visible: button.badge > 0
            x: rail.namesShown ? button.width - width - Theme.space2 : button.width - width + 2
            y: rail.namesShown ? Math.round((button.height - height) / 2) : 1
            count: button.badge
            variant: "accent"
            Accessible.ignored: true
        }
        Accessible.description: button.badge > 0 ? qsTr("%n to study now", "", button.badge) : ""
        // The short name (the drawer): a tap on it is a tap on the button.
        BText {
            visible: rail.labelled && button.shortName !== ""
            x: Math.round((button.width - width) / 2)
            y: button.height
            width: Math.min(implicitWidth, rail.width - 6)
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            text: button.shortName
            font.pixelSize: Theme.textXs
            font.weight: button.selected ? Theme.weightSemiBold : Theme.weightMedium
            color: button.selected ? Theme.fg : Theme.fgMuted
            Accessible.ignored: true
            TapHandler {
                onTapped: button.clicked()
            }
        }
    }

    // Quick switch between notebooks: the recent ones, the documentation, any other folder.
    // (Made the first time it opens: docs/PERFORMANCE.md "L10".)
    property BMenu notebookMenuPopup: null
    function notebookMenuNow(): BMenu {
        if (!rail.notebookMenuPopup)
            rail.notebookMenuPopup = notebookMenuComponent.createObject(rail) as BMenu
        return rail.notebookMenuPopup
    }
    Component {
        id: notebookMenuComponent
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
                text: qsTr("Brulion Documentation")
                iconName: "circle-help"
                // (Its key opens the documentation; from the documentation it goes back.)
                shortcut: docsItem.isCurrent ? "" : ShortcutMap.sequences["vault.docs"] || ""
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
}
