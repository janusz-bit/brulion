pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitPublishDialog — "Publish to GitHub…" for a repository without a remote (T3 Code's Publish
// repository): its name (the folder's by default, "owner/name" for an organisation) and whether it
// is private or public; CodeGit.publish runs `gh repo create <name> --private|--public --source
// <folder> --remote origin --push` — actionFinished("publish", ok, message, its address).
//
//   GitPublishDialog { id: publishDialog; git: panel.git }   …   publishDialog.openFor()
BDialog {
    id: dialog
    objectName: "gitPublishDialog"

    property CodeGit git: null
    /// "private" | "public".
    property string visibility: "private"
    readonly property list<var> visibilities: [
        { value: "private", text: qsTr("Private"), icon: "lock" },
        { value: "public", text: qsTr("Public"), icon: "globe" }
    ]

    /// The work tree's folder name, as a repository's name.
    function folderName(): string {
        if (dialog.git === null)
            return ""
        const top = dialog.git.topLevel
        const slash = Js.lastIndexOf(top, "/")
        const name = Js.sliceFrom(top, slash + 1)
        return Js.replaceRegExp(name, "[^A-Za-z0-9._-]+", "g", "-")
    }
    function openFor(): void {
        nameField.text = dialog.folderName()
        dialog.visibility = "private"
        dialog.open()
    }

    title: qsTr("Publish to GitHub")
    subtitle: qsTr("A new repository on GitHub, added as the remote “origin” and pushed, with the GitHub CLI.")
    iconName: "upload"
    acceptText: qsTr("Publish")
    acceptEnabled: dialog.git !== null && !dialog.git.busy && dialog.git.isValidRepositoryName(nameField.typedText)
    initialFocusItem: nameField
    onAccepted: dialog.git.publish(nameField.text, dialog.visibility === "public")

    BText {
        Layout.fillWidth: true
        text: qsTr("Name")
        variant: "label"
    }
    BTextField {
        id: nameField
        objectName: "gitPublishName"
        Layout.fillWidth: true
        accessibleName: qsTr("Repository name")
        placeholderText: qsTr("my-project, or organisation/my-project")
        errorText: nameField.typedText !== "" && dialog.git !== null && !dialog.git.isValidRepositoryName(nameField.typedText)
                   ? qsTr("Letters a–z, digits, “.”, “_” and “-” only") : ""
        onAccepted: dialog.tryAccept()
    }
    BSegmented {
        objectName: "gitPublishVisibility"
        Layout.topMargin: Theme.space2
        accessibleName: qsTr("Who can see it")
        model: dialog.visibilities
        currentValue: dialog.visibility
        onActivated: (value) => dialog.visibility = "" + value
    }
}
