pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitHubOpenDialog — a GitHub repository (or one of its folders) as a new notebook
// (Workspace.github.openRepository; docs/UI.md "Notebooks from GitHub"): the address as pasted
// ("owner/name", a github.com page of a folder or file, a git remote) with what Brulion understood,
// the branch and the folder when they are not in the address, a token for private repositories
// and for sending changes (when none is saved), and on the desktop the folder on this device
// (the app's storage by default). GitHub is asked first; then the notebook opens on the sync
// center's GitHub tab and its notes arrive.
//
//   GitHubOpenDialog { id: githubDialog }  …  githubDialog.openWith("", "")
BDialog {
    id: dialog

    readonly property WorkspaceGitHub github: Workspace.github
    /// WorkspaceGitHub.parseAddress() of the address typed.
    property var parsed: ({})
    readonly property bool addressValid: AppController.routeFlag(parsed, "valid")
    readonly property string parsedBranch: AppController.routeText(parsed, "branch")
    readonly property string parsedFolder: AppController.routeText(parsed, "folder")
    /// Why it could not be opened ("" when nothing failed).
    property string error
    /// Waiting for GitHub (the token, then the repository).
    property bool waiting: false
    property bool showToken: false
    readonly property string suggested: addressValid ? github.suggestedFolder(addressField.typedText, folderField.typedText) : ""

    function openWith(address: string, folder: string): void {
        addressField.text = address
        branchField.text = ""
        folderField.text = folder
        localField.text = ""
        tokenField.text = ""
        dialog.error = ""
        dialog.waiting = false
        dialog.parsed = dialog.github.parseAddress(address)
        dialog.open()
    }
    function startOpening(): void {
        // Trimmed by WorkspaceGitHub.
        dialog.github.openRepository(addressField.text, branchField.text, folderField.text,
                                     AppController.isDesktop ? localField.text : "")
    }
    function edited(): void {
        dialog.error = ""
        dialog.parsed = dialog.github.parseAddress(addressField.typedText)
    }

    iconName: "git-branch"
    title: qsTr("Open a notebook from GitHub")
    subtitle: qsTr("Markdown notes kept in a GitHub repository — all of it or one folder — as a notebook. Brulion downloads them, keeps them up to date and sends your changes back as commits or pull requests.")
    acceptText: qsTr("Open")
    busy: waiting || github.checkingLink || github.checkingToken
    acceptEnabled: addressValid && error === ""
    initialFocusItem: addressField
    onAcceptRequested: {
        dialog.error = ""
        dialog.waiting = true
        const token = Js.trim(tokenField.text)
        if (token !== "" && !dialog.github.signedIn)
            dialog.github.saveToken(token) // then onTokenChecked
        else
            dialog.startOpening()
    }
    onClosed: dialog.waiting = false

    Connections {
        target: dialog.github
        function onTokenChecked(ok: bool, message: string): void {
            if (!dialog.opened || !dialog.waiting)
                return
            if (ok) {
                tokenField.text = ""
                dialog.startOpening()
            } else {
                dialog.waiting = false
                dialog.error = message
            }
        }
        function onNotebookPrepared(path: string): void {
            if (!dialog.opened || !dialog.waiting)
                return
            dialog.waiting = false
            dialog.close()
            AppController.openVaultAt(path, { view: "sync", tab: "github" })
        }
        function onPrepareFailed(message: string): void {
            if (!dialog.opened)
                return
            dialog.waiting = false
            dialog.error = message
        }
    }

    /// Small muted explanatory text.
    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("Repository")
        variant: "label"
    }
    BTextField {
        id: addressField
        objectName: "githubOpenAddress"
        Layout.fillWidth: true
        leadingIcon: "git-branch"
        accessibleName: qsTr("Address of the repository")
        placeholderText: qsTr("owner/name or https://github.com/…")
        inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
        errorText: !dialog.addressValid ? AppController.routeText(dialog.parsed, "error") : ""
        onTextEdited: dialog.edited()
    }
    Hint {
        visible: dialog.addressValid
        text: {
            const name = AppController.routeText(dialog.parsed, "fullName")
            // The first segment after tree/ taken for the branch; GitHub is asked on Open (a branch
            // name may hold "/").
            if (dialog.parsedBranch === "")
                return qsTr("%1: its default branch, the whole repository").arg(name)
            if (dialog.parsedFolder === "")
                return qsTr("%1: the branch “%2”, the whole repository").arg(name).arg(dialog.parsedBranch)
            return qsTr("%1: the branch “%2”, the folder “%3”").arg(name).arg(dialog.parsedBranch).arg(dialog.parsedFolder)
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: Theme.compact ? 1 : 2
        columnSpacing: Theme.space3
        rowSpacing: Theme.space1
        BText { text: qsTr("Branch"); variant: "label" }
        BText { visible: !Theme.compact; text: qsTr("Folder in the repository"); variant: "label" }
        BTextField {
            id: branchField
            Layout.fillWidth: true
            accessibleName: qsTr("Branch")
            placeholderText: dialog.parsedBranch !== "" ? dialog.parsedBranch : qsTr("The default one")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.error = ""
        }
        BText { visible: Theme.compact; text: qsTr("Folder in the repository"); variant: "label" }
        BTextField {
            id: folderField
            Layout.fillWidth: true
            leadingIcon: "folder"
            accessibleName: qsTr("Folder in the repository")
            placeholderText: dialog.parsedFolder !== "" ? dialog.parsedFolder : qsTr("The whole repository")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.error = ""
        }
    }

    // A token: private repositories, and sending changes.
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.space1
        visible: !dialog.github.signedIn
        BText {
            Layout.fillWidth: true
            text: qsTr("Token (optional)")
            variant: "label"
        }
        BTextField {
            id: tokenField
            Layout.fillWidth: true
            leadingIcon: "lock"
            echoMode: dialog.showToken ? TextInput.Normal : TextInput.Password
            accessibleName: qsTr("Personal access token")
            placeholderText: "ghp_…"
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
            onTextEdited: dialog.error = ""
            trailingItem: BIconButton {
                iconName: dialog.showToken ? "eye-off" : "eye"
                label: dialog.showToken ? qsTr("Hide the token") : qsTr("Show the token")
                size: "small"
                onClicked: dialog.showToken = !dialog.showToken
            }
        }
        Hint {
            text: qsTr("Public repositories open without one (to read). A private repository, and sending your changes, need a personal access token of your GitHub account.")
        }
        BButton {
            text: qsTr("Make a token on GitHub")
            iconName: "external-link"
            size: "small"
            variant: "ghost"
            onClicked: Qt.openUrlExternally(dialog.github.tokenUrl)
        }
    }
    Hint {
        visible: dialog.github.signedIn
        text: dialog.github.login !== "" ? qsTr("Signed in to GitHub as %1.").arg("@" + dialog.github.login)
                                         : qsTr("A GitHub token is saved on this device.")
    }

    // Desktop: where the notes go on this device.
    ColumnLayout {
        Layout.fillWidth: true
        visible: AppController.isDesktop
        spacing: Theme.space1
        BText {
            Layout.fillWidth: true
            text: qsTr("Folder on this device")
            variant: "label"
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BTextField {
                id: localField
                Layout.fillWidth: true
                leadingIcon: "folder"
                accessibleName: qsTr("Folder on this device")
                placeholderText: dialog.suggested !== "" ? dialog.suggested : qsTr("In Brulion's own storage")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextEdited: dialog.error = ""
            }
            BButton {
                text: qsTr("Browse…")
                iconName: "folder-open"
                onClicked: folderPicker.open()
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        visible: dialog.error !== ""
        implicitHeight: errorRow.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.dangerBg
        RowLayout {
            id: errorRow
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "circle-alert"; size: Theme.iconSize; color: Theme.dangerText }
            BText {
                Layout.fillWidth: true
                text: dialog.error
                wrapMode: Text.Wrap
            }
        }
    }

    FilePicker {
        id: folderPicker
        title: qsTr("Choose the notebook folder")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: localField.text = AppController.localPath(selectedFolder)
    }
}
