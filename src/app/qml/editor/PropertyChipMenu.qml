pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PropertyChipMenu — the menu of a chip of the page's properties: a tag, an alias or an item of a
// list. One menu for the page header's chips (ContentPageHeader) and the Właściwości panel's
// (PropertyListValue), so both read alike (docs/UI.md "Page properties"): "Otwórz stronę tagu"
// (a tag) or "Otwórz" (a list item that is a [[Page]]), "Kopiuj" (aliases and list items),
// "Usuń z tej strony" / "Usuń alias" / "Usuń „…”" (not on a value that cannot be edited). The
// owner acts on the signals; removedText() is the toast's text after a removal (the owner adds
// "Cofnij"), also for a removal without the menu (a key on a chip).
//
//   PropertyChipMenu { kind: "tags"; item: "dom"; onRemoveRequested: … }
BMenu {
    id: menu

    /// "tags", "aliases" or "list".
    property string kind
    /// The chip's item: a tag without "#", an alias, a list item's text.
    property string item
    /// What the chip shows for the item (a [[Page]] its page's name), for "Usuń „…”".
    property string display: item
    /// A list item that opens something (a [[Page]]).
    property bool opensItem: false
    property bool readOnly: false

    signal openRequested()
    signal copyRequested()
    signal removeRequested()

    /// The toast after `item` (shown as `display`) of `kind` was removed.
    function removedText(kind: string, item: string, display: string): string {
        return kind === "tags" ? qsTr("Removed tag #%1 from this page").arg(item)
             : kind === "aliases" ? qsTr("Removed alias “%1”").arg(item)
             : qsTr("Removed “%1”").arg(display)
    }

    BMenuItem {
        objectName: "propertyChipOpen"
        visible: menu.kind === "tags" || menu.opensItem
        height: visible ? implicitHeight : 0
        text: menu.kind === "tags" ? qsTr("Open tag page") : qsTr("Open")
        iconName: menu.kind === "tags" ? "hash" : "file-text"
        onTriggered: menu.openRequested()
    }
    BMenuItem {
        objectName: "propertyChipCopy"
        visible: menu.kind !== "tags"
        height: visible ? implicitHeight : 0
        text: qsTr("Copy")
        iconName: "copy"
        onTriggered: menu.copyRequested()
    }
    BMenuItem {
        objectName: "propertyChipRemove"
        visible: !menu.readOnly
        height: visible ? implicitHeight : 0
        text: menu.kind === "tags" ? qsTr("Remove from this page")
            : menu.kind === "aliases" ? qsTr("Remove alias")
            : qsTr("Remove “%1”").arg(menu.display)
        iconName: "x"
        danger: true
        onTriggered: menu.removeRequested()
    }
}
