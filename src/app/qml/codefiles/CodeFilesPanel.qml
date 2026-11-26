pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// CodeFilesPanel — the "Files" tab of Code mode's right panel (docs/CODE-FILES.md "The file
// viewer", docs/UI.md "Code mode"): one file of the thread's folder, read-only, its colours and
// line numbers (CodeFileView over CodeFileDocument), a picture as a picture; binary files, missing
// ones and folders say so; big files show their first 2 MB. Its header: the recent files (a menu,
// kept per folder in the device's settings, `code/recentFiles/<hash>`), the path as a breadcrumb (a
// folder part opens the file picker there), Go to file… (the picker), Copy path, Open in editor,
// Reload. Lines selected → "Comment…": a short text, then the comment goes to the thread's composer
// (addToChat: the path, the lines in a fenced block, the comment — CodeFiles.reviewComment).
//
//   CodeFilesPanel { folder: root.folder; onAddToChat: (text) => chat.addToComposer(text) }
//   panel.open("src/main.cpp", 42)          // shown at line 42, marked
Item {
    id: panel

    property string folder
    /// The file shown (relative to `folder`; "" none).
    property string path
    /// Recently opened files of the folder, newest first.
    property list<string> recent

    signal addToChat(string text)
    /// The file picker asked for, with this query.
    signal pickRequested(string query)

    /// The line to show once the file is read (0: the top), when `positionPending`; else a file read
    /// again keeps `keepLine` at the top.
    property int pendingLine: 0
    property bool positionPending: false
    property int keepLine: 0
    /// The lines a comment is being written about.
    property int commentFrom: 0
    property int commentTo: 0

    readonly property string kind: doc.kind
    readonly property list<string> parts: panel.path !== "" ? Js.split(panel.path, "/") : panel.noStrings
    readonly property list<string> noStrings: []
    readonly property var syntaxPalette: ({
        keyword: Theme.chipScheduledFg, type: Theme.link, constant: Theme.warningText, builtin: Theme.link,
        function: Theme.fg, number: Theme.warningText, string: Theme.chipRecurrenceFg, comment: Theme.fgFaint,
        preprocessor: Theme.dangerText, tag: Theme.dangerText, attribute: Theme.warningText,
        property: Theme.link, variable: Theme.tagFg, inserted: Theme.diffAddFg, deleted: Theme.diffDelFg
    })

    /// Show `path` (relative to the folder, "/"-separated) at `line` (0: the top).
    function open(path: string, line: int): void {
        const same = path === panel.path
        panel.pendingLine = line
        panel.positionPending = true
        panel.path = path
        view.clearSelection()
        panel.noteRecent(path)
        if (same && doc.kind === "text")
            applyLater.restart()
        else if (same)
            doc.reload()
    }
    function noteRecent(path: string): void {
        if (path === "" || panel.folder === "")
            return
        panel.recent = CodeFiles.withRecent(panel.recent, path, 20)
        AppSettings.setValue(CodeFiles.recentKey(panel.folder), panel.recent.join("\n"))
    }
    function loadRecent(): void {
        panel.recent = panel.folder === "" ? panel.noStrings
                     : Js.nonEmptyStrings(Js.split("" + AppSettings.value(CodeFiles.recentKey(panel.folder), ""), "\n"))
    }
    function applyLine(): void {
        if (doc.kind !== "text")
            return
        if (panel.positionPending) {
            panel.positionPending = false
            view.showLine(panel.pendingLine)
        } else {
            view.scrollTo(panel.keepLine)
        }
    }
    /// Read the file again where it is shown (an agent's turn may have changed it).
    function reload(): void {
        if (panel.path === "")
            return
        panel.keepLine = doc.kind === "text" ? view.topLine() : 0
        doc.reload()
    }
    function copyPath(): void {
        if (CodeFiles.copyText(panel.path))
            AppController.toast(qsTr("Path copied"), "success")
    }
    function openInEditor(): void {
        const abs = doc.absolutePath
        if (abs === "")
            return
        if (!Workspace.projects.openInEditor(abs))
            AppController.toast(qsTr("The editor could not be started: set its command in Settings → Extensions → Code mode."), "error")
    }
    function startComment(from: int, to: int): void {
        panel.commentFrom = from
        panel.commentTo = to
        commentField.text = ""
        commentDialog.open()
    }
    function sendComment(): void {
        const lines = doc.linesText(panel.commentFrom, panel.commentTo)
        const comment = "" + commentField.text
        panel.addToChat(CodeFiles.reviewComment(panel.path, panel.commentFrom, panel.commentTo, lines, comment))
        view.clearSelection()
    }
    /// A folder part of the breadcrumb: the picker in that folder.
    function pickIn(index: int): void {
        panel.pickRequested(panel.parts.slice(0, index + 1).join("/") + "/")
    }

    onFolderChanged: {
        panel.path = ""
        panel.loadRecent()
    }
    Component.onCompleted: panel.loadRecent()

    CodeFileDocument {
        id: doc
        objectName: "codeFileDocument"
        folder: panel.folder
        path: panel.path
        palette: panel.syntaxPalette
        onLoaded: applyLater.restart()
    }
    // (The rows exist once the model's reset has been laid out.)
    Later {
        id: applyLater
        onTriggered: panel.applyLine()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ---- The path, the recent files, the actions ------------------------------------------
        RowLayout {
            objectName: "codeFilesHeader"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space2
            Layout.rightMargin: Theme.space2
            Layout.topMargin: Theme.space1
            Layout.bottomMargin: Theme.space1
            spacing: Theme.space1

            BIconButton {
                id: recentButton
                objectName: "codeFilesRecent"
                size: "small"
                iconName: "history"
                label: qsTr("Recent files")
                enabled: panel.recent.length > 0
                onClicked: recentMenu.popup(recentButton, 0, recentButton.height)
            }
            BBreadcrumb {
                objectName: "codeFilesPath"
                Layout.fillWidth: true
                visible: panel.path !== ""
                model: Js.fromStrings(panel.parts)
                onActivated: (index) => {
                    if (index < panel.parts.length - 1)
                        panel.pickIn(index)
                }
            }
            Item {
                Layout.fillWidth: true
                visible: panel.path === ""
                Layout.preferredHeight: 1
            }
            BSpinner {
                visible: doc.kind === "loading"
                size: Theme.iconSizeSm
            }
            BIconButton {
                objectName: "codeFilesGoTo"
                size: "small"
                iconName: "search"
                label: qsTr("Go to file…")
                shortcut: "Ctrl+P"
                onClicked: panel.pickRequested("")
            }
            BIconButton {
                objectName: "codeFilesCopyPath"
                size: "small"
                iconName: "copy"
                label: qsTr("Copy path")
                enabled: panel.path !== ""
                onClicked: panel.copyPath()
            }
            BIconButton {
                objectName: "codeFilesOpenEditor"
                size: "small"
                iconName: "external-link"
                label: qsTr("Open in editor")
                enabled: doc.absolutePath !== "" && doc.kind !== "missing"
                onClicked: panel.openInEditor()
            }
            BIconButton {
                objectName: "codeFilesReload"
                size: "small"
                iconName: "refresh-cw"
                label: qsTr("Reload")
                enabled: panel.path !== ""
                onClicked: panel.reload()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.borderSubtle
        }
        // Only the start of a big file.
        BText {
            objectName: "codeFilesTruncated"
            Layout.fillWidth: true
            visible: doc.kind === "text" && doc.truncated
            leftPadding: Theme.space3
            rightPadding: Theme.space3
            topPadding: Theme.space1
            bottomPadding: Theme.space1
            text: qsTr("Only the first 2 MB of this %1 file are shown.").arg(CodeFiles.sizeText(doc.size))
            variant: "faint"
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            CodeFileView {
                id: view
                objectName: "codeFileView"
                anchors.fill: parent
                visible: doc.kind === "text"
                document: doc
                path: panel.path
                onCommentRequested: (from, to) => panel.startComment(from, to)
                onCopyRequested: (text) => {
                    if (CodeFiles.copyText(text))
                        AppController.toast(qsTr("Copied"), "success")
                }
            }
            Rectangle {
                anchors.fill: parent
                visible: doc.kind === "picture"
                color: Theme.bgSunken
                Image {
                    objectName: "codeFilePicture"
                    anchors.fill: parent
                    anchors.margins: Theme.space4
                    source: doc.kind === "picture" ? doc.fileUrl : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    // (Never decoded larger than the panel can show.)
                    sourceSize.width: Math.max(64, Math.ceil(width * 2))
                    sourceSize.height: Math.max(64, Math.ceil(height * 2))
                }
            }
            BSpinner {
                anchors.centerIn: parent
                visible: doc.kind === "loading"
            }
            BEmptyState {
                objectName: "codeFilesMessage"
                anchors.centerIn: parent
                width: Math.min(parent.width - 2 * Theme.space4, maximumWidth)
                visible: panel.path !== "" && doc.kind !== "text" && doc.kind !== "picture" && doc.kind !== "loading"
                         && doc.kind !== ""
                compact: true
                iconName: doc.kind === "folder" ? "folder" : doc.kind === "missing" ? "file" : "triangle-alert"
                title: doc.kind === "binary" ? qsTr("Binary file")
                     : doc.kind === "missing" ? qsTr("Not there")
                     : doc.kind === "folder" ? qsTr("A folder")
                     : doc.kind === "outside" ? qsTr("Outside the project")
                     : qsTr("Cannot be read")
                description: doc.kind === "binary" ? qsTr("Its contents are not text (%1).").arg(CodeFiles.sizeText(doc.size))
                           : doc.kind === "missing" ? qsTr("This file is not in the folder (any more).")
                           : doc.kind === "folder" ? qsTr("Pick one of its files with Go to file….")
                           : doc.kind === "outside" ? qsTr("Only the files of the thread's folder are shown here.")
                           : qsTr("Brulion may not read this file.")
            }

            // ---- No file open: the picker, the recent files ----------------------------------
            ColumnLayout {
                objectName: "codeFilesEmpty"
                anchors.fill: parent
                anchors.margins: Theme.space3
                visible: panel.path === ""
                spacing: Theme.space2
                BEmptyState {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Theme.space6
                    compact: true
                    iconName: "file-text"
                    title: qsTr("No file open")
                    description: qsTr("Open a file of the project to read it here: Go to file… (Ctrl+P), a search's result, a file an agent names.")
                }
                BButton {
                    objectName: "codeFilesEmptyGoTo"
                    Layout.alignment: Qt.AlignHCenter
                    iconName: "search"
                    text: qsTr("Go to file…")
                    onClicked: panel.pickRequested("")
                }
                BSectionHeader {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space4
                    visible: panel.recent.length > 0
                    text: qsTr("Recent")
                }
                ListView {
                    objectName: "codeFilesRecentList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: panel.recent
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: BListItem {
                        id: recentRow
                        required property string modelData
                        width: ListView.view.width
                        dense: true
                        iconName: CodeFiles.iconOf(recentRow.modelData, false)
                        text: recentRow.modelData
                        onClicked: panel.open(recentRow.modelData, 0)
                    }
                }
            }
        }
    }

    BMenu {
        id: recentMenu
        objectName: "codeFilesRecentMenu"
        Instantiator {
            model: panel.recent
            delegate: BMenuItem {
                id: recentItem
                required property string modelData
                text: recentItem.modelData
                iconName: recentItem.modelData === panel.path ? "check" : CodeFiles.iconOf(recentItem.modelData, false)
                onTriggered: panel.open(recentItem.modelData, 0)
            }
            onObjectAdded: (index, object) => recentMenu.insertItem(index, object as Item)
            onObjectRemoved: (index, object) => recentMenu.removeItem(object as Item)
        }
    }

    BDialog {
        id: commentDialog
        objectName: "codeFileCommentDialog"
        title: panel.commentTo > panel.commentFrom ? qsTr("Comment on lines %1–%2").arg(panel.commentFrom).arg(panel.commentTo)
                                                   : qsTr("Comment on line %1").arg(panel.commentFrom)
        subtitle: panel.path
        iconName: "message-square"
        acceptText: qsTr("Add to chat")
        initialFocusItem: commentField
        onAccepted: panel.sendComment()
        BTextArea {
            id: commentField
            objectName: "codeFileCommentField"
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            placeholderText: qsTr("What should the agent do here?")
            wrapMode: TextEdit.Wrap
        }
        BText {
            Layout.fillWidth: true
            text: qsTr("The lines and your comment go to the thread's message, to send with what else you write.")
            variant: "faint"
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
    }
}
