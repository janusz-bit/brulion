pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ContentPageHeader — the top of a page in PageView (the block editor's header, it scrolls with
// the blocks): the title — editable in place (a click; Enter renames through PageView, Escape
// cancels) — with the favourite star and the page menu, previous / next day arrows for daily
// notes, the folder breadcrumb in the side panel (the shell's top bar shows it otherwise), and
// the page's aliases and tags as chips (the editor shows all properties in its collapsed
// "Właściwości" block). Next to the star: the note mode toggle (document / outline; its icon
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
    // Workspace.revision: the summary follows saves (frontmatter edits) and other devices.
    // Workspace.indexing: a page opened while the notebook is still being read (at start-up)
    // gets its tags and aliases once the index has it.
    readonly property var summary: {
        void Workspace.revision
        void Workspace.indexing
        return path !== "" ? tools.summary(path) : ({})
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

                    HoverHandler {
                        id: titleHover
                        enabled: !Theme.touchUi && !header.journal && header.exists
                        cursorShape: Qt.IBeamCursor
                    }
                    TapHandler {
                        enabled: !header.journal && header.exists
                        onTapped: header.startTitleEdit()
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
        Flow {
            readonly property var aliases: header.summary.aliases || []
            readonly property var tags: header.summary.tags || []
            id: chips
            visible: aliases.length + tags.length > 0
            Layout.fillWidth: true
            Layout.topMargin: Theme.space1
            spacing: Theme.space1

            Repeater {
                model: chips.aliases
                delegate: BChip {
                    required property string modelData
                    text: header.editor ? header.editor.autoTextSafe(modelData) : modelData
                    iconName: "bookmark"
                    Accessible.name: qsTr("Alias: %1").arg(modelData)
                }
            }
            Repeater {
                model: chips.tags
                delegate: BChip {
                    required property var modelData
                    variant: "tag"
                    text: "#" + (header.editor ? header.editor.autoTextSafe(modelData.display) : modelData.display)
                    interactive: true
                    Accessible.name: qsTr("Tag %1").arg(modelData.display)
                    onClicked: AppController.openTag(modelData.tag)
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
