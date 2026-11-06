pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PropertyMenu — the menu of one property in the properties panel (PropertyRow: its type icon, a
// right-click or a long press on the row, the Menu key / Shift+F10): "Type" (text, number,
// checkbox, date, date and time, list — the value converted; one that cannot be kept is cleared
// with "Undo" in the toast; tags and aliases keep theirs), "Rename" (F2), "Copy value",
// "Remove" (Ctrl+Backspace; "Undo" in the toast); a row the fields cannot edit (a map, a nested
// list, a block Brulion cannot edit safely) offers "Edit as text" (the block's raw text in
// its inline editor). One menu per panel, created when first needed.
BMenu {
    id: menu

    property PropertyRow row: null
    readonly property bool editable: row !== null && row.editable

    /// Bumped when a type was chosen: the click toggled that item's check mark, its binding (the
    /// row's type) takes over again.
    property int checkRevision: 0
    /// The type `type` chosen.
    function choose(type: string): void {
        if (row)
            row.setType(type)
        ++menu.checkRevision
    }

    /// The "Type" submenu.
    readonly property alias typeMenu: typeSubMenu

    BMenu {
        id: typeSubMenu
        objectName: "propertyTypeMenu"
        title: qsTr("Type")
        iconName: "type"
        enabled: menu.editable && !menu.row.fixedType
        BMenuItem {
            id: typeText
            objectName: "propertyType_text"
            text: menu.row ? menu.row.typeName("text") : ""
            iconName: menu.row ? menu.row.typeIcon("text") : ""
            checkable: true
            checked: menu.checkRevision >= 0 && menu.row !== null && menu.row.type === "text"
            onTriggered: menu.choose("text")
        }
        BMenuItem {
            id: typeNumber
            objectName: "propertyType_number"
            text: menu.row ? menu.row.typeName("number") : ""
            iconName: menu.row ? menu.row.typeIcon("number") : ""
            checkable: true
            checked: menu.checkRevision >= 0 && menu.row !== null && menu.row.type === "number"
            onTriggered: menu.choose("number")
        }
        BMenuItem {
            id: typeCheckbox
            objectName: "propertyType_checkbox"
            text: menu.row ? menu.row.typeName("checkbox") : ""
            iconName: menu.row ? menu.row.typeIcon("checkbox") : ""
            checkable: true
            checked: menu.checkRevision >= 0 && menu.row !== null && menu.row.type === "checkbox"
            onTriggered: menu.choose("checkbox")
        }
        BMenuItem {
            id: typeDate
            objectName: "propertyType_date"
            text: menu.row ? menu.row.typeName("date") : ""
            iconName: menu.row ? menu.row.typeIcon("date") : ""
            checkable: true
            checked: menu.checkRevision >= 0 && menu.row !== null && menu.row.type === "date"
            onTriggered: menu.choose("date")
        }
        BMenuItem {
            id: typeDatetime
            objectName: "propertyType_datetime"
            text: menu.row ? menu.row.typeName("datetime") : ""
            iconName: menu.row ? menu.row.typeIcon("datetime") : ""
            checkable: true
            checked: menu.checkRevision >= 0 && menu.row !== null && menu.row.type === "datetime"
            onTriggered: menu.choose("datetime")
        }
        BMenuItem {
            id: typeList
            objectName: "propertyType_list"
            text: menu.row ? menu.row.typeName("list") : ""
            iconName: menu.row ? menu.row.typeIcon("list") : ""
            checkable: true
            checked: menu.checkRevision >= 0 && menu.row !== null && menu.row.type === "list"
            onTriggered: menu.choose("list")
        }
    }
    BMenuItem {
        objectName: "propertyEditAsText"
        visible: menu.row !== null && !menu.editable
        height: visible ? implicitHeight : 0
        text: qsTr("Edit as text")
        iconName: "code"
        onTriggered: if (menu.row) menu.row.editor.startEditing(menu.row.blockRow, -1)
    }
    BMenuItem {
        objectName: "propertyRename"
        text: qsTr("Rename")
        iconName: "pencil"
        shortcut: "F2"
        enabled: menu.editable
        onTriggered: if (menu.row) menu.row.startRename()
    }
    BMenuItem {
        objectName: "propertyCopy"
        text: qsTr("Copy value")
        iconName: "copy"
        enabled: menu.row !== null && !menu.row.empty
        onTriggered: if (menu.row) menu.row.copyValue()
    }
    BMenuSeparator {}
    BMenuItem {
        objectName: "propertyRemove"
        text: qsTr("Remove")
        iconName: "trash-2"
        shortcut: "Ctrl+Backspace"
        danger: true
        enabled: menu.editable
        onTriggered: if (menu.row) menu.row.remove()
    }
}
