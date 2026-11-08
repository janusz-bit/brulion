pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import Brulion

// ContentPageHeader — the top of a page in PageView (the block editor's header, it scrolls with
// the blocks): the title — editable in place (a click; Enter renames through PageView, Escape
// cancels) — with the favourite star and the page menu, previous / next day arrows for daily
// notes, the folder breadcrumb in the side panel (the shell's top bar shows it otherwise), and
// the page's aliases and tags as chips — from the page's properties, editable there: a chip's menu
// (a right-click, a long press) removes one, "+" adds a tag or an alias (the editor shows all
// properties in its "Properties" panel, docs/UI.md "Page properties"). Next to the star, on a wide
// header: the note mode toggle (document / outline; its icon shows the current mode, a click asks
// for the other one: modeToggleRequested) — a small header (phones, the side panel) leaves it to
// the page menu ⋯, the top bar's More and the palette. The page actions (the star, the menu)
// stand at the end of the title row; a page without one (it starts with its own "# Title") puts
// them at the end of its chips' first line, else on a row of their own. An empty page
// gets a "Start writing" button; a page that does not exist yet says that typing creates it.
// A page named by a tag that is in use (docs/SYNTAX.md §3: #tag is a link to this page) gets, at
// every width, "Tag overview" (the tag overview) as a "#" button among the page actions, "Tasks with
// this tag" in the page menu (PageView; also in Tag overview and every #tag's menu), and its
// sub-tags as chips under the title (#work/reports under Work), each opening its own page.
// The page's look (docs/UI.md "Page icon and cover", PageController.look): its icon before the
// title (above the chips for a page that starts with its own "# Title"; a click: iconRequested —
// the icon dialog), its cover — a band above everything, the picture filling its width, moved up
// or down with "Reposition" (a drag, then "Save": `banner_y`), changed with "Change cover"
// (coverRequested). A cover from the internet shows only from the device's cache: "Show" first
// (unless the device allows such pictures), a spinner, the reason it failed; a file that is not in
// the notebook (any more) is named.
// Reading mode (docs/UI.md "Reading mode"; PageController.readOnly; the top bar's book button
// switches it: AppSettings.readingMode): while it is on the title, the icon,
// the cover and the chips change nothing (a click on the title opens no field — the page menu's
// "Rename…" still does —, no "+", no "Remove", no "Change cover"), the note mode toggle and "Start
// writing" are not shown.
//
//   ContentPageHeader {
//       controller: page; editor: editor; tools: tools
//       onRenameRequested: (title) => …; onMenuRequested: (item) => menu.popup(item, …)
//   }
Item {
    id: header

    property PageController controller
    property BlockEditor editor
    property PageTools tools
    /// "YYYY-MM-DD" for a daily note, else "".
    property string journalDate
    /// A weekly or monthly note's period (Workspace.periodicNoteOf), else {}.
    property var periodic: ({})
    /// Its keys: period ("week" | "month", "" for none), start and end ("YYYY-MM-DD"), label.
    readonly property string periodicPeriod: AppController.routeText(periodic, "period")
    readonly property string periodicStart: AppController.routeText(periodic, "start")
    readonly property string periodicEnd: AppController.routeText(periodic, "end")
    readonly property string periodicLabel: AppController.routeText(periodic, "label")
    readonly property bool periodicNote: periodicPeriod !== ""
    readonly property bool weekly: periodicNote && periodicPeriod === "week"
    /// The first block repeats the title as "# Title": no second title above it.
    property bool startsWithTitle: false
    property bool inSidePanel: false
    /// The title field is open.
    property bool editingTitle: false
    /// An error for the title being typed ("" = fine).
    property string titleError

    /// A map a call returned, read with AppController.route*() (compiled calls cannot take a map
    /// held in a local after another call: kept here).
    property var _held: ({})
    signal renameRequested(string title)
    signal menuRequested(Item anchor)
    /// The icon or the cover was clicked to be changed (PageView opens their dialogs).
    signal iconRequested()
    signal coverRequested()
    /// The mode toggle was clicked (PageView switches: PageController.toggleMode).
    signal modeToggleRequested()
    /// The page menu's button (route.open "menu" opens the menu there).
    property Item menuAnchor: null

    readonly property bool small: Theme.compact || inSidePanel
    /// Reading mode: nothing in the header changes the page.
    readonly property bool readOnly: controller !== null && controller.readOnly
    /// The page's look (PageController.look): its icon ({ kind, name }) and its cover ({ kind,
    /// state, url, target, host, error, position }).
    readonly property var pageIcon: header.controller ? header.controller.look.icon : ({})
    readonly property var cover: header.controller ? header.controller.look.cover : ({})
    readonly property bool hasIcon: AppController.routeText(header.pageIcon, "kind") !== ""
    readonly property string coverKind: AppController.routeText(header.cover, "kind")
    readonly property string coverTarget: AppController.routeText(header.cover, "target")
    readonly property bool hasCover: coverKind !== ""
    /// The cover's picture is being moved (a drag): coverDraft is shown until it is written.
    property bool coverMoving: false
    property real coverDraft: 0.5
    /// The cover's `position` as written (0 top … 1 bottom; "" none: the middle).
    readonly property string coverPositionText: AppController.routeText(header.cover, "position")
    readonly property real coverPosition: coverMoving ? coverDraft
                                                      : coverPositionText !== "" ? Js.toNumber(coverPositionText) : 0.5
    function startCoverMove(): void {
        header.coverDraft = header.coverPosition
        header.coverMoving = true
    }
    function finishCoverMove(save: bool): void {
        if (save && header.controller) {
            header._held = header.controller.setPageCoverPosition(header.coverDraft)
            if (!AppController.routeFlag(header._held, "ok"))
                AppController.toast(AppController.routeText(header._held, "reason"), "error")
        }
        header.coverMoving = false
    }
    onPathChanged: header.coverMoving = false
    readonly property string path: controller ? controller.path : ""
    readonly property bool exists: controller ? controller.exists : false
    readonly property bool loaded: controller ? controller.loaded : false
    readonly property bool journal: journalDate !== ""
    readonly property bool documentMode: controller ? controller.documentMode : false
    // Workspace.revision: the summary follows saves (frontmatter edits), renames — the tags naming
    // a renamed page are rewritten after it moved — and other devices. Workspace.indexing: a page
    // opened while the notebook is still being read (at start-up) gets its tags and aliases once
    // the index has it. Both read for real: qmlcachegen compiles a bare `void Workspace.revision`
    // away, and the binding's dependency with it (the tag strip of a renamed page came only with
    // some later change).
    // (PageTools does it: summaryTools below.)
    PageTools {
        id: summaryTools
        workspace: Workspace
        path: header.path
    }
    /// The page's folders: [{ title, path }], outermost first.
    readonly property list<var> folderParts: summaryTools.folderParts
    // (Workspace.favorites read for the binding's dependency.)
    readonly property bool favorite: Workspace.favorites.length >= 0 && header.path !== "" && Workspace.isFavorite(header.path)
    readonly property bool showTitle: !startsWithTitle || journal
    /// The side panel's folder breadcrumb row is shown (it carries the page actions then).
    readonly property bool breadcrumbRow: inSidePanel && folderParts.length > 0
    /// No title row and no other row for the page actions: they stand at the end of the aliases'
    /// and tags' first line, beside the chips (without chips they keep a row of their own).
    readonly property bool actionsWithChips: !showTitle && !breadcrumbRow && chips.visible
    /// The tag that names this page ({ key, text, references, subTags }), or {}.
    readonly property var tagInfo: summaryTools.headerTag
    /// Its key ("" none) and its text ("#praca").
    readonly property string tagKey: AppController.routeText(header.tagInfo, "key")
    readonly property string tagText: AppController.routeText(header.tagInfo, "text")
    /// That tag is in use (tagged blocks or sub-tags): Tag overview among the page actions, Tasks
    /// with this tag in the page menu, the sub-tags under the title.
    readonly property bool tagInUse: tagKey !== ""
                                     && (AppController.routeInt(header.tagInfo, "references", 0) > 0 || subTags.length > 0)
    /// The tag's sub-tags ({ tag, display, count }).
    readonly property list<var> subTags: summaryTools.subTags
    readonly property real textX: editor ? editor.contentX + editor.gutter : 0
    readonly property real textWidth: editor ? editor.contentWidth - editor.gutter : width

    implicitHeight: column.y + column.implicitHeight + (small ? Theme.space3 : Theme.space5)

    function startTitleEdit(): void {
        if (header.journal || !header.exists)
            return
        header.titleError = ""
        titleField.text = header.controller.title
        header.editingTitle = true
        titleField.forceActiveFocus()
        titleField.selectAll()
    }
    function commitTitle(): void {
        const title = "" + Js.trim(titleField.text)
        if (title === header.controller.title || title === "") {
            header.editingTitle = false
            return
        }
        const problem = header.tools.validateTitle(header.path, title)
        if (problem !== "") {
            header.titleError = problem
            return
        }
        header.editingTitle = false
        header.renameRequested(title)
    }
    function cancelTitle(): void {
        header.editingTitle = false
        header.titleError = ""
        if (header.editor)
            header.editor.forceActiveFocus()
    }
    // ---- Tags and aliases of the page's properties (docs/UI.md "Page properties") -------------
    property PropertyChipMenu chipMenu: null
    property BMenu addMenu: null
    /// The menu of the tag or alias `name` (`kind` "tags" | "aliases") at `item`.
    function openChipMenu(kind: string, name: string, item: Item): void {
        if (!header.controller || !header.controller.loaded)
            return
        if (!header.chipMenu)
            header.chipMenu = chipMenuComponent.createObject(header) as PropertyChipMenu
        header.chipMenu.kind = kind
        header.chipMenu.item = name
        header.chipMenu.readOnly = header.readOnly
        header.chipMenu.popup(item, 0, item.height)
    }
    function openAddMenu(item: Item): void {
        if (!header.addMenu)
            header.addMenu = addMenuComponent.createObject(header) as BMenu
        header.addMenu.popup(item, 0, item.height)
    }
    /// Take `name` out of the page's tags / aliases (a toast with "Undo").
    function removeListItem(kind: string, name: string): void {
        // (Strings from calls laundered: a call cannot take one a call before may change.)
        const before = "" + header.controller.undoToken()
        header._held = header.controller.removePageListItem(kind, name)
        if (!AppController.routeFlag(header._held, "ok")) {
            AppController.toast(AppController.routeText(header._held, "reason"), "error")
            return
        }
        const text = "" + header.chipMenu.removedText(kind, name, name)
        if (header.editor)
            header.editor.noticeUndoable(text, "info", before)
        else
            AppController.toast(text, "info")
    }
    // The same menu as the Properties panel's chips (PropertyChipMenu).
    Component {
        id: chipMenuComponent
        PropertyChipMenu {
            id: chipMenuItem
            onOpenRequested: AppController.openTag(chipMenuItem.item)
            onCopyRequested: {
                if (AppController.routeFlag(header.controller.copyText(chipMenuItem.item), "ok"))
                    AppController.toast(qsTr("Copied %1").arg(chipMenuItem.item), "success")
            }
            onRemoveRequested: header.removeListItem(chipMenuItem.kind, chipMenuItem.item)
        }
    }
    Component {
        id: addMenuComponent
        BMenu {
            BMenuItem {
                objectName: "headerAddTag"
                text: qsTr("Add tag")
                iconName: "hash"
                onTriggered: chips.adding = "tags"
            }
            BMenuItem {
                objectName: "headerAddAlias"
                text: qsTr("Add alias")
                iconName: "bookmark"
                onTriggered: chips.adding = "aliases"
            }
            BMenuItem {
                objectName: "headerAddProperty"
                text: qsTr("Add property…")
                iconName: "plus"
                onTriggered: if (header.editor) header.editor.addProperty()
            }
        }
    }

    /// The page's icon, a button opening the icon dialog (iconRequested).
    component IconButton: T.AbstractButton {
        id: iconButton
        property real iconSize: Theme.text3xl
        objectName: "pageIconButton"
        implicitWidth: Math.round(iconSize * 1.3)
        implicitHeight: Math.round(iconSize * 1.3)
        focusPolicy: Qt.StrongFocus
        hoverEnabled: !Theme.touchUi
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Page icon")
        Accessible.description: header.readOnly ? "" : qsTr("Change icon")
        // (Reading mode: the icon is only shown.)
        enabled: !header.readOnly
        onClicked: header.iconRequested()
        background: Rectangle {
            radius: Theme.radius
            color: iconButton.down ? Theme.bgPressed : iconButton.hovered || iconButton.visualFocus ? Theme.bgHover : "transparent"
            border.width: iconButton.visualFocus ? 2 : 0
            border.color: Theme.focusRing
        }
        contentItem: Item {
            BPageIcon {
                anchors.centerIn: parent
                icon: header.pageIcon
                size: Math.round(iconButton.iconSize * 1.05)
                color: Theme.fg
            }
        }
    }

    /// `tag:` query of a tag key (quoted when it has a space, a comma or a quote).
    function tagQuery(key: string): string {
        return "tag:" + (Js.test(key, "[\\s,\"]", "") ? "\"" + key + "\"" : key)
    }
    /// The folder of the breadcrumb's part `index` in all pages.
    function openFolder(index: int): void {
        const part = header.folderParts[index]
        AppController.navigate({ view: "allPages", path: AppController.routeText(part, "path") })
    }
    function openDay(offset: int): void {
        AppController.navigate({ view: "journal", date: DatesHelper.addDays(header.journalDate, offset) })
    }

    Component {
        id: pageActions
        Row {
            spacing: 2
            BIconButton {
                objectName: "pageTagOverview"
                visible: header.tagInUse
                iconName: "hash"
                label: qsTr("Tag overview")
                Accessible.description: qsTr("Everything tagged %1").arg(header.tagText)
                size: "small"
                onClicked: AppController.navigate({ view: "tag", tag: header.tagKey, overview: true })
            }
            BIconButton {
                objectName: "noteModeToggle"
                // A small header leaves it to the page menu ⋯ (also the top bar's More, the palette).
                visible: header.loaded && !header.small && !header.readOnly
                iconName: header.documentMode ? "file-text" : "list"
                label: header.documentMode ? qsTr("Document mode") : qsTr("Outline mode")
                // The name says the current mode; screen readers also hear what a press does.
                Accessible.description: header.documentMode ? qsTr("Switch to outline") : qsTr("Switch to document")
                size: "small"
                onClicked: header.modeToggleRequested()
            }
            BIconButton {
                visible: header.exists
                iconName: "star"
                label: header.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                size: "small"
                checkable: true
                checked: header.favorite
                iconColor: header.favorite ? Theme.warningText : Theme.fgMuted
                onClicked: Workspace.toggleFavorite(header.path)
            }
            BIconButton {
                id: menuButton
                iconName: "ellipsis"
                label: qsTr("Page actions")
                size: "small"
                onClicked: header.menuRequested(menuButton)
                Component.onCompleted: header.menuAnchor = menuButton
                Component.onDestruction: if (header.menuAnchor === menuButton) header.menuAnchor = null
            }
        }
    }

    ColumnLayout {
        id: column
        x: header.textX
        y: header.small ? Theme.space3 : Theme.space8
        width: header.textWidth
        spacing: header.small ? Theme.space1 : Theme.space2

        // ---- Cover (docs/UI.md "Page icon and cover") ------------------------------------------------
        Item {
            id: coverBand
            objectName: "pageCover"
            visible: header.hasCover
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? (header.small ? 112 : 180) : 0
            Layout.bottomMargin: visible ? Theme.space2 : 0
            clip: true
            readonly property string coverState: AppController.routeText(header.cover, "state")
            readonly property bool ready: coverState === "ready" && coverImage.status === Image.Ready

            Rectangle {
                anchors.fill: parent
                radius: Theme.radius
                color: Theme.bgSunken
            }
            Image {
                id: coverImage
                objectName: "pageCoverImage"
                /// The picture at the band's width (its height by its proportions).
                readonly property real naturalHeight: implicitWidth > 0 ? coverBand.width * implicitHeight / implicitWidth
                                                                        : coverBand.height
                width: coverBand.width
                height: Math.max(coverBand.height, naturalHeight)
                y: -(height - coverBand.height) * header.coverPosition
                source: coverBand.coverState === "ready" ? AppController.routeText(header.cover, "url") : ""
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: Math.ceil(Math.max(1, coverBand.width) * Screen.devicePixelRatio)
            }
            // Not shown (yet): why, with what can be done.
            Column {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space4
                spacing: Theme.space2
                visible: !coverBand.ready
                BSpinner {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: coverBand.coverState === "loading" || (coverBand.coverState === "ready" && coverImage.status === Image.Loading)
                    running: visible
                }
                BText {
                    objectName: "pageCoverMessage"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    visible: text !== ""
                    text: coverBand.coverState === "missing" ? qsTr("No picture “%1” in the notebook").arg(header.coverTarget)
                          : coverBand.coverState === "ask" ? qsTr("Cover from %1").arg(AppController.routeText(header.cover, "host"))
                          : coverBand.coverState === "failed" ? (AppController.routeText(header.cover, "error") !== ""
                                                                 ? AppController.routeText(header.cover, "error")
                                                                 : qsTr("The picture could not be loaded"))
                          : coverBand.coverState === "ready" && coverImage.status === Image.Error ? qsTr("The picture could not be loaded")
                          : ""
                    variant: "muted"
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                BButton {
                    objectName: "pageCoverShow"
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: header.coverKind === "web" && (coverBand.coverState === "ask" || coverBand.coverState === "failed")
                    size: "small"
                    text: coverBand.coverState === "failed" ? qsTr("Try again") : qsTr("Show")
                    iconName: coverBand.coverState === "failed" ? "refresh-cw" : "eye"
                    onClicked: if (header.controller) header.controller.showWebPicture(header.coverTarget)
                }
            }
            HoverHandler {
                id: coverHover
                enabled: !Theme.touchUi
            }
            // Moving: the picture follows a vertical drag (none to make for a picture no taller
            // than the band).
            DragHandler {
                id: coverDrag
                enabled: header.coverMoving && coverImage.height > coverBand.height
                target: null
                xAxis.enabled: false
                property real startPosition: 0.5
                onActiveChanged: if (coverDrag.active) coverDrag.startPosition = header.coverDraft
                onTranslationChanged: {
                    const room = coverImage.height - coverBand.height
                    if (coverDrag.active && room > 0)
                        header.coverDraft = Math.max(0, Math.min(1, coverDrag.startPosition - coverDrag.translation.y / room))
                }
            }
            Row {
                objectName: "pageCoverTools"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.space2
                spacing: Theme.space1
                // (With the keys: the page menu's "Change cover…".)
                visible: !header.readOnly && (header.coverMoving || Theme.touchUi || coverHover.hovered)
                BButton {
                    objectName: "pageCoverMove"
                    visible: !header.coverMoving && coverBand.ready
                    size: "small"
                    text: qsTr("Reposition")
                    iconName: "move-up"
                    onClicked: header.startCoverMove()
                }
                BButton {
                    objectName: "pageCoverChange"
                    visible: !header.coverMoving
                    size: "small"
                    text: qsTr("Change cover")
                    iconName: "image"
                    onClicked: header.coverRequested()
                }
                BButton {
                    objectName: "pageCoverMoveCancel"
                    visible: header.coverMoving
                    size: "small"
                    text: qsTr("Cancel")
                    onClicked: header.finishCoverMove(false)
                }
                BButton {
                    objectName: "pageCoverMoveSave"
                    visible: header.coverMoving
                    size: "small"
                    variant: "primary"
                    text: qsTr("Save position")
                    onClicked: header.finishCoverMove(true)
                }
            }
            BText {
                visible: header.coverMoving
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Theme.space2
                text: qsTr("Drag the picture up or down")
                variant: "small"
                color: Theme.fg
                Rectangle {
                    z: -1
                    anchors.fill: parent
                    anchors.margins: -Theme.space1
                    radius: Theme.radiusXs
                    color: Theme.bgElevated
                    opacity: 0.85
                }
            }
        }

        // ---- The icon above everything else when there is no title row ("# Title" first) ------------
        IconButton {
            visible: header.hasIcon && !header.showTitle
            iconSize: header.small ? Theme.text2xl : Theme.text3xl
        }

        // ---- Day navigation / folder (side panel) · favourite · menu ---------------------------------
        // Pages show their folders in the shell's top bar; the side panel has none. The star and
        // the menu go here when there is no title row to carry them (daily notes, a page that
        // starts with its own "# Title" and has no chips to stand beside: actionsWithChips).
        RowLayout {
            visible: header.journal || (!header.showTitle && !header.actionsWithChips) || header.breadcrumbRow
            Layout.fillWidth: true
            spacing: 2

            BBreadcrumb {
                visible: !header.journal && header.breadcrumbRow
                Layout.fillWidth: true
                rootIcon: "folder"
                // (Rows { title, path }: the breadcrumb shows the titles.)
                model: header.folderParts
                onActivated: (index) => header.openFolder(index)
            }
            RowLayout {
                visible: header.journal
                spacing: 2
                BIconButton {
                    iconName: "chevron-left"
                    label: qsTr("Previous day: %1").arg(DatesHelper.formatShort(DatesHelper.addDays(header.journalDate, -1)))
                    size: "small"
                    onClicked: header.openDay(-1)
                }
                BIconButton {
                    iconName: "chevron-right"
                    label: qsTr("Next day: %1").arg(DatesHelper.formatShort(DatesHelper.addDays(header.journalDate, 1)))
                    size: "small"
                    onClicked: header.openDay(1)
                }
                BButton {
                    visible: header.journalDate !== DatesHelper.today
                    size: "small"
                    variant: "ghost"
                    text: qsTr("Today")
                    onClicked: AppController.navigate({ view: "journal", date: DatesHelper.today })
                }
            }
            Item {
                visible: header.journal || !header.breadcrumbRow
                Layout.fillWidth: true
            }
            Loader {
                active: header.journal || (!header.showTitle && !header.actionsWithChips)
                visible: active
                sourceComponent: pageActions
            }
        }

        // ---- Title · favourite · menu -------------------------------------------------------------------
        RowLayout {
            visible: header.showTitle || header.editingTitle
            Layout.fillWidth: true
            spacing: Theme.space2

            IconButton {
                visible: header.hasIcon && header.showTitle && !header.editingTitle
                Layout.alignment: Qt.AlignTop
                iconSize: titleText.font.pixelSize
                implicitHeight: titleText.lineHeight
            }

            Item {
                Layout.fillWidth: true
                implicitHeight: header.editingTitle ? titleField.implicitHeight : titleText.implicitHeight

                Text {
                    id: titleText
                    visible: !header.editingTitle
                    width: parent.width
                    text: header.journal ? DatesHelper.formatLong(header.journalDate) : (header.controller ? header.controller.title : "")
                    textFormat: Text.PlainText
                    font.family: Theme.fontEditor
                    font.pixelSize: header.small ? Theme.text2xl : Theme.text3xl
                    font.weight: Theme.weightSemiBold
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.lineHeightPx(font.pixelSize, Theme.lineHeightTight)
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                    Accessible.description: !header.journal && header.exists && !header.readOnly ? qsTr("Click to rename") : ""

                    // The tap area (rename): a touch target tall on touch UIs, centred on the title.
                    Item {
                        width: parent.width
                        height: Math.max(parent.height, Theme.touchUi ? Theme.touchTarget : 0)
                        y: (parent.height - height) / 2
                        HoverHandler {
                            id: titleHover
                            enabled: !Theme.touchUi && !header.journal && header.exists && !header.readOnly
                            cursorShape: Qt.IBeamCursor
                        }
                        TapHandler {
                            enabled: !header.journal && header.exists && !header.readOnly
                            onTapped: header.startTitleEdit()
                        }
                    }
                    Rectangle {
                        // A hint that the title can be edited.
                        visible: titleHover.hovered
                        anchors.fill: parent
                        anchors.margins: -Theme.space1
                        z: -1
                        radius: Theme.radius
                        color: Theme.bgHover
                    }
                }
                BTextField {
                    id: titleField
                    visible: header.editingTitle
                    width: parent.width
                    font.family: Theme.fontEditor
                    font.pixelSize: header.small ? Theme.text2xl : Theme.text3xl
                    font.weight: Theme.weightSemiBold
                    accessibleName: qsTr("Page title")
                    errorText: header.titleError
                    onTextEdited: header.titleError = ""
                    onAccepted: header.commitTitle()
                    onEscapePressed: header.cancelTitle()
                    Keys.onEscapePressed: header.cancelTitle()
                    onActiveFocusChanged: if (!activeFocus && header.editingTitle && header.titleError === "") header.commitTitle()
                }
            }
            Loader {
                active: !header.journal && header.showTitle
                visible: active
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: header.small ? 0 : Theme.space1
                sourceComponent: pageActions
            }
        }

        // ---- A weekly or monthly note: its period, the one before and after, the current one --------
        RowLayout {
            objectName: "periodicNav"
            visible: header.periodicNote
            Layout.fillWidth: true
            spacing: 2
            BIconButton {
                objectName: "periodicPrevious"
                iconName: "chevron-left"
                label: header.weekly ? qsTr("Previous week") : qsTr("Previous month")
                size: "small"
                onClicked: AppController.openPeriodicNote(header.periodicPeriod, header.periodicStart, -1)
            }
            BIconButton {
                objectName: "periodicNext"
                iconName: "chevron-right"
                label: header.weekly ? qsTr("Next week") : qsTr("Next month")
                size: "small"
                onClicked: AppController.openPeriodicNote(header.periodicPeriod, header.periodicStart, 1)
            }
            BText {
                objectName: "periodicLabel"
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space1
                text: header.periodicNote ? header.periodicLabel : ""
                variant: "muted"
                elide: Text.ElideRight
            }
            BButton {
                readonly property bool current: header.periodicNote && DatesHelper.today >= header.periodicStart
                                                && DatesHelper.today <= header.periodicEnd
                visible: header.periodicNote && !current
                size: "small"
                variant: "ghost"
                text: header.weekly ? qsTr("This week") : qsTr("This month")
                onClicked: AppController.openPeriodicNote(header.periodicPeriod, "", 0)
            }
        }

        // ---- Relative day / "not created yet" ------------------------------------------------------
        BText {
            visible: header.journal && (header.journalDate === DatesHelper.today
                                        || header.journalDate === DatesHelper.addDays(DatesHelper.today, -1)
                                        || header.journalDate === DatesHelper.addDays(DatesHelper.today, 1))
            text: header.journalDate === DatesHelper.today ? qsTr("Today")
                  : header.journalDate === DatesHelper.addDays(DatesHelper.today, -1) ? qsTr("Yesterday") : qsTr("Tomorrow")
            variant: "label"
            color: Theme.accentText
        }
        BText {
            visible: header.loaded && !header.exists
            Layout.fillWidth: true
            text: header.journal ? qsTr("No note for this day yet — it is created when you write something.")
                                 : qsTr("This page does not exist yet — it is created when you write something.")
            variant: "muted"
            wrapMode: Text.Wrap
        }

        // ---- Properties summary ---------------------------------------------------------------------
        // The page's aliases and tags: from its properties once the page is loaded (an edit shows at
        // once), else from the index's summary. A chip from a property has a menu (a right-click, a
        // long press; an alias's tap too): a tag's page or "Remove from this page", an alias copied or
        // removed. "+" at the end of the row adds a tag or an alias in a field there (or opens the
        // properties panel's add row). Without a title row the page actions stand at the end of
        // the first line (chipsActions): the chips wrap short of them.
        Flow {
            id: chips
            objectName: "pageHeaderChips"
            readonly property bool live: header.loaded && header.controller !== null
            // Rows { name, editable, key (tags from the index) }: the page's properties, else the
            // index's summary (PageTools.aliasChips / tagChips).
            readonly property list<var> aliases: live ? chips.liveChips("aliases") : summaryTools.aliasChips
            readonly property list<var> tags: live ? chips.liveChips("tags") : summaryTools.tagChips
            /// The page's properties' "tags" | "aliases" (PageController.pageProperties).
            function liveChips(kind: string): list<var> {
                if (kind === "tags")
                    return header.controller.pageProperties.tags
                return header.controller.pageProperties.aliases
            }
            /// The field adding "tags" | "aliases" is open ("": none).
            property string adding: ""
            property string addError: ""
            /// This page's properties panel is shown and open right below: it has every chip with
            /// the same actions (a tag's page, ×, "+"), so the header does not repeat them.
            readonly property bool panelOpen: live && header.editor !== null && header.editor.propertiesExpanded
                                              && AppController.routeInt(header.controller.pageProperties, "visibleRow", -1) >= 0
            visible: (aliases.length + tags.length > 0 || adding !== "") && !panelOpen
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            Layout.rightMargin: header.actionsWithChips && chipsActions.item ? chipsActions.width + Theme.space2 : 0
            spacing: Theme.space1
            /// The height of the first line (the page actions are centred on it).
            readonly property real firstLineHeight: {
                let tallest = 0
                for (let i = 0; i < chips.children.length; ++i) {
                    const c = chips.children[i]
                    if (c.visible && c.width > 0 && c.y < 1)
                        tallest = Math.max(tallest, c.height)
                }
                return tallest
            }

            Repeater {
                model: chips.aliases
                delegate: BChip {
                    id: aliasChip
                    required property var modelData
                    readonly property string name: AppController.routeText(modelData, "name")
                    readonly property bool editable: chips.live && AppController.routeFlag(aliasChip.modelData, "editable")
                    objectName: "headerAlias_" + name
                    text: aliasChip.name
                    iconName: "bookmark"
                    interactive: editable
                    Accessible.name: qsTr("Alias: %1").arg(aliasChip.name)
                    onClicked: header.openChipMenu("aliases", aliasChip.name, aliasChip)
                    onPressAndHold: header.openChipMenu("aliases", aliasChip.name, aliasChip)
                    TapHandler {
                        enabled: aliasChip.editable
                        acceptedButtons: Qt.RightButton
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        onTapped: header.openChipMenu("aliases", aliasChip.name, aliasChip)
                    }
                }
            }
            Repeater {
                model: chips.tags
                delegate: BChip {
                    id: tagChip
                    required property var modelData
                    readonly property string name: AppController.routeText(modelData, "name")
                    /// The tag's key (the index's rows; else the name is the tag).
                    readonly property string key: AppController.routeText(modelData, "key")
                    readonly property bool editable: chips.live && AppController.routeFlag(tagChip.modelData, "editable")
                    objectName: "headerTag_" + name
                    variant: "tag"
                    text: "#" + tagChip.name
                    interactive: true
                    Accessible.name: qsTr("Tag %1").arg(tagChip.name)
                    onClicked: AppController.openTag(tagChip.key !== "" ? tagChip.key : tagChip.name)
                    onPressAndHold: if (tagChip.editable) header.openChipMenu("tags", tagChip.name, tagChip)
                    TapHandler {
                        enabled: tagChip.editable
                        acceptedButtons: Qt.RightButton
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        onTapped: header.openChipMenu("tags", tagChip.name, tagChip)
                    }
                }
            }
            BIconButton {
                id: chipsAdd
                objectName: "pageHeaderAdd"
                visible: chips.live && chips.adding === "" && AppController.routeFlag(header.controller.pageProperties, "canAdd")
                         && !header.controller.sourceMode && !header.readOnly
                size: "small"
                iconName: "plus"
                label: qsTr("Add a tag or alias…")
                onClicked: header.openAddMenu(chipsAdd)
            }
            Loader {
                id: chipsField
                active: chips.adding !== ""
                visible: active
                width: active ? Math.min(240, chips.width) : 0
                sourceComponent: BSuggestField {
                    id: chipField
                    objectName: "pageHeaderField"
                    size: "small"
                    placeholderText: chips.adding === "tags" ? qsTr("Tag") : qsTr("Alias")
                    errorText: chips.addError
                    inputMethodHints: Qt.ImhNoAutoUppercase
                    // Tags: the values the notebook already uses (PageTools.tagSuggestions).
                    suggesting: chips.adding === "tags"
                    onSuggestionsRequested: (typed) => {
                        chipField.suggestions = summaryTools.tagSuggestions(
                                    header.controller.suggestPropertyValues("tags", typed, 8))
                    }
                    property bool done: false
                    function add(value: string): bool {
                        if (Js.trim(value) === "")
                            return true
                        header._held = header.controller.addPageListItem(chips.adding, value)
                        const ok = AppController.routeFlag(header._held, "ok")
                        chips.addError = ok ? "" : AppController.routeText(header._held, "reason")
                        if (ok)
                            chipField.text = ""
                        return ok
                    }
                    Component.onCompleted: chipField.forceActiveFocus()
                    onTypedTextChanged: chips.addError = ""
                    onPicked: (value) => chipField.add(value)
                    onCommitted: (value) => {
                        if (Js.trim(value) === "")
                            chips.adding = ""
                        else
                            chipField.add(value)
                    }
                    onCancelled: {
                        chipField.done = true
                        chips.adding = ""
                        chips.addError = ""
                    }
                    /// Leaving the field (a tap elsewhere, another page shown): what it holds is
                    /// added and it closes; what cannot be added is said in a toast.
                    function leave(): void {
                        if (chipField.done)
                            return
                        chipField.done = true
                        if (chipField.preeditText !== "")
                            EditorInput.commitPreedit(chipField)
                        if (!chipField.add(chipField.text))
                            AppController.toast(chips.addError, "error")
                        chips.adding = ""
                        chips.addError = ""
                    }
                    onActiveFocusChanged: if (!chipField.activeFocus) chipField.leave()
                    Connections {
                        target: header.controller
                        function onPathAboutToChange() { chipField.leave() }
                    }
                }
            }
        }

        // ---- The tag that names this page -----------------------------------------------------------
        // Its sub-tags (#work/reports under Work), each a page of its own (Tag overview is the "#"
        // among the page actions, Tasks with this tag in the page menu).
        Flow {
            objectName: "pageSubTags"
            visible: header.tagInUse && header.subTags.length > 0
            Layout.fillWidth: true
            spacing: Theme.space1
            Repeater {
                model: header.subTags
                delegate: BChip {
                    id: subTagChip
                    required property var modelData
                    readonly property string tagDisplay: AppController.routeText(modelData, "display")
                    variant: "tag"
                    interactive: true
                    text: "#" + subTagChip.tagDisplay + " · " + AppController.routeText(subTagChip.modelData, "count")
                    Accessible.name: qsTr("Tag %1").arg(subTagChip.tagDisplay)
                    onClicked: AppController.openTag(AppController.routeText(subTagChip.modelData, "tag"))
                }
            }
        }

        // An empty page (e.g. today's journal without a template): say where to type.
        BButton {
            visible: header.loaded && header.controller.blocks.visibleCount === 0 && !header.readOnly
            Layout.topMargin: Theme.space2
            Layout.leftMargin: -Theme.space3
            variant: "ghost"
            iconName: "square-pen"
            text: qsTr("Start writing")
            onClicked: header.editor.startEditing(header.controller.appendEmptyBlock(), -1)
        }
    }

    // The page actions at the end of the chips' first line (actionsWithChips): beside the column,
    // not in it, so the chips keep their place in the column (one pageActions is active at a time:
    // it sets menuAnchor).
    Loader {
        id: chipsActions
        active: header.actionsWithChips
        visible: active
        x: column.x + column.width - width
        y: column.y + chips.y + Math.max(0, (chips.firstLineHeight - height) / 2)
        sourceComponent: pageActions
    }
}
