pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ShellContributionItems — the items extensions put into a menu (b.app.contribute; docs/UI.md
// "Extension items in the UI"): one BMenuItem per item of `slot` ("pageMenu", "blockMenu", the
// top bar's "toolbar" on phones), inserted into `menu` right after `after` (an item of the menu;
// null: at its top), in the items' order. They follow Workspace.extensions.appRevision: an item an
// extension or an agent adds, changes or removes shows at once, also in an open menu — and only
// then are they made again (WorkspaceExtensions.contributionsKey). `shown` false: none; `count` how
// many there are (a separator around them). A click emits picked(extension, id); the owner runs it
// with its context (AppScripting.activate(slot, extension, id, path, line): the page, the block's
// saved line).
// Each item is a plain BMenuItem (the menu's keyboard rules hold, BMenu): the label, the icon
// (else "puzzle"), the extension's title as its description — never `danger`: what deletes is the
// app's own last item of a menu.
//
//   BMenuSeparator { id: extensionsAt }
//   ShellContributionItems {
//       menu: moreMenu; after: extensionsAt; slot: "pageMenu"; shown: bar.onPage
//       onPicked: (extension, id) => AppScripting.activate("pageMenu", extension, id, bar.pagePath, -1)
//   }
Instantiator {
    id: items

    property BMenu menu: null
    /// The menu's item the new ones follow (null: the first ones of the menu).
    property Item after: null
    property string slot
    property bool shown: true

    /// An item was clicked (Enter, a tap): `extension`'s item `id`.
    signal picked(string extension, string id)

    readonly property list<var> noRows: []
    /// The slot's items as text: it changes (and the items are made again) only when they do.
    readonly property string slotKey: items.shown && Workspace.extensions !== null
                                      ? Workspace.extensions.contributionsKey(items.slot, Workspace.extensions.appRevision) : ""
    model: items.slotKey !== "" ? Workspace.extensions.contributions(items.slot, items.slotKey.length) : items.noRows

    delegate: BMenuItem {
        id: entry
        // (A row of WorkspaceExtensions.contributions(): its `id` and `icon` read through modelData —
        // a delegate's property cannot have those names.)
        required property var modelData
        required property string extension
        required property string label
        required property string title
        readonly property string itemId: UiModels.stringField(entry.modelData, "id")
        objectName: "contribution_" + items.slot + "_" + entry.itemId
        text: entry.label
        iconName: UiModels.stringField(entry.modelData, "icon") !== "" ? UiModels.stringField(entry.modelData, "icon") : "puzzle"
        hint: Theme.touchUi ? "" : entry.title
        onTriggered: items.picked(entry.extension, entry.itemId)
    }

    /// The index in `menu` where the item `index` of this list goes.
    function insertAt(index: int): int {
        if (!items.menu || items.after === null)
            return index
        for (let i = 0; i < items.menu.count; ++i) {
            if (items.menu.itemAt(i) === items.after)
                return i + 1 + index
        }
        return items.menu.count
    }
    onObjectAdded: (index, object) => {
        if (items.menu)
            items.menu.insertItem(items.insertAt(index), object as Item)
    }
    // (takeItem: the Instantiator destroys its objects itself.)
    onObjectRemoved: (index, object) => {
        if (!items.menu)
            return
        for (let i = 0; i < items.menu.count; ++i) {
            if (items.menu.itemAt(i) === object as Item) {
                items.menu.takeItem(i)
                return
            }
        }
    }
}
