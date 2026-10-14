pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// BSearchField — BTextField with a search icon, a clear button and a debounced search signal.
//
//   BSearchField { placeholderText: qsTr("Szukaj w notatkach…"); shortcut: "Ctrl+Shift+F"
//                  onSearch: (query) => model.query = query }
//
// search(query) fires `debounce` ms after typing stops (0 = immediately) and at once on
// Enter. Escape clears the text; with an empty field it emits escapePressed() and lets the
// key through (an enclosing popup closes). `shortcut` shows a key-cap hint while the field is empty (not on touch UIs).
BTextField {
    id: control

    property int debounce: 150
    property string shortcut

    signal search(string query)

    leadingIcon: "search"
    clearable: true
    inputMethodHints: Qt.ImhNoPredictiveText
    Accessible.role: Accessible.EditableText

    trailingItem: BKbd {
        sequence: control.shortcut
        small: true
        visible: control.shortcut !== "" && control.length === 0 && !control.activeFocus && !Theme.touchUi
    }

    onTextEdited: {
        if (debounce > 0)
            debounceTimer.restart()
        else
            search(text)
    }
    onCleared: {
        debounceTimer.stop()
        search("")
    }
    onAccepted: {
        debounceTimer.stop()
        search(text)
    }

    Timer {
        id: debounceTimer
        interval: control.debounce
        onTriggered: control.search(control.text)
    }
}
