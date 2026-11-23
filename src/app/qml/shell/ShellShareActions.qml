pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellShareActions — the extensions' share actions (b.app.contribute("share", …), docs/UI.md
// "Extension items in the UI"): under a "With an extension" label, one button per item (its icon,
// else "puzzle"; its label), in the items' order. In both share dialogs: "Save to Brulion"
// (ShellShareDialog: what another app shared) and "Export page" (ContentExportDialog: a page shared
// from Brulion). Hidden while there is none. A
// click emits picked(extension, id): the dialog runs it (AppScripting.activate("share", …), the
// command gets ctx.shared) and closes. They follow Workspace.extensions.appRevision, as every
// slot's items do.
//
//   ShellShareActions {
//       Layout.fillWidth: true
//       onPicked: (extension, id) => dialog.runShareAction(extension, id)
//   }
ColumnLayout {
    id: actions

    /// An action was clicked: `extension`'s item `id`.
    signal picked(string extension, string id)

    readonly property list<var> noRows: []
    readonly property string itemsKey: Workspace.extensions !== null
                                       ? Workspace.extensions.contributionsKey("share", Workspace.extensions.appRevision) : ""
    readonly property list<var> items: actions.itemsKey !== ""
                                       ? Workspace.extensions.contributions("share", actions.itemsKey.length) : actions.noRows
    readonly property int count: actions.items.length

    visible: actions.count > 0
    spacing: Theme.space2

    BText {
        Layout.fillWidth: true
        text: qsTr("With an extension")
        variant: "label"
    }
    Repeater {
        model: actions.items
        delegate: BButton {
            id: action
            // (A row of WorkspaceExtensions.contributions(): its `id` and `icon` read through
            // modelData — a delegate's property cannot have those names.)
            required property var modelData
            required property string extension
            required property string label
            readonly property string itemId: UiModels.stringField(action.modelData, "id")
            readonly property string glyph: UiModels.stringField(action.modelData, "icon")
            objectName: "contribution_share_" + action.itemId
            Layout.fillWidth: true
            iconName: action.glyph !== "" ? action.glyph : "puzzle"
            text: action.label
            onClicked: actions.picked(action.extension, action.itemId)
        }
    }
}
