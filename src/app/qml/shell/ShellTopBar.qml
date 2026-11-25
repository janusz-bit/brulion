pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellTopBar — the bar above the current view: the drawer button (narrow; wide windows leave room
// for the page panel's button of AppShell, which never moves), back/forward, the title
// (a breadcrumb for pages in folders and for All pages in a folder), search, command palette,
// reading mode, quick capture, side panel toggle and the "more" menu with page actions (Move to…, Show in
// tree among them). On narrow windows it keeps only what fits a phone: the drawer button ☰,
// always first (the same place whatever the view, the keyboard or the history), back (with
// history, and not on Android, whose back key / gesture walks the same history: `systemBack`),
// the title, the AI assistant's ✨ (while it is on: AppSettings.assistantEnabled), the reading
// mode's book (pages and the journal), quick capture + (AppSettings.captureButtonShown), search
// only while the bottom navigation has no Search tab (so the two bars never repeat a button), and
// the menu. The ✨'s menu talks to the assistant (src/core/extensions/builtin/asystent.luau) in the
// AI chat (ChatView, route "chat"): a new conversation about the page on screen (in the journal's
// stream today's note) — it searches and reads the whole notebook and changes it with the user's
// yes —, beside the page in the side panel (wide windows), the conversations, a summary of that
// page as a new conversation; the button spins while a model answers (WorkspaceExtensions.aiPending).
// Phones (narrow windows) show no tab bar: with two tabs or more a square with their number comes
// before Search — the tab switcher, a menu of the tabs (one tap shows it) with New tab, Close
// tab and Close other tabs (docs/UI.md "Tabs, split view and windows").
// On a picture attachment (`picture`) the menu's "Markdown source" is the picture bar's "Show as
// text" / "Show picture" (Ctrl+E there too), and the note mode is not offered (no note to switch).
// Extensions (docs/UI.md "Extension items in the UI"): their buttons (b.app.contribute("toolbar", …))
// before More — on phones the first items of More —, their page items (`pageMenu`) in More while a
// page shows; the "Live" chip (ShellLiveIndicator) after the title while an AI agent or another
// live client changed the app.
// `topInset` extends the bar under the Android status bar (edge-to-edge).
Rectangle {
    id: bar

    property string title
    /// Folder parts above the title (pages in folders): their titles, and the paths a crumb
    /// opens in All pages ("" = the notebook's root).
    property list<string> crumbs
    property list<string> crumbPaths
    property real topInset: 0
    /// Wide windows: how much of the bar, from its left edge, the page panel's button covers
    /// (AppShell's sidebarToggle stays in one place over this bar or over the panel): the row
    /// starts after it.
    property real leadingRoom: 0
    /// The system has a back key / gesture of its own that walks the history (Android): narrow
    /// windows leave the top bar's back out.
    property bool systemBack: AppController.isAndroid
    /// The view shows a picture attachment, as text (`pictureAsText`) or as the picture.
    property bool picture: false
    property bool pictureAsText: false
    /// The view shows a PDF in the PDF viewer / another file that is no note, read-only: the
    /// Markdown, note mode, properties and history items are none of theirs; a PDF offers "Open in
    /// another app" (the browser: "Open in a new tab").
    property bool pdf: false
    property bool fileOnly: false
    /// Narrow windows: the view has a context panel (the side panel's drawer, contextRequested()).
    property bool contextAvailable: false
    /// The note's mode shown, "document" | "outline" ("" for none to switch: ShellRouteView.viewNoteMode).
    property string noteMode

    signal menuClicked()
    signal paletteRequested()
    /// The search field: the page switcher (pages, "> " for the commands, the notes' text).
    signal switcherRequested()
    signal captureRequested()
    signal newPageRequested()
    signal renameRequested(string path)
    signal deleteRequested(string path)
    signal moveRequested(string path)
    signal revealRequested(string path)
    signal aboutRequested()
    /// The side panel's drawer was asked for (narrow windows).
    signal contextRequested()

    readonly property var route: AppController.currentRoute
    readonly property string routeView: AppController.currentView
    readonly property string routePath: titles.field(AppController.currentRoute, "path")
    /// A daily note opened as a page ("journal" route) whose file exists: it gets the page
    /// actions too (favorite, history, side panel, trash), but not Rename (its name is its date).
    readonly property string journalPagePath: {
        const date = titles.field(AppController.currentRoute, "date")
        // Follows the names (the daily note created or deleted; read for real: a bare `void
        // Workspace.namesRevision` is compiled away with the dependency), only on a journal route.
        if (bar.routeView !== "journal" || date === "" || Workspace.namesRevision < 0)
            return ""
        const path = bar.routePath !== "" ? bar.routePath : Workspace.journalPath(date)
        return Workspace.exists(path) ? path : ""
    }
    readonly property bool onPage: routeView === "page" || journalPagePath !== ""
    /// The view shows a page in the editor (a page or a daily note).
    readonly property bool inEditor: onPage || routeView === "journal" || routeView === "journals"
    /// ... and it is a note (no picture, PDF or other file): reading mode, properties, note mode.
    readonly property bool inNote: inEditor && !picture && !pdf && !fileOnly
    readonly property string pagePath: routeView === "page" ? routePath : journalPagePath
    // (Workspace.favorites: followed; it lists the favourites that exist.)
    readonly property bool favorite: onPage && Workspace.favorites.length >= 0 && Workspace.isFavorite(bar.pagePath)
                                     && Workspace.exists(bar.pagePath)
    /// The page's tasks are ignored (`tasks:: ignore`): the menu offers to show them again. (Read
    /// again when some note's properties changed: Workspace.propertiesRevision.)
    readonly property bool tasksIgnored: onPage && Workspace.propertiesRevision >= 0 && Workspace.tasksIgnored(bar.pagePath)

    ShellRouteTitles { id: titles }
    /// A model is answering (b.ai: the assistant or another extension).
    readonly property bool aiBusy: Workspace.extensions !== null && Workspace.extensions.aiPending > 0
    /// The chat on screen shows what the AI does (its words as they come): the ✨ does not spin —
    /// every frame of it would be the whole window drawn again, the model's processor time.
    readonly property bool aiSpins: bar.aiBusy && !Workspace.chats.runningShown
    /// The page the AI menu is about (set as it opens): the one on screen; in the journal's stream
    /// today's note when it exists; "" for none.
    property string aiPage: ""
    function openAiMenu(): void {
        const today = Workspace.journalPath(DatesHelper.today)
        aiPage = bar.pagePath !== "" ? bar.pagePath
               : bar.routeView === "journals" && Workspace.exists(today) ? today : ""
        const menu = bar.aiMenuNow()
        menu.popup(aiButton, aiButton.width - menu.width, aiButton.height)
    }
    /// The name of aiPage as the chat takes it ("" for none).
    readonly property string aiPageName: bar.aiPage !== "" ? Workspace.pageTitle(bar.aiPage) : ""
    /// A new conversation of the AI chat about aiPage (`prompt`: sent at once), in the side panel
    /// with `beside`.
    function newChat(prompt: string, beside: bool): void {
        AppController.navigate({ view: "chat", new: true, page: bar.aiPageName, prompt: prompt }, beside)
    }
    /// The back button is shown (wide windows: always, disabled without history).
    readonly property bool backShown: !Theme.narrow || (AppController.canGoBack && !bar.systemBack)

    implicitHeight: topInset + (Theme.touchUi ? 56 : 46)
    color: Theme.bg

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSubtle
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: bar.topInset
        anchors.leftMargin: Theme.narrow ? 4 : Theme.space2
        anchors.rightMargin: Theme.narrow ? 4 : Theme.space2
        spacing: Theme.narrow ? 0 : 2

        // Narrow windows: the notebook's mark opens the drawer (its views, pages and settings) —
        // the place of ☰, as an account's picture in a chat app.
        T.Button {
            id: menuButton
            objectName: "topBarMenu"
            visible: Theme.narrow
            implicitWidth: Math.max(Theme.touchTarget, Theme.controlHeight)
            implicitHeight: implicitWidth
            hoverEnabled: !Theme.touchUi
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Menu")
            Accessible.description: Workspace.name
            onClicked: bar.menuClicked()
            background: Item {
                BFocusRing {
                    visible: menuButton.visualFocus
                    targetRadius: menuButton.width / 2
                }
            }
            contentItem: Item {
                BAvatar {
                    anchors.centerIn: parent
                    size: 30
                    name: Workspace.name
                    active: menuButton.hovered || menuButton.down
                    scale: menuButton.down ? Theme.pressScale : 1
                    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutBack } }
                }
            }
        }
        // The room of the page panel's button (less the row's margin; the spacing after it
        // stays the gap between that button and Back).
        Item {
            visible: !Theme.narrow && bar.leadingRoom > Theme.space2
            Layout.preferredWidth: Math.max(0, bar.leadingRoom - Theme.space2)
            Layout.preferredHeight: 1
        }
        BIconButton {
            objectName: "topBarBack"
            visible: bar.backShown
            enabled: AppController.canGoBack
            iconName: "arrow-left"
            label: qsTr("Back")
            shortcut: ShortcutMap.sequences["nav.back"] || ""
            onClicked: AppController.back(AppController.focusedTab)
        }
        BIconButton {
            visible: !Theme.narrow
            enabled: AppController.canGoForward
            iconName: "arrow-right"
            label: qsTr("Forward")
            shortcut: ShortcutMap.sequences["nav.forward"] || ""
            onClicked: AppController.forward(AppController.focusedTab)
        }

        // Title or breadcrumb.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // (The row itself starts 4 px in on narrow windows.)
            Layout.leftMargin: !Theme.narrow ? Theme.space2 : 4

            BText {
                visible: bar.crumbs.length === 0
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                text: bar.title
                variant: "title"
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }
            BBreadcrumb {
                visible: bar.crumbs.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                model: Js.appended(Js.fromStrings(bar.crumbs), bar.title)
                onActivated: (index) => {
                    if (index < 0 || index >= bar.crumbPaths.length)
                        return
                    const path = bar.crumbPaths[index]
                    // (`if`, not `cond ? {…} : {…}`: that compiles to undefined — docs/UI.md "Compiled QML".)
                    if (path !== "")
                        AppController.navigate({ view: "allPages", path: path })
                    else
                        AppController.navigate({ view: "allPages" })
                }
            }
        }

        // AI agents and other live clients changed the app (docs/UI.md "Extension items in the
        // UI"): who and what, undone in one click.
        ShellLiveIndicator {
            objectName: "topBarLive"
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: Theme.narrow ? 0 : Theme.space1
            Layout.rightMargin: Theme.narrow ? 0 : Theme.space1
        }

        // Phones: the tab switcher, a square with the number of tabs (two or more).
        T.AbstractButton {
            id: tabSwitcherButton
            objectName: "topBarTabs"
            visible: Theme.narrow && AppController.tabCount > 1
            implicitWidth: Theme.touchTarget
            implicitHeight: Theme.touchTarget
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Tabs (%1)").arg(AppController.tabCount)
            onClicked: {
                const menu = bar.tabSwitcherNow()
                menu.popup(tabSwitcherButton, tabSwitcherButton.width - menu.width, tabSwitcherButton.height)
            }
            background: Rectangle {
                visible: tabSwitcherButton.down
                radius: Theme.radiusFull
                color: Theme.bgPressed
            }
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.iconSize + 2
                    height: Theme.iconSize + 2
                    radius: Theme.radiusXs
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.fgMuted
                    BText {
                        anchors.centerIn: parent
                        text: AppController.tabCount > 99 ? "99+" : "" + AppController.tabCount
                        variant: "small"
                        font.weight: Theme.weightSemiBold
                        color: Theme.fgMuted
                    }
                }
            }
        }
        // Wide windows: one field for pages and commands — the page switcher opens, "> " in it
        // turns to the command palette, and it offers a search of the notes' text.
        T.Button {
            id: searchField
            objectName: "topBarSearchField"
            visible: !Theme.narrow
            /// What the field says (and what UI tests look for).
            readonly property string label: qsTr("Search or type a command…")
            readonly property string shortcut: ShortcutMap.sequences["page.switch"] || ""
            Layout.preferredWidth: Math.round(Math.min(340, Math.max(170, bar.width * 0.3)))
            Layout.preferredHeight: Theme.controlHeightSm + 2
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            leftPadding: Theme.space2 + 2
            rightPadding: Theme.space1 + 2
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: label
            Accessible.description: shortcut
            onClicked: bar.switcherRequested()

            background: Rectangle {
                radius: Theme.radius
                color: searchField.down ? Theme.bgPressed : Theme.bgSunken
                border.width: 1
                border.color: searchField.hovered ? Theme.borderStrong : Theme.border
                BFocusRing {
                    visible: searchField.visualFocus
                    targetRadius: Theme.radius
                }
            }
            contentItem: RowLayout {
                spacing: Theme.space2
                BIcon {
                    name: "search"
                    size: Theme.iconSizeSm
                    color: Theme.fgFaint
                }
                BText {
                    Layout.fillWidth: true
                    text: searchField.label
                    variant: "small"
                    color: Theme.fgFaint
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                BKbd {
                    visible: searchField.width > 230
                    sequence: searchField.shortcut
                    small: true
                }
            }
        }
        // Phones: the search of the notes' text (the palette is in the menu) — unless the bottom
        // navigation has it.
        BIconButton {
            objectName: "topBarSearch"
            visible: Theme.narrow && AppSettings.bottomNavTabs.indexOf("search") < 0
            iconName: "search"
            label: qsTr("Search")
            shortcut: ShortcutMap.sequences["go.search"] || ""
            onClicked: AppController.search("")
        }
        // What the app told the user (NotificationsView, Workspace.notifications): the bell, its
        // unread count as a badge. Phones: only while something is unread (More keeps it).
        BIconButton {
            id: bellButton
            objectName: "topBarNotifications"
            readonly property int unread: Workspace.notifications.unread
            visible: Workspace.isOpen && (!Theme.narrow || bellButton.unread > 0)
            iconName: bellButton.unread > 0 ? "bell-ring" : "bell"
            label: bellButton.unread > 0 ? qsTr("Notifications: %n unread", "", bellButton.unread) : qsTr("Notifications")
            onClicked: AppController.navigate({ view: "notifications" })
            BBadge {
                objectName: "topBarNotificationsBadge"
                visible: bellButton.unread > 0
                x: parent.width - width + 2
                y: -2
                count: bellButton.unread
                max: 9
                variant: "accent"
                Accessible.ignored: true
            }
        }
        // The AI assistant's commands, a tap away (while it is on): its menu; a spinner while a
        // model answers.
        BIconButton {
            id: aiButton
            objectName: "topBarAi"
            visible: AppSettings.assistantEnabled
            iconName: bar.aiSpins ? "" : "sparkles"
            label: bar.aiBusy ? qsTr("The AI is answering…") : qsTr("AI assistant")
            onClicked: bar.openAiMenu()
            BSpinner {
                anchors.centerIn: parent
                visible: bar.aiSpins
                size: Theme.iconSize
                calm: true // (an answer takes minutes on a phone, whose model needs the cores)
            }
        }
        // Reading mode (docs/UI.md "Reading mode"): a device setting for every page and the journal,
        // at every width (phones read the most); checked while it is on. An eye: the book is the
        // journal's (the rail, the bottom navigation — on a phone both are on screen).
        BIconButton {
            objectName: "topBarReadingMode"
            // (Narrow windows: in ⋮, the bar stays short.)
            visible: bar.inNote && !Theme.narrow
            iconName: "eye"
            label: AppSettings.readingMode ? qsTr("Leave reading mode") : qsTr("Reading mode")
            shortcut: ShortcutMap.sequences["view.reading"] || ""
            // (Not `checkable`: a click must not break the binding to the setting.)
            checked: AppSettings.readingMode
            Accessible.checkable: true
            Accessible.checked: checked
            onClicked: AppSettings.readingMode = !AppSettings.readingMode
        }
        // Quick capture (a note into today's journal): always on wide windows; on phones unless
        // switched off (Settings → Appearance → Layout; capture stays in the palette and under
        // Ctrl+Shift+N).
        BIconButton {
            objectName: "shellCaptureButton"
            visible: !Theme.narrow || AppSettings.captureButtonShown
            iconName: "plus"
            label: qsTr("Quick capture")
            shortcut: ShortcutMap.sequences["capture"] || ""
            onClicked: bar.captureRequested()
        }
        BIconButton {
            visible: !Theme.narrow
            iconName: "panel-right"
            label: AppController.sidePanelVisible ? qsTr("Hide side panel") : qsTr("Show side panel")
            shortcut: ShortcutMap.sequences["panel.toggle"] || ""
            checkable: true
            checked: AppController.sidePanelVisible
            onClicked: AppController.sidePanelVisible = !AppController.sidePanelVisible
        }
        // The extensions' buttons (b.app.contribute("toolbar", …)); phones: at the top of More.
        Repeater {
            model: bar.toolbarKey !== "" ? Workspace.extensions.contributions("toolbar", bar.toolbarKey.length) : bar.noRows
            delegate: BIconButton {
                id: extensionButton
                required property var modelData
                required property string extension
                required property string tooltip
                required property string tone
                readonly property string itemId: UiModels.stringField(extensionButton.modelData, "id")
                objectName: "toolbar_" + extensionButton.itemId
                // A click leaves the keyboard focus where it was (the edited block): the button can
                // go with the extension's next change, and a focused item that goes leaves the
                // focus nowhere. Tab reaches it.
                focusPolicy: Qt.TabFocus
                iconName: UiModels.stringField(extensionButton.modelData, "icon") !== ""
                          ? UiModels.stringField(extensionButton.modelData, "icon") : "puzzle"
                toolTip: extensionButton.tooltip
                Binding on iconColor {
                    when: extensionButton.tone !== ""
                    value: bar.toneColor(extensionButton.tone)
                }
                onClicked: AppScripting.activate("toolbar", extensionButton.extension, extensionButton.itemId, "", -1)
            }
        }
        BIconButton {
            id: moreButton
            objectName: "topBarMore"
            iconName: Theme.narrow ? "ellipsis-vertical" : "ellipsis"
            label: qsTr("More")
            onClicked: bar.openMoreMenu()
        }
    }

    readonly property list<var> noRows: []
    /// The extensions' top bar items as text (WorkspaceExtensions.contributionsKey): the buttons are
    /// made again only when they change, not with every other slot's change.
    readonly property string toolbarKey: !Theme.narrow && Workspace.extensions !== null
                                         ? Workspace.extensions.contributionsKey("toolbar", Workspace.extensions.appRevision) : ""
    /// The text colour of an extension item's `tone` (b.app.contribute): accent, success, danger,
    /// warning, info.
    function toneColor(tone: string): color {
        switch (tone) {
        case "accent": return Theme.accentText
        case "success": return Theme.successText
        case "danger": return Theme.dangerText
        case "warning": return Theme.warningText
        case "info": return Theme.infoText
        default: return Theme.fgMuted
        }
    }

    // The tab switcher (phones): the tabs, the one shown checked like the notebook switcher's
    // notebook; then new / close.
    property BMenu tabSwitcherPopup: null
    function tabSwitcherNow(): BMenu {
        if (!bar.tabSwitcherPopup)
            bar.tabSwitcherPopup = tabSwitcherComponent.createObject(bar) as BMenu
        return bar.tabSwitcherPopup
    }
    Component {
        id: tabSwitcherComponent
        BMenu {
            id: tabSwitcher
            objectName: "tabSwitcher"
            Instantiator {
                model: AppController.tabs
                delegate: BMenuItem {
                    id: entry
                    // (AppController.tabs: { tabId, route, … })
                    required property int tabId
                    required property var route
                    readonly property bool isCurrent: tabId === AppController.focusedTab
                    objectName: "tabSwitcherEntry"
                    text: titles.routeTitle(titles.field(entry.route, "view"),
                                            titles.field(entry.route, "path"),
                                            titles.field(entry.route, "date"),
                                            titles.field(entry.route, "tag"),
                                            titles.field(entry.route, "title"))
                    // (No view icon: a checkable item draws its check where the icon would be.)
                    checkable: true
                    // Not `checked`: a checkable item has already toggled it when onTriggered runs.
                    // (A Binding: it follows isCurrent again after the toggle; onTriggered puts it back.)
                    Binding on checked { value: entry.isCurrent }
                    onTriggered: {
                        entry.checked = entry.isCurrent
                        AppController.selectTab(entry.tabId)
                    }
                }
                onObjectAdded: (index, object) => tabSwitcher.insertItem(index, object as Item)
                // (takeItem: the Instantiator destroys its objects itself.)
                onObjectRemoved: (index, object) => {
                    for (let i = 0; i < tabSwitcher.count; ++i) {
                        if (tabSwitcher.itemAt(i) === object as Item) {
                            tabSwitcher.takeItem(i)
                            return
                        }
                    }
                }
            }
            BMenuSeparator {}
            BMenuItem {
                text: qsTr("New tab")
                iconName: "plus"
                shortcut: ShortcutMap.sequences["tab.new"] || ""
                onTriggered: AppController.newTab()
            }
            BMenuItem {
                text: qsTr("Close tab")
                iconName: "x"
                shortcut: ShortcutMap.sequences["tab.close"] || ""
                onTriggered: AppController.closeTab(AppController.focusedTab)
            }
            BMenuItem {
                objectName: "tabSwitcherCloseOthers"
                text: qsTr("Close other tabs")
                iconName: "x"
                onTriggered: AppController.closeOtherTabs(AppController.focusedTab)
            }
        }
    }

    /// A page action in the menu's first row: its icon over a short name (the accessible name says
    /// all); `on` tints the icon (a favourite).
    component MenuTile: T.Button {
        id: tile
        property string iconName
        property string accessibleName
        property bool on: false
        Layout.fillWidth: true
        Layout.preferredWidth: 1 // equal widths
        implicitHeight: Theme.touchUi ? 58 : 50
        hoverEnabled: !Theme.touchUi
        focusPolicy: Qt.StrongFocus
        padding: 2
        Accessible.role: Accessible.Button
        Accessible.name: accessibleName !== "" ? accessibleName : text
        background: Rectangle {
            radius: Theme.radius
            color: tile.down ? Theme.bgPressed : tile.hovered ? Theme.bgHover : "transparent"
            BFocusRing {
                visible: tile.visualFocus
                targetRadius: Theme.radius
            }
        }
        contentItem: Column {
            spacing: 3
            topPadding: 2
            BIcon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: tile.iconName
                size: Theme.iconSize
                color: tile.on ? Theme.warningText : tile.hovered || tile.down ? Theme.fg : Theme.fgMuted
            }
            BText {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: tile.text
                font.pixelSize: Theme.textXs
                color: Theme.fgMuted
                elide: Text.ElideRight
                Accessible.ignored: true
            }
        }
    }
    /// A small heading over a row of the menu ("Mode", "Open in").
    component MenuLabel: BText {
        leftPadding: Theme.space2
        topPadding: Theme.space1
        bottomPadding: 2
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
    }

    function openMoreMenu(): void {
        const menu = bar.moreMenuNow()
        menu.popup(moreButton, moreButton.width - menu.width, moreButton.height)
    }

    /// The menu's mode switch: the note as a document, an outline or its Markdown source.
    function setMode(mode: string): void {
        if (bar.moreMenuPopup)
            bar.moreMenuPopup.close()
        if (mode === "source") {
            if (!AppController.sourceModeShown)
                AppController.requestUiAction("toggleSourceMode")
            return
        }
        if (AppController.sourceModeShown)
            AppController.requestUiAction("toggleSourceMode")
        if (bar.noteMode !== mode)
            AppController.requestUiAction("toggleNoteMode")
    }

    // The ✨ button's menu: the AI chat — a new conversation (the page on screen goes along), one
    // beside the page, the conversations, a summary of the page —, the AI's settings.
    property BMenu aiMenuPopup: null
    function aiMenuNow(): BMenu {
        if (!bar.aiMenuPopup)
            bar.aiMenuPopup = aiMenuComponent.createObject(bar) as BMenu
        return bar.aiMenuPopup
    }
    Component {
        id: aiMenuComponent
        BMenu {
            id: aiMenu
            objectName: "topBarAiMenu"
            BMenuItem {
                objectName: "aiAsk"
                text: qsTr("New conversation")
                iconName: "message-square"
                onTriggered: bar.newChat("", false)
            }
            BMenuItem {
                objectName: "aiAskBeside"
                visible: AppController.sidePanelAllowed && bar.aiPage !== ""
                height: visible ? implicitHeight : 0
                text: qsTr("New conversation beside the page")
                iconName: "panel-right"
                onTriggered: bar.newChat("", true)
            }
            BMenuItem {
                objectName: "aiChats"
                text: qsTr("Conversations")
                iconName: "history"
                onTriggered: AppController.navigate({ view: "chat" })
            }
            BMenuItem {
                objectName: "aiSummarize"
                text: qsTr("Summarise this page")
                iconName: "scroll-text"
                enabled: bar.aiPage !== ""
                onTriggered: bar.newChat(qsTr("Summarise this page in a few sentences and list its most important tasks."), false)
            }
            BMenuSeparator {}
            BMenuItem {
                objectName: "aiSettings"
                text: qsTr("AI settings…")
                iconName: "settings"
                onTriggered: AppController.navigate({ view: "settings", section: "extensions" })
            }
        }
    }

    // "More" (docs/UI.md "Top bar"): the page's actions as a row of icons, the note's mode, where
    // to open the view, a few actions in a list; the notebook's places in a submenu; Settings.
    // Built ahead in the background (AppShell.prebuildNext(): prebuildMoreMenu()), or made when
    // first opened (docs/PERFORMANCE.md "L10").
    readonly property BMenu moreMenuPopup: moreMenuLoader.item as BMenu
    function moreMenuNow(): BMenu {
        // (Synchronous: an incubation in progress, built ahead, finishes at once.)
        moreMenuLoader.asynchronous = false
        moreMenuLoader.active = true
        return moreMenuLoader.item as BMenu
    }
    function prebuildMoreMenu(): void {
        moreMenuLoader.active = true
    }
    Loader {
        id: moreMenuLoader
        active: false
        asynchronous: true
        sourceComponent: BMenu {
            id: moreMenu
            objectName: "topBarMoreMenu"
            sheet: true

            // ---- Phones: the extensions' top bar buttons (b.app.contribute("toolbar", …)) ------
            ShellContributionItems {
                id: toolbarItems
                menu: moreMenu
                slot: "toolbar"
                shown: Theme.narrow
                onPicked: (extension, id) => AppScripting.activate("toolbar", extension, id, "", -1)
            }
            BMenuSeparator {
                visible: Theme.narrow && toolbarItems.count > 0
                height: visible ? implicitHeight : 0
            }

            // ---- The page: favourite, rename, move, history ------------------------------------
            Item {
                visible: bar.onPage
                implicitWidth: Theme.touchUi ? 280 : 264
                implicitHeight: visible ? tiles.implicitHeight + 4 : 0
                RowLayout {
                    id: tiles
                    anchors.fill: parent
                    anchors.bottomMargin: 4
                    spacing: 2
                    MenuTile {
                        objectName: "menuFavorite"
                        text: qsTr("Favorite")
                        accessibleName: bar.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                        iconName: "star"
                        on: bar.favorite
                        onClicked: {
                            moreMenu.close()
                            Workspace.toggleFavorite(bar.pagePath)
                        }
                    }
                    MenuTile {
                        objectName: "menuRenamePage"
                        visible: bar.routeView === "page"
                        text: qsTr("Rename")
                        accessibleName: qsTr("Rename…")
                        iconName: "pencil"
                        onClicked: {
                            moreMenu.close()
                            bar.renameRequested(bar.pagePath)
                        }
                    }
                    MenuTile {
                        objectName: "menuMovePage"
                        visible: bar.routeView === "page"
                        text: qsTr("Move")
                        accessibleName: qsTr("Move to…")
                        iconName: "folder-input"
                        onClicked: {
                            moreMenu.close()
                            bar.moveRequested(bar.pagePath)
                        }
                    }
                    MenuTile {
                        objectName: "menuPageHistory"
                        visible: !bar.pdf && !bar.fileOnly
                        text: qsTr("History")
                        accessibleName: qsTr("Page history")
                        iconName: "history"
                        onClicked: {
                            moreMenu.close()
                            AppController.navigate({ view: "history", path: bar.pagePath })
                        }
                    }
                }
            }
            BMenuSeparator {
                visible: bar.onPage
                height: visible ? implicitHeight : 0
            }

            // ---- The note's mode: document, outline, Markdown source --------------------------
            Column {
                objectName: "menuNoteMode"
                visible: bar.inNote && bar.noteMode !== ""
                height: visible ? implicitHeight : 0
                bottomPadding: Theme.space1
                spacing: 2
                MenuLabel {
                    text: qsTr("Mode")
                }
                BSegmented {
                    x: Theme.space1
                    width: parent.width - 2 * Theme.space1
                    fill: true
                    size: "small"
                    controlled: true
                    accessibleName: qsTr("Mode")
                    model: [{ value: "document", text: qsTr("Document"), icon: "file-text" },
                            { value: "outline", text: qsTr("Outline"), icon: "list-indent-increase" },
                            { value: "source", text: qsTr("Source"), icon: "code" }]
                    currentValue: AppController.sourceModeShown ? "source" : bar.noteMode
                    onActivated: (value) => bar.setMode(value)
                }
            }
            // A picture: its two views (PageView switches them for this action too).
            BMenuItem {
                objectName: "menuPictureText"
                visible: bar.inEditor && bar.picture
                height: visible ? implicitHeight : 0
                text: bar.pictureAsText ? qsTr("Show picture") : qsTr("Show as text")
                iconName: bar.pictureAsText ? "image" : "code"
                shortcut: ShortcutMap.sequences["page.source"] || ""
                onTriggered: AppController.requestUiAction("toggleSourceMode")
            }

            // ---- Where to open the view (wide windows; phones: a new tab) ----------------------
            Column {
                visible: !Theme.narrow
                height: visible ? implicitHeight : 0
                bottomPadding: 2
                spacing: 0
                MenuLabel {
                    text: qsTr("Open in")
                }
                RowLayout {
                    width: parent.width
                    spacing: 2
                    MenuTile {
                        objectName: "menuOpenInNewTab"
                        text: qsTr("New tab")
                        accessibleName: qsTr("Open in new tab")
                        iconName: "plus"
                        onClicked: {
                            moreMenu.close()
                            AppController.openTab(bar.route)
                        }
                    }
                    MenuTile {
                        objectName: "menuOpenInNewWindow"
                        visible: AppController.windowsAllowed
                        text: qsTr("Window")
                        accessibleName: qsTr("Open in new window")
                        iconName: "app-window"
                        onClicked: {
                            moreMenu.close()
                            AppController.openWindow(bar.route)
                        }
                    }
                    MenuTile {
                        objectName: "menuOpenInSidePanel"
                        visible: bar.onPage
                        text: qsTr("Side panel")
                        accessibleName: qsTr("Open in side panel")
                        iconName: "panel-right"
                        onClicked: {
                            moreMenu.close()
                            AppController.openPage(bar.pagePath, -1, true)
                        }
                    }
                    MenuTile {
                        objectName: "menuSplitView"
                        text: AppController.splitTab !== 0 ? qsTr("Unsplit") : qsTr("Split")
                        accessibleName: AppController.splitTab !== 0 ? qsTr("Close split view") : qsTr("Split view")
                        iconName: AppController.splitTab !== 0 ? "square" : "columns-2"
                        onClicked: {
                            moreMenu.close()
                            if (AppController.splitTab !== 0)
                                AppController.closeSplit()
                            else
                                AppController.splitView()
                        }
                    }
                }
            }
            BMenuItem {
                objectName: "menuOpenInNewTabNarrow"
                visible: Theme.narrow
                height: visible ? implicitHeight : 0
                text: qsTr("Open in new tab")
                iconName: "plus"
                onTriggered: AppController.openTab(bar.route)
            }
            // Narrow windows keep the top bar to the notebook, the title, + and ⋮: reading mode and
            // the side panel's drawer are here (the panel also from the right edge).
            BMenuItem {
                objectName: "menuReadingModeNarrow"
                visible: Theme.narrow && bar.inNote
                height: visible ? implicitHeight : 0
                text: qsTr("Reading mode")
                iconName: "eye"
                checkable: true
                checked: AppSettings.readingMode
                onTriggered: AppSettings.readingMode = !AppSettings.readingMode
            }
            BMenuItem {
                objectName: "menuSidePanelNarrow"
                visible: Theme.narrow && bar.contextAvailable
                height: visible ? implicitHeight : 0
                text: qsTr("Show side panel")
                iconName: "panel-right"
                onTriggered: bar.contextRequested()
            }
            BMenuSeparator {}

            // ---- A few actions --------------------------------------------------------------------
            BMenuItem {
                // The properties panel's add row of the page shown (the day being written in).
                objectName: "menuAddProperty"
                visible: bar.inNote
                height: visible ? implicitHeight : 0
                text: qsTr("Add property")
                iconName: "plus"
                shortcut: ShortcutMap.sequences["page.addProperty"] || ""
                enabled: !AppController.sourceModeShown && !AppSettings.readingMode
                onTriggered: AppController.requestUiAction("addProperty")
            }
            BMenuItem {
                // The page's tasks out of Tasks, the calendar and task queries, or back (the
                // property `zadania:: ignoruj` / `tasks:: ignore`, PageView "toggleTasksIgnored").
                objectName: "menuIgnoreTasks"
                visible: bar.inNote && bar.onPage
                height: visible ? implicitHeight : 0
                text: bar.tasksIgnored ? qsTr("Show this page's tasks in Tasks") : qsTr("Ignore tasks on this page")
                iconName: bar.tasksIgnored ? "list-checks" : "eye-off"
                enabled: !AppController.sourceModeShown && !AppSettings.readingMode
                onTriggered: AppController.requestUiAction("toggleTasksIgnored")
            }
            BMenuItem {
                // The note's own options: its tasks, line numbers, picture frames, text width
                // (PageView "openNoteOptionsDialog", docs/UI.md "Note options").
                objectName: "menuNoteOptions"
                visible: bar.inNote && bar.onPage
                height: visible ? implicitHeight : 0
                text: qsTr("Note options…")
                iconName: "sliders-horizontal"
                enabled: !AppController.sourceModeShown && !AppSettings.readingMode
                onTriggered: AppController.requestUiAction("noteOptions")
            }
            BMenuItem {
                objectName: "menuFindInNote"
                visible: ["page", "journal", "journals"].indexOf(AppController.currentView) >= 0
                height: visible ? implicitHeight : 0
                text: qsTr("Find in note")
                iconName: "search"
                shortcut: ShortcutMap.sequences["page.find"] || ""
                onTriggered: AppController.requestUiAction("findInNote")
            }
            BMenuItem {
                objectName: "menuRevealPage"
                visible: bar.onPage
                height: visible ? implicitHeight : 0
                text: qsTr("Show in tree")
                iconName: "folder-tree"
                onTriggered: bar.revealRequested(bar.pagePath)
            }
            BMenuItem {
                // The note as QR codes another phone's camera reads (docs/UI.md "QR code"); in the
                // journal's stream the day being written in.
                objectName: "menuQrCode"
                visible: bar.inNote
                height: visible ? implicitHeight : 0
                text: qsTr("QR code…")
                iconName: "qr-code"
                onTriggered: AppController.requestUiAction("qrCode")
            }
            BMenuItem {
                id: openInOtherAppItem
                objectName: "menuOpenInOtherApp"
                // A PDF; another attachment of the allow-list (DOCX, XLSX, MP3…: name and first bytes).
                visible: bar.onPage && (bar.pdf || (bar.fileOnly && Workspace.canOpenInOtherApp(bar.pagePath)))
                height: visible ? implicitHeight : 0
                text: Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
                iconName: "external-link"
                onTriggered: Workspace.openInOtherApp(bar.pagePath)
            }
            // The extensions' items of a page's menu (b.app.contribute("pageMenu", …): ctx.page).
            ShellContributionItems {
                menu: moreMenu
                after: openInOtherAppItem
                slot: "pageMenu"
                shown: bar.onPage
                onPicked: (extension, id) => AppScripting.activate("pageMenu", extension, id, bar.pagePath, -1)
            }
            // The page's last item, set apart on both sides (the rule of BMenuItem's header).
            BMenuSeparator {
                visible: bar.onPage
                height: visible ? implicitHeight : 0
            }
            BMenuItem {
                objectName: "menuTrashPage"
                visible: bar.onPage
                height: visible ? implicitHeight : 0
                text: qsTr("Move to trash")
                iconName: "trash-2"
                danger: true
                onTriggered: bar.deleteRequested(bar.pagePath)
            }
            BMenuSeparator {
                visible: bar.inEditor || bar.onPage
                height: visible ? implicitHeight : 0
            }
            // Phones have no search field: the palette is here.
            BMenuItem {
                visible: Theme.narrow
                height: visible ? implicitHeight : 0
                text: qsTr("Command palette")
                iconName: "command"
                shortcut: ShortcutMap.sequences["palette"] || ""
                onTriggered: bar.paletteRequested()
            }

            // ---- The notebook's places (on phones this menu is the only way there besides the
            // palette) ------------------------------------------------------------------------------
            BMenu {
                objectName: "menuNotebook"
                title: qsTr("Notebook")
                iconName: "notebook-pen"

                BMenuItem {
                    text: qsTr("New page")
                    iconName: "square-pen"
                    shortcut: ShortcutMap.sequences["page.new"] || ""
                    onTriggered: bar.newPageRequested()
                }
                BMenuSeparator {}
                BMenuItem {
                    text: qsTr("Sync center")
                    iconName: "folder-sync"
                    onTriggered: AppController.navigate({ view: "sync" })
                }
                BMenuItem {
                    // Straight to the import tab: notes from Obsidian, Logseq, Notion… (docs/UI.md).
                    objectName: "menuImportNotes"
                    text: qsTr("Import notes")
                    iconName: "download"
                    onTriggered: AppController.navigate({ view: "importExport", tab: "import" })
                }
                BMenuItem {
                    text: qsTr("Import and export")
                    iconName: "import"
                    onTriggered: AppController.navigate({ view: "importExport" })
                }
                BMenuItem {
                    objectName: "menuReplaceInNotebook"
                    visible: !AppController.inDocsVault
                    height: visible ? implicitHeight : 0
                    text: qsTr("Replace in notebook")
                    iconName: "repeat"
                    onTriggered: AppController.navigate({ view: "replace" })
                }
                BMenuItem {
                    text: qsTr("Clean up")
                    iconName: "sparkles"
                    onTriggered: AppController.navigate({ view: "cleanup" })
                }
                BMenuItem {
                    objectName: "menuNotifications"
                    text: Workspace.notifications.unread > 0 ? qsTr("Notifications (%1)").arg(Workspace.notifications.unread) : qsTr("Notifications")
                    iconName: "bell"
                    onTriggered: AppController.navigate({ view: "notifications" })
                }
                BMenuItem {
                    text: Workspace.trashCount > 0 ? qsTr("Trash (%1)").arg(Workspace.trashCount) : qsTr("Trash")
                    iconName: "trash-2"
                    onTriggered: AppController.navigate({ view: "trash" })
                }
                BMenuSeparator {}
                BMenuItem {
                    text: qsTr("About Brulion")
                    iconName: "info"
                    onTriggered: bar.aboutRequested()
                }
            }
            BMenuItem {
                text: qsTr("Settings")
                iconName: "settings"
                shortcut: ShortcutMap.sequences["go.settings"] || ""
                onTriggered: AppController.navigate({ view: "settings" })
            }
        }
    }
}
