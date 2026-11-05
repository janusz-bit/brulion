pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellTopBar — the bar above the current view: sidebar/drawer button, back/forward, the title
// (a breadcrumb for pages in folders and for All pages in a folder), search, command palette,
// quick capture, side panel toggle and the "more" menu with page actions (Przenieś do…, Pokaż w
// drzewie among them). On narrow windows it keeps only what fits a phone: back (with history),
// the title, search and the menu; the drawer button ☰ only where the shell says so
// (`menuButtonShown`: the bottom navigation's Menu opens the drawer, so ☰ only shows while that
// bar is not there — a view replaces it, or the on-screen keyboard is up). With no button before
// it, the title starts on the view's content edge.
// Phones (narrow windows) show no tab bar: with two tabs or more a square with their number comes
// before Szukaj — the tab switcher, a menu of the tabs (one tap shows it) with Nowa karta, Zamknij
// kartę and Zamknij pozostałe karty (docs/UI.md "Tabs, split view and windows").
// On a picture attachment (`picture`) the menu's "Markdown source" is the picture bar's "Show as
// text" / "Show picture" (Ctrl+E there too), and the note mode is not offered (no note to switch).
// `topInset` extends the bar under the Android status bar (edge-to-edge).
Rectangle {
    id: bar

    property string title
    /// Folder parts above the title (pages in folders): their titles, and the paths a crumb
    /// opens in All pages ("" = the notebook's root).
    property list<string> crumbs
    property list<string> crumbPaths
    property real topInset: 0
    property bool sidebarVisible: true
    /// Narrow windows: the drawer button ☰ is shown (the shell turns it off while the bottom
    /// navigation, whose Menu opens the same drawer, is shown).
    property bool menuButtonShown: true
    /// The view shows a picture attachment, as text (`pictureAsText`) or as the picture.
    property bool picture: false
    property bool pictureAsText: false
    /// The view shows a PDF in the PDF viewer / another file that is no note, read-only: the
    /// Markdown, note mode, properties and history items are none of theirs; a PDF offers "Otwórz w
    /// innej aplikacji" (the browser: "Otwórz w nowej karcie").
    property bool pdf: false
    property bool fileOnly: false
    /// The note's mode shown, "document" | "outline" ("" for none to switch: ShellRouteView.viewNoteMode).
    property string noteMode

    signal menuClicked()
    signal sidebarToggled()
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

    readonly property var route: AppController.currentRoute
    readonly property string routeView: AppController.currentView
    readonly property string routePath: titles.field(AppController.currentRoute, "path")
    /// A daily note opened as a page ("journal" route) whose file exists: it gets the page
    /// actions too (favorite, history, side panel, trash), but not Rename (its name is its date).
    readonly property string journalPagePath: {
        const date = titles.field(AppController.currentRoute, "date")
        // (Read for real: a bare `void Workspace.revision` is compiled away with the dependency.)
        if (Workspace.revision < 0 || bar.routeView !== "journal" || date === "")
            return ""
        const path = bar.routePath !== "" ? bar.routePath : Workspace.journalPath(date)
        return Workspace.exists(path) ? path : ""
    }
    readonly property bool onPage: routeView === "page" || journalPagePath !== ""
    /// The view shows a page in the editor (a page or a daily note).
    readonly property bool inEditor: onPage || routeView === "journal" || routeView === "journals"
    readonly property string pagePath: routeView === "page" ? routePath : journalPagePath
    // (Workspace.favorites: followed; it lists the favourites that exist.)
    readonly property bool favorite: onPage && Workspace.favorites.length >= 0 && Workspace.isFavorite(bar.pagePath)
                                     && Workspace.exists(bar.pagePath)

    ShellRouteTitles { id: titles }
    /// A button comes before the title (wide windows always have the sidebar toggle).
    readonly property bool leadingButton: !Theme.narrow || bar.menuButtonShown || AppController.canGoBack
    /// Where the title starts with nothing before it: the views' content edge (the editor's
    /// compact padding on phones).
    readonly property int contentEdge: Theme.compact ? Theme.space3 : Theme.space4

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

        BIconButton {
            objectName: "topBarMenu"
            visible: Theme.narrow && bar.menuButtonShown
            iconName: "menu"
            label: qsTr("Menu")
            onClicked: bar.menuClicked()
        }
        BIconButton {
            visible: !Theme.narrow
            iconName: "panel-left"
            label: bar.sidebarVisible ? qsTr("Hide sidebar") : qsTr("Show sidebar")
            checkable: true
            checked: bar.sidebarVisible
            onClicked: bar.sidebarToggled()
        }
        BIconButton {
            visible: !Theme.narrow || AppController.canGoBack
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
            Layout.leftMargin: !Theme.narrow ? Theme.space2 : bar.leadingButton ? 4 : bar.contentEdge - 4

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
                model: Js.appendedString(bar.crumbs, bar.title)
                onActivated: (index) => {
                    if (index < 0 || index >= bar.crumbPaths.length)
                        return
                    const path = bar.crumbPaths[index]
                    AppController.navigate(path !== "" ? { view: "allPages", path: path } : { view: "allPages" })
                }
            }
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
            onClicked: tabSwitcher.popup(tabSwitcherButton, tabSwitcherButton.width - tabSwitcher.width, tabSwitcherButton.height)
            background: Rectangle {
                radius: Theme.radiusFull
                color: tabSwitcherButton.down ? Theme.bgPressed : "transparent"
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
        // Phones: the search of the notes' text (the palette is in the menu).
        BIconButton {
            visible: Theme.narrow
            iconName: "search"
            label: qsTr("Search")
            shortcut: ShortcutMap.sequences["go.search"] || ""
            onClicked: AppController.search("")
        }
        BIconButton {
            visible: !Theme.narrow
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
        BIconButton {
            id: moreButton
            iconName: Theme.narrow ? "ellipsis-vertical" : "ellipsis"
            label: qsTr("More")
            onClicked: bar.openMoreMenu()
        }
    }

    // The tab switcher (phones): the tabs, the one shown checked; then new / close.
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
                objectName: "tabSwitcherEntry"
                text: titles.routeTitle(titles.field(entry.route, "view"),
                                        titles.field(entry.route, "path"),
                                        titles.field(entry.route, "date"),
                                        titles.field(entry.route, "tag"))
                iconName: tabId === AppController.focusedTab ? "check" : "file-text"
                onTriggered: AppController.selectTab(tabId)
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
            onTriggered: AppController.newTab()
        }
        BMenuItem {
            text: qsTr("Close tab")
            iconName: "x"
            onTriggered: AppController.closeTab(AppController.focusedTab)
        }
        BMenuItem {
            objectName: "tabSwitcherCloseOthers"
            text: qsTr("Close other tabs")
            iconName: "x"
            onTriggered: AppController.closeOtherTabs(AppController.focusedTab)
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
    /// A small heading over a row of the menu ("Tryb", "Otwórz w").
    component MenuLabel: BText {
        leftPadding: Theme.space2
        topPadding: Theme.space1
        bottomPadding: 2
        font.pixelSize: Theme.textXs
        color: Theme.fgFaint
    }

    /// The "More" menu (--popup more: screenshots).
    readonly property alias moreMenuPopup: moreMenu
    function openMoreMenu(): void {
        moreMenu.popup(moreButton, moreButton.width - moreMenu.width, moreButton.height)
    }

    /// The menu's mode switch: the note as a document, an outline or its Markdown source.
    function setMode(mode: string): void {
        moreMenu.close()
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

    // "Więcej" (docs/UI.md "Top bar"): the page's actions as a row of icons, the note's mode, where
    // to open the view, a few actions in a list; the notebook's places in a submenu; Ustawienia.
    BMenu {
        id: moreMenu
        objectName: "topBarMoreMenu"

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
            visible: bar.inEditor && bar.noteMode !== "" && !bar.picture && !bar.pdf && !bar.fileOnly
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
        BMenuSeparator {}

        // ---- A few actions --------------------------------------------------------------------
        BMenuItem {
            // The properties panel's add row of the page shown (the day being written in).
            objectName: "menuAddProperty"
            visible: bar.inEditor && !bar.picture && !bar.pdf && !bar.fileOnly
            height: visible ? implicitHeight : 0
            text: qsTr("Add property")
            iconName: "plus"
            enabled: !AppController.sourceModeShown
            onTriggered: AppController.requestUiAction("addProperty")
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
            objectName: "menuOpenInOtherApp"
            // A PDF; another attachment of the allow-list (DOCX, XLSX, MP3…: name and first bytes).
            visible: bar.onPage && (bar.pdf || (bar.fileOnly && Workspace.canOpenInOtherApp(bar.pagePath)))
            height: visible ? implicitHeight : 0
            text: Qt.platform.os === "wasm" ? qsTr("Open in a new tab") : qsTr("Open in another app")
            iconName: "external-link"
            onTriggered: Workspace.openInOtherApp(bar.pagePath)
        }
        BMenuItem {
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
            onTriggered: bar.paletteRequested()
        }

        // ---- The notebook's places (on phones this menu is the only way there besides the
        // palette) ------------------------------------------------------------------------------
        BMenu {
            id: notebookPlaces
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
