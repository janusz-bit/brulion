pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitBasePicker — the branch a pull request goes into, in the commit and pull request dialogs: a
// button with the base ("" the repository's default branch, CodeGit.defaultBranch) and a menu of
// the branches (CodeGit.branchList after branches(); a remote-tracking one as the branch's name on
// the remote: "origin/rozwój" → "rozwój").
//
//   GitBasePicker { git: repo; base: dialog.prBase; onPicked: (name) => dialog.prBase = name }
RowLayout {
    id: picker

    property CodeGit git: null
    /// The base chosen ("" the default branch).
    property string base
    readonly property string shown: picker.base !== "" ? picker.base
                                  : picker.git !== null && picker.git.defaultBranch !== "" ? picker.git.defaultBranch
                                  : qsTr("the default branch")

    signal picked(string name)

    /// The branch's name on the remote for a remote-tracking one.
    function baseOf(name: string, remote: bool): string {
        if (!remote)
            return name
        const slash = Js.indexOf(name, "/")
        return slash >= 0 ? Js.sliceFrom(name, slash + 1) : name
    }

    spacing: Theme.space2
    BText {
        text: qsTr("Into")
        variant: "label"
    }
    BButton {
        id: button
        objectName: "gitBasePickerButton"
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        size: "small"
        iconName: "chevron-down"
        iconRight: true
        text: picker.shown
        Accessible.name: qsTr("Base branch: %1").arg(picker.shown)
        onClicked: {
            if (picker.git !== null)
                picker.git.branches()
            menu.popup(button, 0, button.height)
        }
    }

    BMenu {
        id: menu
        objectName: "gitBasePickerMenu"
        BMenuItem {
            objectName: "gitBasePickerDefault"
            text: picker.git !== null && picker.git.defaultBranch !== ""
                  ? qsTr("Default branch (%1)").arg(picker.git.defaultBranch) : qsTr("Default branch")
            checkable: true
            Binding on checked {
                value: picker.base === ""
            }
            onTriggered: picker.picked("")
        }
        BMenuSeparator {}
        Instantiator {
            model: picker.git !== null ? picker.git.branchList : picker.noRows
            delegate: BMenuItem {
                id: item
                required property string name
                required property bool remote
                required property bool current
                required property int index
                readonly property string base: picker.baseOf(item.name, item.remote)
                objectName: "gitBasePickerItem_" + item.index
                text: item.name
                iconName: item.remote ? "cloud" : "git-branch"
                checkable: true
                Binding on checked {
                    value: picker.base !== "" && picker.base === item.base
                }
                onTriggered: picker.picked(item.base)
            }
            onObjectAdded: (index, object) => menu.insertItem(index + 2, object as Item)
            onObjectRemoved: (index, object) => menu.removeItem(object as Item)
        }
    }
    readonly property list<var> noRows: []
}
