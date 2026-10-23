pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellQuickCapture — jot something down without leaving the current view: the text becomes a
// new block at the end of today's journal (AppController.capture). Ctrl+Enter adds it.
BDialog {
    id: dialog

    /// Emitted after the text was saved (the shell shows a toast with "Open").
    signal captured()

    title: qsTr("Quick capture")
    subtitle: qsTr("Goes to the end of today's journal")
    iconName: "notebook-pen"
    acceptText: qsTr("Add")
    acceptEnabled: field.typedText.trim() !== ""
    initialFocusItem: field

    onAboutToShow: field.text = ""
    onAccepted: {
        if (AppController.capture(field.text))
            dialog.captured()
    }

    BTextArea {
        id: field
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.touchUi ? 140 : 120
        placeholderText: qsTr("What's on your mind?")
        wrapMode: TextEdit.Wrap
    }
    BText {
        Layout.fillWidth: true
        visible: !Theme.touchUi
        text: qsTr("Ctrl+Enter adds the note. Tasks work too: “- [ ] call the plumber 📅 tomorrow”.")
        variant: "faint"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }
}
