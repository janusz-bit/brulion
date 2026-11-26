pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CodeProjectSection — a project in CodeSidebar with its threads (docs/UI.md "Code mode"): its row
// (fold, its icon — an emoji of the user's, t3.json's iconPath picture, else a folder —, its name, a
// pin, "+" a new thread, "⋯" its menu; dragged with the mouse to another place in the list), a draft
// of a new thread when one was written and not sent, its threads (CodeThreadRow; ChatListModel with
// the sidebar's filter and order), "No threads yet". The sidebar owns the menus and dialogs: the
// section asks for them by its signals.
ColumnLayout {
    id: section

    required property var modelData
    required property int index
    readonly property string projectId: AppController.routeText(section.modelData, "id")
    readonly property string projectName: AppController.routeText(section.modelData, "name")
    readonly property string projectPath: AppController.routeText(section.modelData, "path")
    readonly property string projectIcon: AppController.routeText(section.modelData, "icon")
    readonly property string projectIconUrl: AppController.routeText(section.modelData, "iconUrl")
    readonly property bool projectPinned: AppController.routeFlag(section.modelData, "pinned")
    /// Shown unfolded; the sidebar's filter words and order.
    property bool open: true
    property string filter
    property string sortBy: "activity"
    property string routePath
    /// A new thread of this project is on screen.
    property bool newShown: false
    /// Being dragged (drawn faded).
    property bool dragging: false
    /// The draft of a new thread of it (ChatView keeps it as "code:<id>").
    readonly property string draft: Workspace.chats.draftSerial >= 0 ? Workspace.chats.draft("code:" + section.projectId) : ""
    readonly property int threadCount: threads.count
    readonly property real rowHeight: projectRow.height

    signal navigate(var route)
    signal toggleRequested()
    signal menuRequested(Item item, real x, real y)
    signal renameThread(string path, string title)
    signal deleteThread(string path, string title, string worktree, string branch, string project)
    signal copyRequested(string text, string what)
    signal dragStarted()
    signal dragMoved(real dy)
    signal dragEnded()

    /// The paths of its threads as listed (none while folded).
    function threadPathAt(row: int): string {
        return threads.pathAt(row)
    }
    function threadRow(path: string): int {
        return path === "" ? -1 : threads.rowOf(path)
    }
    function threadPaths(): list<string> {
        if (!section.open)
            return []
        return threads.paths()
    }

    Layout.fillWidth: true
    spacing: 0
    opacity: section.dragging ? 0.5 : 1

    ChatListModel {
        id: threads
        workspace: Workspace
        locale: DatesHelper.locale
        project: section.projectId
        filter: section.filter
        sortBy: section.sortBy
    }

    Item {
        id: projectRow
        objectName: "codeProject_" + section.projectId
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.listItemHeight
        Accessible.role: Accessible.Button
        Accessible.name: section.projectName

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: Theme.space1
            anchors.rightMargin: Theme.space1
            radius: Theme.radius
            color: projectHover.hovered ? Theme.bgHover : "transparent"
        }
        HoverHandler { id: projectHover; enabled: !Theme.touchUi }
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: section.toggleRequested()
            onLongPressed: section.menuRequested(projectRow, projectRow.width / 2, projectRow.height)
        }
        TapHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedButtons: Qt.RightButton
            onTapped: (point) => section.menuRequested(projectRow, point.position.x, point.position.y)
        }
        // A drag with the mouse moves the project (touch: the menu's Move up / Move down).
        DragHandler {
            id: drag
            objectName: "codeProjectDrag"
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            xAxis.enabled: false
            onActiveChanged: {
                if (drag.active)
                    section.dragStarted()
                else
                    section.dragEnded()
            }
            onActiveTranslationChanged: {
                if (drag.active)
                    section.dragMoved(drag.activeTranslation.y)
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space3
            anchors.rightMargin: Theme.space2
            spacing: Theme.space2
            BIcon {
                name: section.open ? "chevron-down" : "chevron-right"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
            Item {
                objectName: "codeProjectIcon"
                Layout.preferredWidth: Theme.iconSize
                Layout.preferredHeight: Theme.iconSize
                BText {
                    anchors.centerIn: parent
                    visible: section.projectIcon !== ""
                    text: section.projectIcon
                    font.pixelSize: Theme.iconSize - 2
                }
                Image {
                    id: picture
                    anchors.fill: parent
                    visible: section.projectIcon === "" && section.projectIconUrl !== "" && picture.status === Image.Ready
                    source: section.projectIcon === "" ? section.projectIconUrl : ""
                    sourceSize.width: Theme.iconSize * 2
                    sourceSize.height: Theme.iconSize * 2
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                }
                BIcon {
                    anchors.centerIn: parent
                    visible: section.projectIcon === "" && !picture.visible
                    name: "folder"
                    size: Theme.iconSize
                    color: Theme.fgMuted
                }
            }
            BText {
                Layout.fillWidth: true
                text: section.projectName
                font.weight: Theme.weightSemiBold
                elide: Text.ElideRight
            }
            BIcon {
                visible: section.projectPinned
                name: "pin"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
            BIconButton {
                objectName: "codeNewThread_" + section.projectId
                size: "small"
                iconName: "plus"
                label: qsTr("New thread in %1").arg(section.projectName)
                onClicked: section.navigate({ view: "code", new: true, project: section.projectId })
            }
            BIconButton {
                id: projectMore
                objectName: "codeProjectMore_" + section.projectId
                size: "small"
                iconName: "ellipsis"
                label: qsTr("More: %1").arg(section.projectName)
                onClicked: section.menuRequested(projectMore, 0, projectMore.height)
            }
        }
    }

    // A new thread written and not sent: its draft, opened again by a click.
    Item {
        objectName: "codeDraft_" + section.projectId
        visible: section.open && section.draft !== "" && !section.newShown
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.listItemHeight
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Draft: %1").arg(section.draft)
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: Theme.space1
            anchors.rightMargin: Theme.space1
            radius: Theme.radius
            color: draftHover.hovered ? Theme.bgHover : "transparent"
        }
        HoverHandler { id: draftHover; enabled: !Theme.touchUi }
        TapHandler { onTapped: section.navigate({ view: "code", new: true, project: section.projectId }) }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space8
            anchors.rightMargin: Theme.space3
            spacing: Theme.space2
            BIcon {
                name: "square-pen"
                size: Theme.iconSizeSm
                color: Theme.fgFaint
            }
            BText {
                Layout.fillWidth: true
                text: Js.trim(Js.replaceRegExp(section.draft, "\\s+", "g", " "))
                color: Theme.fgMuted
                font.italic: true
                elide: Text.ElideRight
            }
            BText {
                text: qsTr("Draft")
                variant: "small"
                color: Theme.fgFaint
            }
        }
    }

    Repeater {
        model: section.open ? threads : null
        delegate: CodeThreadRow {
            projectId: section.projectId
            projectPath: section.projectPath
            routePath: section.routePath
            onNavigate: (route) => section.navigate(route)
            onRenameRequested: (path, title) => section.renameThread(path, title)
            onDeleteRequested: (path, title, worktree, branch) => section.deleteThread(path, title, worktree, branch, section.projectId)
            onCopyRequested: (text, what) => section.copyRequested(text, what)
        }
    }
    BText {
        visible: section.open && threads.count === 0 && (section.draft === "" || section.newShown)
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space8 + Theme.space2
        Layout.bottomMargin: Theme.space1
        text: section.filter !== "" ? qsTr("No thread matches") : qsTr("No threads yet")
        variant: "small"
        color: Theme.fgFaint
    }
}
