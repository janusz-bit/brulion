pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// PublishTargetDialog — a new publishing target or one to change (PublishController.saveTarget;
// docs/UI.md "Publishing"): where it goes — GitHub Pages (a repository, its branch, a folder), a
// WebDAV folder, Nextcloud (the server, the account, a folder, its public link with a password and
// an expiry date), a folder on this computer (desktop) — its name, the site's title and address.
// Passwords are kept on this device in owner-only files, never in the notebook; a password left
// empty keeps the one kept.
//
//   PublishTargetDialog { id: targetDialog; publisher: controller; onSaved: (id) => … }
//   targetDialog.openFor("")        // a new one;  openFor(id) changes one
BDialog {
    id: dialog

    property PublishController publisher
    /// The target being changed ("" a new one).
    property string targetId
    property string kind: "github"
    property bool hasPassword: false
    property bool hasSharePassword: false
    property bool showPasswords: false
    readonly property string problem: publisher.checkFields({
        id: dialog.targetId,
        name: nameField.text,
        kind: dialog.kind,
        repository: repositoryField.text,
        branch: branchField.text,
        folder: dialog.kind === "nextcloud" ? cloudFolderField.text : folderField.text,
        url: urlField.text,
        server: serverField.text,
        username: userField.text,
        path: pathField.text,
        siteUrl: siteUrlField.text,
        title: titleField.text,
        content: "site",
        share: shareSwitch.checked,
        sharePassword: sharePasswordSwitch.checked,
        shareExpires: expiresField.text
    })
    /// The problem is shown once something was typed (or Save was asked for).
    property bool touched: false

    signal saved(string id)

    function openFor(id: string): void {
        fields.map = dialog.publisher.targetFields(id)
        dialog.targetId = id
        dialog.kind = AppController.routeText(fields.map, "kind")
        nameField.text = AppController.routeText(fields.map, "name")
        repositoryField.text = AppController.routeText(fields.map, "repository")
        branchField.text = id === "" ? "" : AppController.routeText(fields.map, "branch")
        folderField.text = AppController.routeText(fields.map, "folder")
        cloudFolderField.text = AppController.routeText(fields.map, "folder")
        urlField.text = AppController.routeText(fields.map, "url")
        serverField.text = AppController.routeText(fields.map, "server")
        userField.text = AppController.routeText(fields.map, "username")
        pathField.text = AppController.routeText(fields.map, "path")
        siteUrlField.text = AppController.routeText(fields.map, "siteUrl")
        titleField.text = AppController.routeText(fields.map, "title")
        shareSwitch.checked = id === "" || AppController.routeFlag(fields.map, "share")
        sharePasswordSwitch.checked = AppController.routeFlag(fields.map, "sharePassword")
        expiresField.text = AppController.routeText(fields.map, "shareExpires")
        dialog.hasPassword = AppController.routeFlag(fields.map, "hasPassword")
        dialog.hasSharePassword = AppController.routeFlag(fields.map, "hasSharePassword")
        passwordField.text = ""
        sharePasswordField.text = ""
        dialog.showPasswords = false
        dialog.touched = id !== ""
        dialog.open()
    }

    MapHolder {
        id: fields
    }

    iconName: "globe"
    title: targetId === "" ? qsTr("New publishing target") : qsTr("Change the target")
    subtitle: qsTr("Where the pages go. Passwords stay on this device.")
    acceptText: qsTr("Save")
    acceptEnabled: problem === ""
    initialFocusItem: nameField
    preferredWidth: Math.max(Theme.dialogWidth, 560)
    onAcceptRequested: dialog.touched = true
    onAccepted: {
        const id = dialog.publisher.saveTarget({
            id: dialog.targetId,
            name: nameField.text,
            kind: dialog.kind,
            repository: repositoryField.text,
            branch: branchField.text,
            folder: dialog.kind === "nextcloud" ? cloudFolderField.text : folderField.text,
            url: urlField.text,
            server: serverField.text,
            username: userField.text,
            path: pathField.text,
            siteUrl: siteUrlField.text,
            title: titleField.text,
            content: "site",
            share: shareSwitch.checked,
            sharePassword: sharePasswordSwitch.checked,
            shareExpires: expiresField.text
        }, passwordField.text, sharePasswordField.text)
        if (id !== "")
            dialog.saved(id)
    }

    /// Small muted explanatory text.
    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    component Label: BText {
        Layout.fillWidth: true
        variant: "label"
    }

    // ---- Where ---------------------------------------------------------------------------------
    Label { text: qsTr("Where") }
    GridLayout {
        objectName: "publishKinds"
        Layout.fillWidth: true
        columns: 2
        columnSpacing: Theme.space2
        rowSpacing: Theme.space2
        Repeater {
            model: dialog.publisher.kinds
            delegate: T.AbstractButton {
                id: tile
                required property var modelData
                required property string value
                required property string description
                readonly property string label: AppController.routeText(modelData, "text")
                readonly property string iconName: AppController.routeText(modelData, "icon")
                readonly property bool chosen: dialog.kind === value
                objectName: "publishKind_" + value
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                implicitHeight: implicitContentHeight + topPadding + bottomPadding
                padding: Theme.space3
                hoverEnabled: !Theme.touchUi
                focusPolicy: Qt.StrongFocus
                enabled: dialog.targetId === "" || tile.chosen
                Accessible.role: Accessible.RadioButton
                Accessible.name: tile.label
                Accessible.description: tile.description
                Accessible.checked: chosen
                onClicked: {
                    dialog.kind = tile.value
                    if (Js.trim(nameField.text) === "")
                        nameField.forceActiveFocus()
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: tile.chosen ? Theme.bgSelected : tile.down ? Theme.bgPressed : tile.hovered ? Theme.bgHover : Theme.bgElevated
                    border.width: tile.chosen ? 1.5 : 1
                    border.color: tile.chosen ? Theme.focusRing : Theme.border
                    opacity: tile.enabled ? 1 : 0.5
                    BFocusRing { visible: tile.visualFocus; targetRadius: Theme.radius }
                }
                contentItem: RowLayout {
                    spacing: Theme.space3
                    BIcon {
                        Layout.alignment: Qt.AlignTop
                        name: tile.iconName
                        size: Theme.iconSize
                        color: tile.chosen ? Theme.link : Theme.fgMuted
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        BText {
                            Layout.fillWidth: true
                            text: tile.label
                            variant: "label"
                            wrapMode: Text.Wrap
                        }
                        Hint {
                            visible: !Theme.compact
                            text: tile.description
                        }
                    }
                }
            }
        }
    }

    Label { text: qsTr("Name") }
    BTextField {
        id: nameField
        objectName: "publishTargetName"
        Layout.fillWidth: true
        accessibleName: qsTr("Name")
        placeholderText: qsTr("Blog, Recipes for the family…")
        onTextEdited: dialog.touched = true
    }

    // ---- GitHub Pages --------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.kind === "github"
        spacing: Theme.space1
        Label { text: qsTr("Repository") }
        BTextField {
            id: repositoryField
            objectName: "publishRepository"
            Layout.fillWidth: true
            leadingIcon: "git-branch"
            accessibleName: qsTr("Repository")
            placeholderText: qsTr("owner/name")
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.touched = true
        }
        GridLayout {
            Layout.fillWidth: true
            columns: Theme.compact ? 1 : 2
            columnSpacing: Theme.space3
            rowSpacing: Theme.space1
            Label { text: qsTr("Branch") }
            Label { visible: !Theme.compact; text: qsTr("Folder in the branch") }
            BTextField {
                id: branchField
                Layout.fillWidth: true
                accessibleName: qsTr("Branch")
                placeholderText: "gh-pages"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }
            Label { visible: Theme.compact; text: qsTr("Folder in the branch") }
            BTextField {
                id: folderField
                Layout.fillWidth: true
                leadingIcon: "folder"
                accessibleName: qsTr("Folder in the branch")
                placeholderText: qsTr("Its root (or docs)")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            }
        }
        Hint {
            text: qsTr("A branch that does not exist yet is made, without history. GitHub Pages is turned on for it (its root or docs/) and serves the site at https://<owner>.github.io/<name>/.")
        }
        Hint {
            text: Workspace.github.signedIn
                  ? (Workspace.github.login !== "" ? qsTr("Uses the GitHub token saved on this device (%1).").arg("@" + Workspace.github.login)
                                                   : qsTr("Uses the GitHub token saved on this device."))
                  : qsTr("Publishing needs a GitHub token with the “repo” scope: add it in the sync center's GitHub tab.")
            color: Workspace.github.signedIn ? Theme.fgMuted : Theme.warningText
        }
        BButton {
            visible: !Workspace.github.signedIn
            text: qsTr("Add a GitHub token…")
            iconName: "lock"
            size: "small"
            variant: "ghost"
            onClicked: {
                dialog.close()
                AppController.navigate({ view: "sync", tab: "github" })
            }
        }
    }

    // ---- WebDAV --------------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.kind === "webdav"
        spacing: Theme.space1
        Label { text: qsTr("WebDAV address of the folder") }
        BTextField {
            id: urlField
            Layout.fillWidth: true
            leadingIcon: "server"
            accessibleName: qsTr("WebDAV address of the folder")
            placeholderText: "https://example.com/dav/strona"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.touched = true
        }
    }

    // ---- Nextcloud -----------------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.kind === "nextcloud"
        spacing: Theme.space1
        Label { text: qsTr("Server") }
        BTextField {
            id: serverField
            Layout.fillWidth: true
            leadingIcon: "cloud"
            accessibleName: qsTr("Server")
            placeholderText: "cloud.example.com"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.touched = true
        }
        Label { text: qsTr("Folder for the site") }
        BTextField {
            id: cloudFolderField
            Layout.fillWidth: true
            leadingIcon: "folder"
            accessibleName: qsTr("Folder for the site")
            placeholderText: qsTr("Public/Recipes")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.touched = true
        }
    }

    // ---- WebDAV and Nextcloud: the account -------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        visible: dialog.kind === "webdav" || dialog.kind === "nextcloud"
        columns: Theme.compact ? 1 : 2
        columnSpacing: Theme.space3
        rowSpacing: Theme.space1
        Label { text: qsTr("User name") }
        Label { visible: !Theme.compact; text: dialog.kind === "nextcloud" ? qsTr("App password") : qsTr("Password") }
        BTextField {
            id: userField
            Layout.fillWidth: true
            leadingIcon: "at-sign"
            accessibleName: qsTr("User name")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.touched = true
        }
        Label { visible: Theme.compact; text: dialog.kind === "nextcloud" ? qsTr("App password") : qsTr("Password") }
        BTextField {
            id: passwordField
            objectName: "publishPassword"
            Layout.fillWidth: true
            leadingIcon: "lock"
            echoMode: dialog.showPasswords ? TextInput.Normal : TextInput.Password
            accessibleName: qsTr("Password")
            placeholderText: dialog.hasPassword ? qsTr("Kept on this device") : ""
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
            trailingItem: BIconButton {
                iconName: dialog.showPasswords ? "eye-off" : "eye"
                label: dialog.showPasswords ? qsTr("Hide the passwords") : qsTr("Show the passwords")
                size: "small"
                onClicked: dialog.showPasswords = !dialog.showPasswords
            }
        }
    }
    Hint {
        visible: dialog.kind === "nextcloud"
        text: qsTr("An app password from Nextcloud's Settings → Security works best (and can be revoked there).")
    }

    // ---- Nextcloud's public link ------------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.kind === "nextcloud"
        spacing: Theme.space1
        BSwitch {
            id: shareSwitch
            objectName: "publishShare"
            Layout.fillWidth: true
            text: qsTr("A public link")
            iconName: "link"
            description: qsTr("Anyone with the link sees the folder (read-only). Off: the files are on Nextcloud, without a link.")
        }
        BSwitch {
            id: sharePasswordSwitch
            Layout.fillWidth: true
            visible: shareSwitch.checked
            text: qsTr("The link asks for a password")
            iconName: "lock"
        }
        BTextField {
            id: sharePasswordField
            Layout.fillWidth: true
            visible: shareSwitch.checked && sharePasswordSwitch.checked
            leadingIcon: "lock"
            echoMode: dialog.showPasswords ? TextInput.Normal : TextInput.Password
            accessibleName: qsTr("The link's password")
            placeholderText: dialog.hasSharePassword ? qsTr("Kept on this device") : qsTr("The link's password")
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
        }
        Label { visible: shareSwitch.checked; text: qsTr("The link expires on") }
        BTextField {
            id: expiresField
            Layout.fillWidth: true
            visible: shareSwitch.checked
            leadingIcon: "calendar-days"
            accessibleName: qsTr("The link expires on")
            placeholderText: qsTr("YYYY-MM-DD (empty: never)")
            inputMethodHints: Qt.ImhDate | Qt.ImhNoPredictiveText
            onTextEdited: dialog.touched = true
        }
        Hint {
            visible: shareSwitch.checked
            text: qsTr("Nextcloud shows a shared folder as a list of files: the viewers open a page there, or download the folder and open index.html. For one file to read online choose “One document” in the target's options.")
        }
    }

    // ---- A folder on this computer ----------------------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.kind === "folder"
        spacing: Theme.space1
        Label { text: qsTr("Folder") }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BTextField {
                id: pathField
                objectName: "publishFolderPath"
                Layout.fillWidth: true
                leadingIcon: "folder"
                accessibleName: qsTr("Folder")
                placeholderText: qsTr("/srv/www/notes")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextEdited: dialog.touched = true
            }
            BButton {
                text: qsTr("Browse…")
                iconName: "folder-open"
                onClicked: folderPicker.open()
            }
        }
        Hint {
            text: qsTr("For a web server's folder, or one a sync app takes to a server. Hidden files there (.git, .htaccess) are never touched.")
        }
    }

    // ---- Every kind ----------------------------------------------------------------------------
    GridLayout {
        Layout.fillWidth: true
        columns: Theme.compact ? 1 : 2
        columnSpacing: Theme.space3
        rowSpacing: Theme.space1
        Label { text: qsTr("Site title (optional)") }
        Label { visible: !Theme.compact && dialog.kind !== "nextcloud"; text: qsTr("Site address (optional)") }
        BTextField {
            id: titleField
            Layout.fillWidth: true
            accessibleName: qsTr("Site title")
            placeholderText: Workspace.name
        }
        Label { visible: Theme.compact && dialog.kind !== "nextcloud"; text: qsTr("Site address (optional)") }
        BTextField {
            id: siteUrlField
            Layout.fillWidth: true
            visible: dialog.kind !== "nextcloud"
            leadingIcon: "globe"
            accessibleName: qsTr("Site address")
            placeholderText: dialog.kind === "github" ? qsTr("A custom domain") : "https://example.com/notes/"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onTextEdited: dialog.touched = true
        }
    }

    Rectangle {
        Layout.fillWidth: true
        visible: dialog.touched && dialog.problem !== ""
        implicitHeight: problemRow.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.warningBg
        RowLayout {
            id: problemRow
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "info"; size: Theme.iconSize; color: Theme.warningText }
            BText {
                Layout.fillWidth: true
                text: dialog.problem
                wrapMode: Text.Wrap
            }
        }
    }

    FilePicker {
        id: folderPicker
        title: qsTr("Choose the folder for the site")
        fileMode: FilePicker.OpenFolder
        dark: Theme.dark
        colors: Theme.colors
        onAccepted: {
            pathField.text = AppController.localPath(selectedFolder)
            dialog.touched = true
        }
    }
}
