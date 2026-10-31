pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentPageHeader — the top of a page in PageView (the block editor's header, it scrolls with
// the blocks): the title — editable in place (a click; Enter renames through PageView, Escape
// cancels) — with the favourite star and the page menu, previous / next day arrows for daily
// notes, the folder breadcrumb in the side panel (the shell's top bar shows it otherwise), and
// the page's aliases and tags as chips — from the page's properties, editable there: a chip's menu
// (a right-click, a long press) removes one, "+" adds a tag or an alias (the editor shows all
// properties in its "Właściwości" panel, docs/UI.md "Page properties"). Next to the star: the note mode toggle (document / outline; its icon
// shows the current mode, a click asks for the other one: modeToggleRequested). An empty page
// gets a "Start writing" button; a page that does not exist yet says that typing creates it.
// A page named by a tag that is in use (docs/SYNTAX.md §3: #tag is a link to this page) gets a
// strip with "Widok tagu" (the tag overview), "Zadania z tym tagiem" and its sub-tags as chips
// (#praca/raporty under Praca), each opening its own page.
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
    /// The first block repeats the title as "# Title": no second title above it.
    property bool startsWithTitle: false
    property bool inSidePanel: false
    /// The title field is open.
    property bool editingTitle: false
    /// An error for the title being typed ("" = fine).
    property string titleError

    signal renameRequested(string title)
    signal menuRequested(Item anchor)
    /// The mode toggle was clicked (PageView switches: PageController.toggleMode).
    signal modeToggleRequested()
    /// The page menu's button (route.open "menu" opens the menu there).
    property Item menuAnchor: null

    readonly property bool small: Theme.compact || inSidePanel
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
    readonly property var summary: {
        const changes = Workspace.revision + (Workspace.indexing ? 1 : 0)
        return path !== "" && changes >= 0 ? tools.summary(path) : ({})
    }
    readonly property var folderParts: summary.folderParts || []
    readonly property bool favorite: Workspace.favorites.some(f => f.path === header.path)
    readonly property bool showTitle: !startsWithTitle || journal
    readonly property real textX: editor ? editor.contentX + editor.gutter : 0
    readonly property real textWidth: editor ? editor.contentWidth - editor.gutter : width

    implicitHeight: column.y + column.implicitHeight + (small ? Theme.space3 : Theme.space5)

    function startTitleEdit() {
        if (journal || !exists)
            return
        titleError = ""
        titleField.text = controller.title
        editingTitle = true
        titleField.forceActiveFocus()
        titleField.selectAll()
    }
    function commitTitle() {
        const title = titleField.text.trim()
        if (title === controller.title || title === "") {
            editingTitle = false
            return
        }
        const problem = tools.validateTitle(path, title)
        if (problem !== "") {
            titleError = problem
            return
        }
        editingTitle = false
        header.renameRequested(title)
    }
    function cancelTitle() {
        editingTitle = false
        titleError = ""
        if (editor)
            editor.forceActiveFocus()
    }
    // ---- Tags and aliases of the page's properties (docs/UI.md "Page properties") -------------
    property var chipMenu: null
    property var addMenu: null
    /// The menu of the tag or alias `name` (`kind` "tags" | "aliases") at `item`.
    function openChipMenu(kind, name, item) {
        if (!controller || !controller.loaded)
            return
        if (!chipMenu)
            chipMenu = chipMenuComponent.createObject(header)
        chipMenu.kind = kind
        chipMenu.name = name
        chipMenu.popup(item, 0, item.height)
    }
    function openAddMenu(item) {
        if (!addMenu)
            addMenu = addMenuComponent.createObject(header)
        addMenu.popup(item, 0, item.height)
    }
    /// Take `name` out of the page's tags / aliases (a toast with "Cofnij").
    function removeListItem(kind, name) {
        const before = controller.undoToken()
        const r = controller.removePageListItem(kind, name)
        if (!r.ok) {
            AppController.toast(r.reason, "error")
            return
        }
        const text = kind === "tags" ? qsTr("Removed tag #%1 from this page").arg(name) : qsTr("Removed alias “%1”").arg(name)
        const action = editor ? editor.undoAction(before) : null
        if (action)
            AppController.toastAction(text, "info", action.text, action.run)
        else
            AppController.toast(text, "info")
    }
    Component {
        id: chipMenuComponent
        BMenu {
            id: chipMenuItem
            property string kind
            property string name
            BMenuItem {
                objectName: "headerTagOpen"
                visible: chipMenuItem.kind === "tags"
                height: visible ? implicitHeight : 0
                text: qsTr("Open tag page")
                iconName: "hash"
                onTriggered: AppController.openTag(chipMenuItem.name)
            }
            BMenuItem {
                objectName: "headerAliasCopy"
                visible: chipMenuItem.kind === "aliases"
                height: visible ? implicitHeight : 0
                text: qsTr("Copy")
                iconName: "copy"
                onTriggered: {
                    if (header.controller.copyText(chipMenuItem.name).ok)
                        AppController.toast(qsTr("Copied %1").arg(chipMenuItem.name), "success")
                }
            }
            BMenuItem {
                objectName: "headerChipRemove"
                text: chipMenuItem.kind === "tags" ? qsTr("Remove from this page") : qsTr("Remove alias")
                iconName: "x"
                danger: true
                onTriggered: header.removeListItem(chipMenuItem.kind, chipMenuItem.name)
            }
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

    /// `tag:` query of a tag key (quoted when it has a space, a comma or a quote).
    function tagQuery(key) {
        return "tag:" + (/[\s,"]/.test(key) ? "\"" + key + "\"" : key)
    }
    function openDay(offset) {
        AppController.navigate({ view: "journal", date: DatesHelper.addDays(header.journalDate, offset) })
    }

    Component {
        id: pageActions
        Row {
            spacing: 2
            BIconButton {
                objectName: "noteModeToggle"
                visible: header.loaded
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

        // ---- Day navigation / folder (side panel) · favourite · menu ---------------------------------
        // Pages show their folders in the shell's top bar; the side panel has none. The star and
        // the menu go here when there is no title row to carry them (daily notes, a page that
        // starts with its own "# Title").
        RowLayout {
            visible: header.journal || !header.showTitle || (header.inSidePanel && header.folderParts.length > 0)
            Layout.fillWidth: true
            spacing: 2

            BBreadcrumb {
                visible: !header.journal && header.inSidePanel && header.folderParts.length > 0
                Layout.fillWidth: true
                rootIcon: "folder"
                model: header.folderParts.map(p => ({ title: p.title }))
                onActivated: (index) => AppController.navigate({ view: "allPages", path: header.folderParts[index].path })
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
                visible: header.journal || !(header.inSidePanel && header.folderParts.length > 0)
                Layout.fillWidth: true
            }
            Loader {
                active: header.journal || !header.showTitle
                visible: active
                sourceComponent: pageActions
            }
        }

        // ---- Title · favourite · menu -------------------------------------------------------------------
        RowLayout {
            visible: header.showTitle || header.editingTitle
            Layout.fillWidth: true
            spacing: Theme.space2

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
                    Accessible.description: !header.journal && header.exists ? qsTr("Click to rename") : ""

                    // The tap area (rename): a touch target tall on touch UIs, centred on the title.
                    Item {
                        width: parent.width
                        height: Math.max(parent.height, Theme.touchUi ? Theme.touchTarget : 0)
                        y: (parent.height - height) / 2
                        HoverHandler {
                            id: titleHover
                            enabled: !Theme.touchUi && !header.journal && header.exists
                            cursorShape: Qt.IBeamCursor
                        }
                        TapHandler {
                            enabled: !header.journal && header.exists
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
        // long press; an alias's tap too): a tag's page or "Usuń z tej strony", an alias copied or
        // removed. "+" at the end of the row adds a tag or an alias in a field there (or opens the
        // properties panel's add row).
        Flow {
            id: chips
            objectName: "pageHeaderChips"
            readonly property bool live: header.loaded && header.controller !== null
            readonly property var aliases: live ? (header.controller.pageProperties.aliases || [])
                                                : (header.summary.aliases || []).map(a => ({ name: a }))
            readonly property var tags: live ? (header.controller.pageProperties.tags || [])
                                             : (header.summary.tags || []).map(t => ({ name: t.display, key: t.tag }))
            /// The field adding "tags" | "aliases" is open ("": none).
            property string adding: ""
            property string addError: ""
            visible: aliases.length + tags.length > 0 || adding !== ""
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            spacing: Theme.space1

            Repeater {
                model: chips.aliases
                delegate: BChip {
                    id: aliasChip
                    required property var modelData
                    readonly property bool editable: chips.live && aliasChip.modelData.editable === true
                    objectName: "headerAlias_" + modelData.name
                    text: header.editor ? header.editor.autoTextSafe(modelData.name) : modelData.name
                    iconName: "bookmark"
                    interactive: editable
                    Accessible.name: qsTr("Alias: %1").arg(modelData.name)
                    onClicked: header.openChipMenu("aliases", aliasChip.modelData.name, aliasChip)
                    onPressAndHold: header.openChipMenu("aliases", aliasChip.modelData.name, aliasChip)
                    TapHandler {
                        enabled: aliasChip.editable
                        acceptedButtons: Qt.RightButton
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        onTapped: header.openChipMenu("aliases", aliasChip.modelData.name, aliasChip)
                    }
                }
            }
            Repeater {
                model: chips.tags
                delegate: BChip {
                    id: tagChip
                    required property var modelData
                    readonly property bool editable: chips.live && tagChip.modelData.editable === true
                    objectName: "headerTag_" + modelData.name
                    variant: "tag"
                    text: "#" + (header.editor ? header.editor.autoTextSafe(modelData.name) : modelData.name)
                    interactive: true
                    Accessible.name: qsTr("Tag %1").arg(modelData.name)
                    onClicked: AppController.openTag(tagChip.modelData.key || tagChip.modelData.name)
                    onPressAndHold: if (tagChip.editable) header.openChipMenu("tags", tagChip.modelData.name, tagChip)
                    TapHandler {
                        enabled: tagChip.editable
                        acceptedButtons: Qt.RightButton
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
                        onTapped: header.openChipMenu("tags", tagChip.modelData.name, tagChip)
                    }
                }
            }
            BIconButton {
                id: chipsAdd
                objectName: "pageHeaderAdd"
                visible: chips.live && chips.adding === "" && header.controller.pageProperties.canAdd === true
                         && !header.controller.sourceMode
                size: "small"
                iconName: "plus"
                label: qsTr("Add a tag or alias…")
                onClicked: header.openAddMenu(chipsAdd)
            }
            Loader {
                id: chipsField
                active: chips.adding !== ""
                visible: active
                width: active ? Math.min(240, header.textWidth) : 0
                sourceComponent: BSuggestField {
                    id: chipField
                    objectName: "pageHeaderField"
                    size: "small"
                    placeholderText: chips.adding === "tags" ? qsTr("Tag") : qsTr("Alias")
                    errorText: chips.addError
                    inputMethodHints: Qt.ImhNoAutoUppercase
                    provider: chips.adding !== "tags" ? null : (typed) => header.controller.suggestPropertyValues("tags", typed, 8)
                        .map(v => ({ title: "#" + v.value, value: v.value, subtitle: v.count > 0 ? String(v.count) : "" }))
                    property bool done: false
                    function add(text) {
                        if (text.trim() === "")
                            return true
                        const r = header.controller.addPageListItem(chips.adding, text)
                        chips.addError = r.ok ? "" : r.reason
                        if (r.ok)
                            chipField.text = ""
                        return r.ok
                    }
                    Component.onCompleted: forceActiveFocus()
                    onTypedTextChanged: chips.addError = ""
                    onPicked: (value) => add(String(value))
                    onCommitted: (text) => {
                        if (text.trim() === "")
                            chips.adding = ""
                        else
                            add(text)
                    }
                    onCancelled: {
                        done = true
                        chips.adding = ""
                        chips.addError = ""
                    }
                    /// Leaving the field (a tap elsewhere, another page shown): what it holds is
                    /// added and it closes; what cannot be added is said in a toast.
                    function leave() {
                        if (done)
                            return
                        done = true
                        if (preeditText !== "")
                            EditorInput.commitPreedit(chipField)
                        if (!add(text))
                            AppController.toast(chips.addError, "error")
                        chips.adding = ""
                        chips.addError = ""
                    }
                    onActiveFocusChanged: if (!activeFocus) leave()
                    Connections {
                        target: header.controller
                        function onPathAboutToChange() { chipField.leave() }
                    }
                }
            }
        }

        // ---- The tag that names this page -----------------------------------------------------------
        Flow {
            id: tagStrip
            readonly property var tag: header.summary.tag || ({})
            readonly property var subTags: tagStrip.tag.subTags || []
            objectName: "pageTagStrip"
            visible: (tagStrip.tag.key || "") !== "" && ((tagStrip.tag.references || 0) > 0 || tagStrip.subTags.length > 0)
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            Layout.leftMargin: -Theme.space2
            spacing: Theme.space1

            BButton {
                objectName: "pageTagOverview"
                size: "small"
                variant: "ghost"
                iconName: "hash"
                text: qsTr("Tag overview")
                Accessible.description: qsTr("Everything tagged %1").arg(tagStrip.tag.text || "")
                onClicked: AppController.navigate({ view: "tag", tag: tagStrip.tag.key, overview: true })
            }
            BButton {
                size: "small"
                variant: "ghost"
                iconName: "list-todo"
                text: qsTr("Tasks with this tag")
                onClicked: AppController.navigate({ view: "tasks", query: header.tagQuery(tagStrip.tag.key) })
            }
        }
        Flow {
            // The sub-tags (#praca/raporty under Praca), each a page of its own.
            visible: tagStrip.visible && tagStrip.subTags.length > 0
            Layout.fillWidth: true
            spacing: Theme.space1
            Repeater {
                model: tagStrip.subTags
                delegate: BChip {
                    required property var modelData
                    variant: "tag"
                    interactive: true
                    text: "#" + (header.editor ? header.editor.autoTextSafe(modelData.display) : modelData.display)
                          + " · " + modelData.count
                    Accessible.name: qsTr("Tag %1").arg(modelData.display)
                    onClicked: AppController.openTag(modelData.tag)
                }
            }
        }

        // An empty page (e.g. today's journal without a template): say where to type.
        BButton {
            visible: header.loaded && header.controller.blocks.visibleCount === 0
            Layout.topMargin: Theme.space2
            Layout.leftMargin: -Theme.space3
            variant: "ghost"
            iconName: "square-pen"
            text: qsTr("Start writing")
            onClicked: header.editor.startEditing(header.controller.appendEmptyBlock(), -1)
        }
    }
}
