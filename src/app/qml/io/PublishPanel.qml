pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// PublishPanel — the "Publish" tab of ImportExportView (PublishController; docs/UI.md
// "Publishing"): the notebook's publishing targets on this device (GitHub Pages, a WebDAV folder,
// Nextcloud with a public link, a folder on this computer) with how each last went and its address;
// for the chosen one what it publishes (pages, folders, tags, the whole notebook; the pages they
// link to, only pages marked publish: true, extension pages, conversations with AI), the content
// (web pages or one document), then "Preview" — the dry run: what goes up, what changes, what is
// removed, what is left out and why, conflicts — and "Publish". Files changed at the target since
// the last publish stop it until "Replace them"; "Unpublish…" removes what it published.
//
// `preset` (route parameters): { add: "page" | "folder" | "tag" | "notebook", path, folder, tag } —
// the page's menu ("Publish…"), the folder's ("Publish folder…"), a tag's page: the item is added
// to the chosen target (a new target asked for first when there is none) and previewed.
ColumnLayout {
    id: panel

    property var preset: ({})
    readonly property alias publisher: publisherObject
    readonly property list<var> contents: [
        { value: "site", text: qsTr("Web pages"), icon: Theme.compact ? "" : "globe" },
        { value: "document", text: qsTr("One document"), icon: Theme.compact ? "" : "file-text" }
    ]
    readonly property bool hasTarget: publisherObject.current !== ""
    property string tagQuery

    function applyPreset(): void {
        // The item the route names goes to the chosen target; without one a new target first.
        if (AppController.routeText(panel.preset, "add") === "")
            return
        const needsTarget = publisherObject.addFromRoute(panel.preset)
        // Once: going back here, or the app opening here again, adds nothing.
        AppController.replaceRoute({ view: "importExport", tab: "publish" })
        if (needsTarget)
            targetDialog.openFor("")
    }
    function tagRowsFor(shown: bool, query: string): list<var> {
        if (!shown)
            return []
        return publisherObject.tagRows(query)
    }
    function itemName(kind: string, label: string): string {
        if (kind === "page")
            return qsTr("Page “%1”").arg(label)
        if (kind === "folder")
            return qsTr("Folder “%1”").arg(label)
        if (kind === "tag")
            return qsTr("Tag %1").arg(label)
        return label
    }

    onPresetChanged: applyPreset()
    Component.onCompleted: applyPreset()

    spacing: Theme.space5

    /// Small muted explanatory text.
    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    PublishController {
        id: publisherObject
        objectName: "publishController"
        workspace: Workspace
        settings: AppSettings
        onFinished: (message, tone) => AppController.toast(message, tone)
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Put pages on the web with one click: GitHub Pages, a Nextcloud link, your own server or a folder. Only what you choose goes – pages marked publish: false never do, nor hidden folders or encrypted text.")
        variant: "muted"
        wrapMode: Text.Wrap
    }

    // ---- Targets -----------------------------------------------------------------------------
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BText {
            Layout.fillWidth: true
            text: qsTr("Where")
            variant: "title"
            Accessible.role: Accessible.Heading
        }
        BButton {
            objectName: "publishNewTarget"
            text: qsTr("New target…")
            iconName: "plus"
            size: "small"
            enabled: !publisherObject.busy
            onClicked: targetDialog.openFor("")
        }
    }
    BCard {
        Layout.fillWidth: true
        visible: publisherObject.targets.length === 0
        tone: "sunken"
        contentItem: ColumnLayout {
            spacing: Theme.space2
            BText {
                Layout.fillWidth: true
                text: qsTr("No targets yet")
                variant: "label"
            }
            Hint {
                text: qsTr("A target is where pages go: a GitHub repository (free GitHub Pages), a folder of your Nextcloud with a public link, a WebDAV folder of your own server, or a folder on this computer. It is this device's: other devices set up their own.")
            }
        }
    }
    GridLayout {
        Layout.fillWidth: true
        visible: publisherObject.targets.length > 0
        columns: !Theme.compact && width >= 640 ? 2 : 1
        columnSpacing: Theme.space2
        rowSpacing: Theme.space2
        Repeater {
            model: publisherObject.targets
            delegate: T.AbstractButton {
                id: tile
                required property var modelData
                required property string name
                required property string kindText
                required property string address
                required property string lastText
                required property bool lastOk
                readonly property string targetId: AppController.routeText(modelData, "id")
                readonly property string iconName: AppController.routeText(modelData, "icon")
                readonly property bool chosen: publisherObject.current === targetId
                objectName: "publishTarget_" + name
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: implicitContentHeight + topPadding + bottomPadding
                padding: Theme.space3
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                enabled: !publisherObject.busy || chosen
                Accessible.role: Accessible.RadioButton
                Accessible.name: tile.name
                Accessible.description: tile.kindText + ", " + tile.address
                Accessible.checked: chosen
                onClicked: publisherObject.current = tile.targetId
                background: Rectangle {
                    radius: Theme.radius
                    color: tile.chosen ? Theme.bgSelected : tile.down ? Theme.bgPressed : tile.hovered ? Theme.bgHover : Theme.bgElevated
                    border.width: tile.chosen ? 1.5 : 1
                    border.color: tile.chosen ? Theme.focusRing : Theme.border
                    BFocusRing { visible: tile.visualFocus; targetRadius: Theme.radius }
                }
                contentItem: RowLayout {
                    spacing: Theme.space3
                    BIcon {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        name: tile.iconName
                        size: Theme.iconSize
                        color: tile.chosen ? Theme.link : Theme.fgMuted
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        BText {
                            Layout.fillWidth: true
                            text: tile.name
                            variant: "label"
                            elide: Text.ElideRight
                        }
                        BText {
                            Layout.fillWidth: true
                            text: tile.kindText + " · " + tile.address
                            variant: "small"
                            color: Theme.fgMuted
                            elide: Text.ElideMiddle
                        }
                        BText {
                            Layout.fillWidth: true
                            text: tile.lastText
                            variant: "small"
                            color: tile.lastOk ? Theme.fgMuted : Theme.dangerText
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    // ---- The chosen target ---------------------------------------------------------------------
    IoCard {
        visible: panel.hasTarget
        iconName: AppController.routeText(publisherObject.currentTarget, "icon")
        title: AppController.routeText(publisherObject.currentTarget, "name")
        subtitle: AppController.routeText(publisherObject.currentTarget, "kindText") + " · "
                  + AppController.routeText(publisherObject.currentTarget, "address")
        trailing: [
            BIconButton {
                id: moreButton
                objectName: "publishTargetMore"
                iconName: "ellipsis"
                label: qsTr("More")
                enabled: !publisherObject.busy
                onClicked: targetMenu.popup(moreButton, 0, moreButton.height)
            }
        ]

        // What is missing before it can publish.
        RowLayout {
            Layout.fillWidth: true
            visible: AppController.routeFlag(publisherObject.currentTarget, "needsToken")
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "lock"; size: Theme.iconSize; color: Theme.warningText }
            Hint {
                text: qsTr("No GitHub token on this device yet: publishing needs one with the “repo” scope.")
                color: Theme.warningText
            }
            BButton {
                text: qsTr("Add a token…")
                size: "small"
                onClicked: AppController.navigate({ view: "sync", tab: "github" })
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: AppController.routeFlag(publisherObject.currentTarget, "needsPassword")
                     || AppController.routeFlag(publisherObject.currentTarget, "needsSharePassword")
                     || AppController.routeFlag(publisherObject.currentTarget, "needsPagesPassword")
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "lock"; size: Theme.iconSize; color: Theme.warningText }
            Hint {
                text: qsTr("A password of this target is not on this device: type it in the target's settings.")
                color: Theme.warningText
            }
            BButton {
                text: qsTr("Edit…")
                size: "small"
                onClicked: targetDialog.openFor(publisherObject.current)
            }
        }

        BText {
            Layout.fillWidth: true
            text: qsTr("What is published")
            variant: "label"
        }
        Repeater {
            model: AppController.routeList(publisherObject.currentTarget, "items")
            delegate: RowLayout {
                id: itemRow
                required property var modelData
                required property int index
                readonly property string kind: AppController.routeText(modelData, "kind")
                readonly property string label: AppController.routeText(modelData, "label")
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon {
                    name: AppController.routeText(itemRow.modelData, "icon")
                    size: Theme.iconSize
                    color: Theme.fgMuted
                }
                BText {
                    Layout.fillWidth: true
                    text: panel.itemName(itemRow.kind, itemRow.label)
                    elide: Text.ElideMiddle
                }
                BIconButton {
                    iconName: "x"
                    label: qsTr("Stop publishing it")
                    size: "small"
                    enabled: !publisherObject.busy
                    onClicked: publisherObject.removeItem(itemRow.index)
                }
            }
        }
        Hint {
            visible: AppController.routeInt(publisherObject.currentTarget, "itemCount", 0) === 0
            text: qsTr("Nothing yet: add a page, a folder, a tag or the whole notebook.")
        }
        BButton {
            id: addButton
            objectName: "publishAddItem"
            text: qsTr("Add…")
            iconName: "plus"
            size: "small"
            enabled: !publisherObject.busy
            onClicked: addMenu.popup(addButton, 0, addButton.height)
        }

        BSwitch {
            Layout.fillWidth: true
            text: qsTr("Also the pages they link to")
            description: qsTr("One step of links: a published page's links lead somewhere.")
            checked: AppController.routeFlag(publisherObject.currentTarget, "linkedPages")
            enabled: !publisherObject.busy
            onToggled: publisherObject.setOption("linkedPages", checked)
        }
        BSwitch {
            Layout.fillWidth: true
            text: qsTr("Only pages marked publish: true")
            description: qsTr("A page you add by itself is published anyway.")
            checked: AppController.routeFlag(publisherObject.currentTarget, "onlyMarked")
            enabled: !publisherObject.busy
            onToggled: publisherObject.setOption("onlyMarked", checked)
        }
        BSwitch {
            Layout.fillWidth: true
            text: qsTr("Extension pages")
            description: qsTr("Pages tagged #extension hold the notebook's code: left out unless chosen.")
            checked: AppController.routeFlag(publisherObject.currentTarget, "extensionPages")
            enabled: !publisherObject.busy
            onToggled: publisherObject.setOption("extensionPages", checked)
        }
        BSwitch {
            Layout.fillWidth: true
            text: qsTr("Conversations with AI")
            description: qsTr("The AI folder: left out unless chosen.")
            checked: AppController.routeFlag(publisherObject.currentTarget, "aiConversations")
            enabled: !publisherObject.busy
            onToggled: publisherObject.setOption("aiConversations", checked)
        }
        BSegmented {
            Layout.fillWidth: Theme.compact
            fill: Theme.compact
            enabled: !publisherObject.busy
            accessibleName: qsTr("Content")
            model: panel.contents
            currentValue: AppController.routeText(publisherObject.currentTarget, "content")
            onActivated: (value) => publisherObject.setContent("" + value)
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "publishPreviewButton"
                text: qsTr("Preview")
                iconName: "eye"
                enabled: !publisherObject.busy
                busy: publisherObject.state === "checking"
                onClicked: publisherObject.preview()
            }
            BButton {
                objectName: "publishButton"
                text: qsTr("Publish")
                iconName: "upload"
                variant: "primary"
                enabled: !publisherObject.busy
                busy: publisherObject.state === "publishing"
                onClicked: publisherObject.publish(false)
            }
            BButton {
                visible: publisherObject.busy
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: publisherObject.cancel()
            }
        }

        // Where it is.
        RowLayout {
            Layout.fillWidth: true
            visible: AppController.routeText(publisherObject.currentTarget, "url") !== "" && publisherObject.state === "idle"
            spacing: Theme.space2
            BIcon { name: "globe"; size: Theme.iconSize; color: Theme.fgMuted }
            BText {
                Layout.fillWidth: true
                text: AppController.routeText(publisherObject.currentTarget, "url")
                color: Theme.link
                elide: Text.ElideMiddle
            }
            BButton {
                text: qsTr("Open")
                iconName: "external-link"
                size: "small"
                variant: "ghost"
                onClicked: Workspace.openUrl(AppController.routeText(publisherObject.currentTarget, "url"))
            }
        }
        Hint {
            text: AppController.routeText(publisherObject.currentTarget, "lastText")
                  + (AppController.routeText(publisherObject.currentTarget, "lastError") !== ""
                     ? " – " + AppController.routeText(publisherObject.currentTarget, "lastError") : "")
        }
    }

    // ---- Working ---------------------------------------------------------------------------------
    IoCard {
        visible: publisherObject.busy
        iconName: "hourglass"
        title: publisherObject.state === "unpublishing" ? qsTr("Unpublishing…")
               : publisherObject.state === "publishing" ? qsTr("Publishing…") : qsTr("Preparing the preview…")
        subtitle: publisherObject.progressText
        BProgressBar {
            Layout.fillWidth: true
            indeterminate: publisherObject.progress < 0
            value: Math.max(0, publisherObject.progress)
        }
    }

    // ---- The plan, the result ---------------------------------------------------------------------
    IoCard {
        id: planCard
        objectName: "publishPlan"
        visible: publisherObject.state === "previewed" || publisherObject.state === "published"
                 || publisherObject.state === "conflicts" || publisherObject.state === "unpublished"
        tone: publisherObject.state === "published" || publisherObject.state === "unpublished" ? "success"
              : publisherObject.state === "conflicts" ? "warning" : "default"
        iconName: publisherObject.state === "previewed" ? "eye"
                  : publisherObject.state === "conflicts" ? "triangle-alert" : "circle-check"
        iconColor: publisherObject.state === "conflicts" ? Theme.warningText
                   : publisherObject.state === "previewed" ? Theme.fgMuted : Theme.successText
        title: publisherObject.state === "previewed" ? qsTr("What a publish would do")
               : publisherObject.state === "conflicts" ? qsTr("Changed at the target")
               : publisherObject.state === "unpublished" ? qsTr("Unpublished") : qsTr("Published")
        subtitle: publisherObject.plan.summary !== undefined ? "" + publisherObject.plan.summary : ""

        Hint {
            visible: publisherObject.state !== "unpublished"
            text: publisherObject.plan.pagesText !== undefined ? "" + publisherObject.plan.pagesText : ""
        }
        Hint {
            visible: AppController.routeFlag(publisherObject.plan, "protectedPages") && publisherObject.state !== "unpublished"
            text: qsTr("Encrypted with the pages' password: the files have random names, and the browser asks for the password.")
        }
        Hint {
            visible: text !== ""
            text: publisherObject.plan.remoteText !== undefined ? "" + publisherObject.plan.remoteText : ""
            color: Theme.warningText
        }
        Repeater {
            model: AppController.routeStrings(publisherObject.plan, "warnings")
            delegate: Hint {
                required property string modelData
                text: "• " + modelData
            }
        }
        BText {
            Layout.fillWidth: true
            visible: publisherObject.state === "conflicts"
            text: publisherObject.errorText
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            visible: publisherObject.state === "published" && publisherObject.url !== ""
            spacing: Theme.space2
            BText {
                objectName: "publishUrl"
                Layout.fillWidth: true
                text: publisherObject.url
                color: Theme.link
                elide: Text.ElideMiddle
            }
            BButton {
                text: qsTr("Open")
                iconName: "external-link"
                size: "small"
                onClicked: Workspace.openUrl(publisherObject.url)
            }
            BButton {
                text: qsTr("Copy link")
                iconName: "copy"
                size: "small"
                variant: "ghost"
                onClicked: {
                    if (publisherObject.copyUrl())
                        AppController.toast(qsTr("Link copied."), "success")
                }
            }
        }

        // The files, what changes first.
        ColumnLayout {
            Layout.fillWidth: true
            visible: publisherObject.state !== "unpublished"
            spacing: 2
            Repeater {
                model: AppController.routeList(publisherObject.plan, "files")
                delegate: RowLayout {
                    id: fileRow
                    required property string path
                    required property string actionText
                    required property string sizeText
                    required property var modelData
                    readonly property string action: AppController.routeText(modelData, "action")
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon {
                        name: AppController.routeText(fileRow.modelData, "icon")
                        size: Theme.iconSizeSm
                        color: fileRow.action === "conflict" ? Theme.warningText
                               : fileRow.action === "unchanged" ? Theme.fgFaint : Theme.fgMuted
                    }
                    BText {
                        Layout.fillWidth: true
                        text: fileRow.path
                        variant: "small"
                        color: fileRow.action === "unchanged" ? Theme.fgMuted : Theme.fg
                        elide: Text.ElideMiddle
                    }
                    BText {
                        text: fileRow.actionText + (fileRow.sizeText !== "" ? " · " + fileRow.sizeText : "")
                        variant: "small"
                        color: fileRow.action === "conflict" ? Theme.warningText : Theme.fgMuted
                    }
                }
            }
            Hint {
                visible: AppController.routeInt(publisherObject.plan, "moreFiles", 0) > 0
                text: qsTr("…and %n more file(s).", "", AppController.routeInt(publisherObject.plan, "moreFiles", 0))
            }
        }

        // Left out, and why.
        ColumnLayout {
            Layout.fillWidth: true
            visible: AppController.routeInt(publisherObject.plan, "leftOutCount", 0) > 0
            spacing: 2
            BText {
                Layout.fillWidth: true
                text: qsTr("Left out")
                variant: "label"
            }
            Repeater {
                model: AppController.routeList(publisherObject.plan, "leftOut")
                delegate: RowLayout {
                    id: leftRow
                    required property string path
                    required property string reasonText
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon { name: "eye-off"; size: Theme.iconSizeSm; color: Theme.fgMuted }
                    BText {
                        Layout.fillWidth: true
                        text: leftRow.path
                        variant: "small"
                        elide: Text.ElideMiddle
                    }
                    BText {
                        text: leftRow.reasonText
                        variant: "small"
                        color: Theme.fgMuted
                    }
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "publishFromPreview"
                visible: publisherObject.state === "previewed" && AppController.routeFlag(publisherObject.plan, "hasChanges")
                         && AppController.routeInt(publisherObject.plan, "conflictCount", 0) === 0
                text: qsTr("Publish")
                iconName: "upload"
                variant: "primary"
                onClicked: publisherObject.publish(false)
            }
            BButton {
                objectName: "publishReplace"
                visible: publisherObject.state === "conflicts"
                         || (publisherObject.state === "previewed"
                             && AppController.routeInt(publisherObject.plan, "conflictCount", 0) > 0)
                text: qsTr("Replace them")
                iconName: "triangle-alert"
                variant: "danger"
                onClicked: replaceConfirm.open()
            }
            BButton {
                text: qsTr("Done")
                variant: "ghost"
                onClicked: publisherObject.dismiss()
            }
        }
    }

    IoCard {
        visible: publisherObject.state === "error"
        tone: "danger"
        iconName: "circle-alert"
        iconColor: Theme.dangerText
        title: qsTr("Publishing did not work")
        subtitle: publisherObject.errorText
        BButton {
            text: qsTr("Close")
            variant: "ghost"
            onClicked: publisherObject.dismiss()
        }
    }

    // ---- Menus and dialogs -----------------------------------------------------------------------
    BMenu {
        id: addMenu
        BMenuItem {
            objectName: "publishAddPage"
            text: qsTr("A page…")
            iconName: "file-text"
            onTriggered: {
                pathPicker.mode = "page"
                pathPicker.open()
            }
        }
        BMenuItem {
            text: qsTr("A folder…")
            iconName: "folder"
            onTriggered: {
                pathPicker.mode = "folder"
                pathPicker.open()
            }
        }
        BMenuItem {
            text: qsTr("A tag…")
            iconName: "hash"
            onTriggered: tagPicker.open()
        }
        BMenuItem {
            text: qsTr("The whole notebook")
            iconName: "notebook-pen"
            onTriggered: publisherObject.addItem("notebook", "")
        }
    }

    BMenu {
        id: targetMenu
        BMenuItem {
            text: qsTr("Edit…")
            iconName: "pencil"
            onTriggered: targetDialog.openFor(publisherObject.current)
        }
        BMenuItem {
            objectName: "publishUnpublish"
            text: qsTr("Unpublish…")
            iconName: "cloud-off"
            enabled: AppController.routeFlag(publisherObject.currentTarget, "published")
            onTriggered: unpublishConfirm.open()
        }
        BMenuSeparator {}
        BMenuItem {
            text: qsTr("Remove the target…")
            iconName: "trash-2"
            danger: true
            onTriggered: removeConfirm.open()
        }
    }

    IoPathPickerDialog {
        id: pathPicker
        folders: publisherObject.folders
        onChosen: (path) => publisherObject.addItem(pathPicker.mode, path)
    }

    BDialog {
        id: tagPicker
        title: qsTr("Choose a tag")
        iconName: "hash"
        showFooter: false
        initialFocusItem: tagSearch
        preferredWidth: Theme.dialogWidth
        onAboutToShow: {
            tagSearch.text = ""
            panel.tagQuery = ""
        }
        BSearchField {
            id: tagSearch
            Layout.fillWidth: true
            placeholderText: qsTr("Search tags…")
            onSearch: (text) => panel.tagQuery = text
            Keys.onDownPressed: tagList.forceActiveFocus()
        }
        ListView {
            id: tagList
            TouchFlick.enabled: true
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(contentHeight, Theme.listItemHeightTwoLine), 380)
            clip: true
            model: panel.tagRowsFor(tagPicker.visible, panel.tagQuery)
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: BScrollBar {}
            delegate: BListItem {
                id: tagRow
                required property string title
                required subtitle
                required property string path
                width: ListView.view.width
                iconName: "hash"
                text: tagRow.title
                onClicked: {
                    publisherObject.addItem("tag", tagRow.path)
                    tagPicker.close()
                }
            }
            BText {
                anchors.centerIn: parent
                visible: tagList.count === 0
                text: qsTr("No tags found.")
                variant: "muted"
            }
        }
    }

    PublishTargetDialog {
        id: targetDialog
        objectName: "publishTargetDialog"
        publisher: publisherObject
        onSaved: (id) => {
            publisherObject.current = id
            publisherObject.addPending()
        }
        onRejected: publisherObject.clearPending()
    }

    BConfirmDialog {
        id: unpublishConfirm
        title: qsTr("Unpublish?")
        subject: AppController.routeText(publisherObject.currentTarget, "name")
        subjectIcon: "globe"
        message: qsTr("Every file this target published is removed from it (a Nextcloud link too). Your notes stay as they are.")
        confirmText: qsTr("Unpublish")
        destructive: true
        onAccepted: publisherObject.unpublish(false)
    }
    BConfirmDialog {
        id: replaceConfirm
        title: qsTr("Replace the changed files?")
        message: qsTr("Someone changed these files at the target since the last publish (or they were there before). Publishing replaces them with this notebook's pages.")
        confirmText: qsTr("Replace them")
        destructive: true
        onAccepted: publisherObject.publish(true)
    }
    BConfirmDialog {
        id: removeConfirm
        title: qsTr("Remove the target?")
        subject: AppController.routeText(publisherObject.currentTarget, "name")
        subjectIcon: "globe"
        message: qsTr("Brulion forgets where it published and its passwords. What it published stays there – unpublish it first to remove it.")
        confirmText: qsTr("Remove")
        destructive: true
        onAccepted: publisherObject.removeTarget(publisherObject.current)
    }
}
