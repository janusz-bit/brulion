pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellExtensionCopy — "Copy to another notebook" of an extension page (docs/EXTENSIONS.md
// "Sharing between notebooks"): the other notebooks this device knows (AppSettings.recentVaults,
// not the documentation); the page goes there at the same path, with the extension pages its code
// requires (b.require), approved there on this device (Workspace.extensions.copyToNotebook). A page
// there with other text is replaced only after a yes. The shell makes it
// (AppShell.openExtensionCopy(path)).
BDialog {
    id: dialog
    objectName: "extensionCopy"

    property string path
    /// The notebook whose page of that path has other text: "Replace" asks again with `replace`.
    property string conflictRoot
    property string conflictName

    title: qsTr("Copy “%1” to another notebook").arg(Workspace.pageTitle(dialog.path))
    subtitle: qsTr("With the extension pages its code requires; approved there on this device")
    iconName: "copy"
    showFooter: dialog.conflictRoot !== ""
    acceptText: qsTr("Replace")
    acceptVariant: "danger"

    function show(pagePath: string): void {
        dialog.path = pagePath
        dialog.conflictRoot = ""
        dialog.open()
    }
    function copyTo(root: string, name: string, replace: bool): void {
        const outcome = Workspace.extensions.copyToNotebook(dialog.path, root, replace)
        if (outcome === "") {
            dialog.conflictRoot = ""
            dialog.close()
        } else if (outcome === "conflict") {
            dialog.conflictRoot = root
            dialog.conflictName = name
        } else {
            AppController.toast(outcome, "error")
        }
    }
    onAcceptRequested: dialog.copyTo(dialog.conflictRoot, dialog.conflictName, true)

    readonly property string current: AppSettings.currentVault
    readonly property string docsPath: AppController.docsVaultPath
    readonly property list<string> vaultPaths: UiModels.stringFields(AppSettings.recentVaults, "path")
    readonly property int others: vaultPaths.length - (vaultPaths.indexOf(current) >= 0 ? 1 : 0)
                                  - (docsPath !== "" && vaultPaths.indexOf(docsPath) >= 0 ? 1 : 0)

    BEmptyState {
        Layout.fillWidth: true
        visible: dialog.others === 0
        compact: true
        iconName: "folder"
        title: qsTr("No other notebook")
        description: qsTr("Open another notebook once (Switch notebook): it is listed here then.")
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.conflictRoot === ""
        spacing: 0
        Repeater {
            model: AppSettings.recentVaults
            delegate: BListItem {
                id: notebook
                required property string path
                required property string name
                objectName: "extensionCopyTarget"
                Layout.fillWidth: true
                visible: path !== dialog.current && path !== dialog.docsPath
                text: name
                subtitle: path
                iconName: "folder"
                onClicked: dialog.copyTo(notebook.path, notebook.name, false)
            }
        }
    }

    BText {
        objectName: "extensionCopyConflict"
        Layout.fillWidth: true
        visible: dialog.conflictRoot !== ""
        wrapMode: Text.Wrap
        text: qsTr("“%1” has a page at %2 with other text. Replace it? Its text stays in that notebook's history.")
                  .arg(dialog.conflictName).arg(dialog.path)
    }
}
