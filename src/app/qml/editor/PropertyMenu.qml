pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// PropertyMenu — the menu of one property in the properties panel (PropertyRow: its type icon, a
// right-click or a long press on the row, the Menu key / Shift+F10): "Typ" (text, number,
// checkbox, date, date and time, list — the value converted; one that cannot be kept is cleared
// with "Cofnij" in the toast; tags and aliases keep theirs), "Zmień nazwę" (F2), "Kopiuj wartość",
// "Usuń" (Ctrl+Backspace; "Cofnij" in the toast). One menu per panel, created when first needed.
BMenu {
    id: menu

    property PropertyRow row: null
    readonly property bool editable: row !== null && row.editable

    /// The type `type` chosen (the item's check mark follows the row's type again: a checkable
    /// item's trigger sets it on its own).
    function choose(item: BMenuItem, type: string) {
        item.checked = Qt.binding(() => menu.row !== null && menu.row.type === type)
        if (row)
            row.setType(type)
    }

    /// The "Typ" submenu.
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
            checked: menu.row !== null && menu.row.type === "text"
            onTriggered: menu.choose(typeText, "text")
        }
        BMenuItem {
            id: typeNumber
            objectName: "propertyType_number"
            text: menu.row ? menu.row.typeName("number") : ""
            iconName: menu.row ? menu.row.typeIcon("number") : ""
            checkable: true
            checked: menu.row !== null && menu.row.type === "number"
            onTriggered: menu.choose(typeNumber, "number")
        }
        BMenuItem {
            id: typeCheckbox
            objectName: "propertyType_checkbox"
            text: menu.row ? menu.row.typeName("checkbox") : ""
            iconName: menu.row ? menu.row.typeIcon("checkbox") : ""
            checkable: true
            checked: menu.row !== null && menu.row.type === "checkbox"
            onTriggered: menu.choose(typeCheckbox, "checkbox")
        }
        BMenuItem {
            id: typeDate
            objectName: "propertyType_date"
            text: menu.row ? menu.row.typeName("date") : ""
            iconName: menu.row ? menu.row.typeIcon("date") : ""
            checkable: true
            checked: menu.row !== null && menu.row.type === "date"
            onTriggered: menu.choose(typeDate, "date")
        }
        BMenuItem {
            id: typeDatetime
            objectName: "propertyType_datetime"
            text: menu.row ? menu.row.typeName("datetime") : ""
            iconName: menu.row ? menu.row.typeIcon("datetime") : ""
            checkable: true
            checked: menu.row !== null && menu.row.type === "datetime"
            onTriggered: menu.choose(typeDatetime, "datetime")
        }
        BMenuItem {
            id: typeList
            objectName: "propertyType_list"
            text: menu.row ? menu.row.typeName("list") : ""
            iconName: menu.row ? menu.row.typeIcon("list") : ""
            checkable: true
            checked: menu.row !== null && menu.row.type === "list"
            onTriggered: menu.choose(typeList, "list")
        }
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
