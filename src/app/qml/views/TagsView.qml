pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// TagsView — routes "tags" (every tag with how often it is used: TagListModel; filter, most used
// or A–Z with nested tags under their parent) and "tag" ({tag}: the tag overview — the pages that
// carry it and every tagged block, grouped by page and rendered like in the editor
// (TagBlocksModel), its parent and sub-tags, "Otwórz stronę" / "Utwórz stronę" and "Zadania z tym
// tagiem" for TasksView).
// A tag names a page (docs/SYNTAX.md §3): a row of the list opens the tag's page
// (AppController.openTag), its trailing button the overview; inside the overview the parent and
// sub-tag chips stay in the overview ({view: "tag", overview: true}).
//
// Keys: typing filters the tags; Down moves into the list; Enter opens the page.
// Contract with the shell (qml/shell/ShellRouteView.qml): `route`, optional `title`.
FocusScope {
    id: root

    property var route: ({})
    readonly property string tag: route && route.view === "tag" && route.tag ? String(route.tag) : ""
    readonly property int maximumWidth: 860
    readonly property real sideMargin: Math.max(Theme.compact ? Theme.space3 : Theme.space6, (width - maximumWidth) / 2)

    readonly property var rendererPalette: ({
        link: Theme.link, unresolved: Theme.linkMissing, muted: Theme.fgMuted,
        codeBackground: Theme.bgSunken, highlightBackground: Theme.mark,
        tagFg: Theme.tagFg, tagBg: Theme.tagBg,
        dueFg: Theme.chipDueFg, dueBg: Theme.chipDueBg,
        overdueFg: Theme.chipOverdueFg, overdueBg: Theme.chipOverdueBg,
        scheduledFg: Theme.chipScheduledFg, scheduledBg: Theme.chipScheduledBg,
        reminderFg: Theme.chipReminderFg, reminderBg: Theme.chipReminderBg,
        alarmFg: Theme.chipAlarmFg, alarmBg: Theme.chipAlarmBg,
        recurrenceFg: Theme.chipRecurrenceFg, recurrenceBg: Theme.chipRecurrenceBg,
        doneFg: Theme.chipDoneFg, doneBg: Theme.chipDoneBg,
        neutralFg: Theme.chipNeutralFg, neutralBg: Theme.chipNeutralBg
    })

    /// `tag:` query of a tag key (quoted when it has a space, a comma or a quote).
    function tagQuery(key) {
        return "tag:" + (/[\s,"]/.test(key) ? "\"" + key + "\"" : key)
    }
    function safe(s) {
        return s && s.indexOf("<") >= 0 ? TextHelper.escapeHtml(s) : (s || "")
    }

    focus: true

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    Loader {
        anchors.fill: parent
        focus: true
        sourceComponent: root.tag === "" ? allTags : oneTag
    }

    // ---- Every tag -----------------------------------------------------------------------------------
    Component {
        id: allTags

        FocusScope {
            id: all
            focus: true
            Component.onCompleted: {
                tags.sortKey = String(AppSettings.value("tags/sort", "count"))
                // Not on touch screens: the on-screen keyboard would cover the tags.
                if (!Theme.touchUi)
                    filterField.forceActiveFocus()
            }

            TagListModel {
                id: tags
                workspace: Workspace
                filter: filterField.text
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: Theme.compact ? Theme.space3 : Theme.space5
                spacing: Theme.space2

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: root.sideMargin
                    Layout.rightMargin: root.sideMargin
                    spacing: Theme.space2
                    BSearchField {
                        id: filterField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Filter tags…")
                        debounce: 100
                        Keys.onDownPressed: {
                            tagList.currentIndex = 0
                            tagList.forceActiveFocus()
                        }
                        onAccepted: if (tags.count > 0) AppController.openTag(tags.get(0).tag)
                    }
                    BSegmented {
                        model: [{ value: "count", text: qsTr("Most used"), icon: "list" },
                                { value: "name", text: qsTr("A–Z"), icon: "tags" }]
                        iconOnly: Theme.compact
                        currentValue: tags.sortKey
                        accessibleName: qsTr("Sort tags")
                        onActivated: (value) => {
                            tags.sortKey = value
                            AppSettings.setValue("tags/sort", value)
                        }
                    }
                }
                BText {
                    Layout.leftMargin: root.sideMargin + 2
                    text: filterField.text.trim() !== "" ? qsTr("%1 of %n tag(s)", "", tags.totalCount).arg(tags.count)
                                                         : qsTr("%n tag(s)", "", tags.totalCount)
                    variant: "caption"
                    color: Theme.fgFaint
                }

                ListView {
                    id: tagList
                    TouchFlick.enabled: true
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: tags
                    currentIndex: -1
                    keyNavigationEnabled: true
                    reuseItems: true
                    boundsBehavior: Flickable.StopAtBounds
                    bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space6
                    T.ScrollBar.vertical: BScrollBar {}
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Tags")
                    Keys.onReturnPressed: if (currentIndex >= 0) AppController.openTag(tags.get(currentIndex).tag)
                    Keys.onEnterPressed: if (currentIndex >= 0) AppController.openTag(tags.get(currentIndex).tag)
                    Keys.onUpPressed: (event) => {
                        if (currentIndex <= 0)
                            filterField.forceActiveFocus()
                        else
                            event.accepted = false
                    }

                    delegate: BListItem {
                        id: item
                        required property int index
                        required property var model
                        readonly property bool nested: tags.sortKey === "name" && item.model.depth > 0 && filterField.text.trim() === ""
                        // The count and the way to the tag overview (a click opens the tag's page).
                        trailing: Row {
                            spacing: Theme.space1
                            BBadge {
                                anchors.verticalCenter: parent.verticalCenter
                                count: item.model.count
                                variant: item.selected ? "accent" : "neutral"
                            }
                            BIconButton {
                                objectName: "tagOverviewButton"
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "tags"
                                size: "small"
                                label: qsTr("Tag overview")
                                Accessible.description: qsTr("Everything tagged #%1").arg(item.model.display)
                                onClicked: AppController.navigate({ view: "tag", tag: item.model.tag, overview: true })
                            }
                        }
                        // Full width (ListView places delegates at x 0); the row itself is inset.
                        width: tagList.width
                        leftInset: root.sideMargin - Theme.space2
                        rightInset: root.sideMargin - Theme.space2
                        leftPadding: root.sideMargin + (nested ? item.model.depth * (Theme.touchUi ? 22 : 20) : 0)
                        rightPadding: root.sideMargin
                        iconName: "hash"
                        iconColor: Theme.tagFg
                        text: nested ? item.model.leaf : item.model.display
                        titleMarkup: item.model.matches.length > 0
                                     ? TextHelper.markMatches(item.model.display, item.model.matches, String(Theme.accentText)) : ""
                        // Where a click goes: the page the tag names, when it is not just the tag's name.
                        subtitle: item.model.pageTitle !== "" && item.model.pageTitle.toLowerCase() !== item.model.leaf.toLowerCase()
                                  ? qsTr("%n note(s)", "", item.model.pageCount) + " · → " + item.model.pageTitle
                                  : qsTr("%n note(s)", "", item.model.pageCount)
                        selected: tagList.currentIndex === item.index && tagList.activeFocus
                        onClicked: AppController.openTag(item.model.tag)
                    }

                    BEmptyState {
                        parent: tagList
                        anchors.centerIn: parent
                        width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                        visible: tags.count === 0 && Workspace.ready
                        iconName: "hash"
                        title: filterField.text.trim() !== "" ? qsTr("No tag matches “%1”").arg(filterField.text.trim()) : qsTr("No tags yet")
                        description: qsTr("Tag blocks with #word (also #nested/tag or #[[two words]]); pages can have tags: in their properties.")
                    }
                }
            }
        }
    }

    // ---- One tag ---------------------------------------------------------------------------------------
    Component {
        id: oneTag

        FocusScope {
            id: one
            focus: true

            TagBlocksModel {
                id: tagged
                workspace: Workspace
                tag: root.tag
                palette: root.rendererPalette
                locale: DatesHelper.locale
                today: DatesHelper.today
            }
            PageTools { id: tools; workspace: Workspace }

            ListView {
                id: list
                TouchFlick.enabled: true
                anchors.fill: parent
                clip: true
                model: tagged
                focus: true
                reuseItems: false
                boundsBehavior: Flickable.StopAtBounds
                bottomMargin: Theme.narrow ? Theme.space16 + Theme.space8 : Theme.space8
                T.ScrollBar.vertical: BScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Blocks tagged #%1").arg(tagged.display)

                header: Item {
                    width: list.width
                    implicitHeight: head.y + head.implicitHeight + Theme.space5

                    ColumnLayout {
                        id: head
                        x: root.sideMargin
                        y: Theme.compact ? Theme.space4 : Theme.space6
                        width: list.width - 2 * root.sideMargin
                        spacing: Theme.space2

                        BButton {
                            visible: tagged.parentTag !== ""
                            Layout.leftMargin: -Theme.space3
                            variant: "ghost"
                            size: "small"
                            iconName: "arrow-up"
                            text: "#" + root.safe(tagged.parentTag)
                            Accessible.name: qsTr("Parent tag %1").arg(tagged.parentTag)
                            onClicked: AppController.navigate({ view: "tag", tag: tagged.parentTag, overview: true })
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "#" + tagged.display
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            font.family: Theme.fontEditor
                            font.pixelSize: Theme.compact ? Theme.text2xl : Theme.text3xl
                            font.weight: Theme.weightSemiBold
                            color: Theme.tagFg
                            Accessible.role: Accessible.Heading
                            Accessible.name: text
                        }
                        BText {
                            Layout.fillWidth: true
                            visible: tagged.pageCount > 0
                            text: tagged.refCount > 0 ? qsTr("%n block(s)", "", tagged.refCount) + " · " + qsTr("in %n note(s)", "", tagged.pageCount)
                                                    : qsTr("in %n note(s)", "", tagged.pageCount)
                            variant: "muted"
                        }
                        Flow {
                            visible: tagged.subTags.length > 0
                            Layout.fillWidth: true
                            spacing: Theme.space1
                            Repeater {
                                model: tagged.subTags
                                delegate: BChip {
                                    required property var modelData
                                    variant: "tag"
                                    interactive: true
                                    text: "#" + root.safe(modelData.display) + " · " + modelData.count
                                    Accessible.name: qsTr("Tag %1").arg(modelData.display)
                                    onClicked: AppController.navigate({ view: "tag", tag: modelData.tag, overview: true })
                                }
                            }
                        }
                        Flow {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.space1
                            spacing: Theme.space2
                            BButton {
                                // The page the tag names (docs/SYNTAX.md §3), written by its first
                                // content when it does not exist yet.
                                objectName: "tagPageButton"
                                // (`revision` read for real: a bare `void` read is compiled away.)
                                readonly property bool pageExists: Workspace.revision >= 0
                                                                   && Workspace.exists(Workspace.tagPage(root.tag))
                                size: "small"
                                iconName: pageExists ? "file-text" : "plus"
                                text: pageExists ? qsTr("Open page") : qsTr("Create page")
                                onClicked: AppController.openTag(root.tag)
                            }
                            BButton {
                                size: "small"
                                iconName: "list-todo"
                                text: qsTr("Tasks with this tag")
                                onClicked: AppController.navigate({ view: "tasks", query: root.tagQuery(root.tag) })
                            }
                            BButton {
                                size: "small"
                                variant: "ghost"
                                iconName: "tags"
                                text: qsTr("All tags")
                                onClicked: AppController.navigate({ view: "tags" })
                            }
                        }
                    }
                }

                delegate: Item {
                    id: entry
                    required property int index
                    required property var model
                    width: list.width
                    implicitHeight: entry.model.rowType === "page" ? groupHeader.implicitHeight + (entry.index > 0 ? Theme.space3 : 0)
                                                                   : refRow.implicitHeight
                    height: implicitHeight

                    ContentRefGroupHeader {
                        id: groupHeader
                        visible: entry.model.rowType === "page"
                        anchors.bottom: parent.bottom
                        x: root.sideMargin
                        width: parent.width - 2 * root.sideMargin
                        refs: tagged
                        path: entry.model.path
                        title: entry.model.title
                        folder: entry.model.folder
                        journalDate: entry.model.journalDate
                        count: entry.model.count
                        collapsed: entry.model.collapsed
                        // A page tagged as a whole (its properties) and not in any block.
                        pageLevel: entry.model.pageLevel && entry.model.count === 0
                        compact: Theme.compact
                    }
                    ContentRefRow {
                        id: refRow
                        visible: entry.model.rowType === "ref"
                        x: root.sideMargin + (Theme.compact ? Theme.space2 : Theme.space6)
                        width: parent.width - x - root.sideMargin
                        tools: tools
                        html: entry.model.html
                        plain: entry.model.text
                        breadcrumb: entry.model.breadcrumb
                        taskStatus: entry.model.taskStatus
                        sourcePath: entry.model.path
                        line: entry.model.line
                        onOpened: (newPane) => AppController.openPage(entry.model.path, entry.model.line, newPane)
                        onTaskToggled: tagged.toggleTask(entry.index)
                        onTaskStatusRequested: (status) => tagged.setTaskStatus(entry.index, status)
                    }
                }

                BEmptyState {
                    parent: list
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: (list.headerItem ? list.headerItem.height : 0) + Theme.space8
                    width: Math.min(parent.width - 2 * Theme.space6, implicitWidth)
                    visible: tagged.count === 0 && Workspace.ready
                    iconName: "hash"
                    title: qsTr("Nothing is tagged #%1").arg(root.tag)
                    description: qsTr("Write #%1 in a block to collect it here.").arg(root.tag)
                    actionText: qsTr("All tags")
                    onActionTriggered: AppController.navigate({ view: "tags" })
                }
            }
        }
    }
}
