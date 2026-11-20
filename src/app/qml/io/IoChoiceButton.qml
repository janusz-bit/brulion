pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// IoChoiceButton — a small button showing the chosen option; a click opens a menu of all of them
// (the import's CSV mapping: a column's type, the title column, the encoding…).
//
//   IoChoiceButton {
//       model: importController.csvTypeChoices          // [{ value, text }]
//       currentValue: "auto"
//       accessibleName: qsTr("Type of “Rok”")
//       onActivated: (value) => …
//   }
BButton {
    id: button

    property list<var> model
    property string currentValue
    property string accessibleName
    /// Shown when no option has currentValue.
    property string fallbackText

    signal activated(string value)

    function currentText(): string {
        for (let i = 0; i < button.model.length; ++i) {
            if (AppController.routeText(button.model[i], "value") === button.currentValue)
                return AppController.routeText(button.model[i], "text")
        }
        return button.fallbackText !== "" ? button.fallbackText : button.currentValue
    }

    text: button.currentText()
    iconName: "chevron-down"
    iconRight: true
    size: "small"
    Accessible.name: button.accessibleName !== "" ? button.accessibleName + ": " + button.text : button.text
    onClicked: menu.popup(button, 0, button.height)

    BMenu {
        id: menu
    }
    Instantiator {
        model: button.model
        delegate: BMenuItem {
            id: item
            required property var modelData
            readonly property string value: AppController.routeText(modelData, "value")
            text: AppController.routeText(modelData, "text")
            checkable: true
            // (A Binding: a click toggles `checked`, which would end a plain binding.)
            Binding on checked {
                value: item.value === button.currentValue
            }
            onTriggered: {
                button.activated(item.value)
                item.checked = item.value === button.currentValue
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object as Item)
        onObjectRemoved: (index, object) => menu.removeItem(object as Item)
    }
}
