pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// CanvasTextDialog — one line asked for on a canvas (CanvasView; docs/UI.md "Canvas view"): a web
// address for a new link card or a link card's own, a group's label, an edge's label. `purpose`
// tells the view what the text is for when it is accepted (chosen(purpose, target, text)).
BDialog {
    id: dialog

    /// "link" (a new link card), "url" (a link card's address), "group", "edge".
    property string purpose
    /// The node or edge the text is for ("" for a new card).
    property string target
    property string placeholder

    signal chosen(string purpose, string target, string text)

    function ask(purpose: string, target: string, title: string, text: string, placeholder: string): void {
        dialog.purpose = purpose
        dialog.target = target
        dialog.title = title
        dialog.placeholder = placeholder
        field.text = text
        dialog.open()
        field.selectAll()
    }

    acceptText: qsTr("OK")
    acceptEnabled: dialog.purpose === "group" || dialog.purpose === "edge" || Js.trim(field.text) !== ""
    initialFocusItem: field
    onAccepted: dialog.chosen(dialog.purpose, dialog.target, dialog.purpose === "link" || dialog.purpose === "url"
                              ? Js.trim(field.text) : field.text)

    BTextField {
        id: field
        objectName: "canvasTextDialogField"
        Layout.fillWidth: true
        placeholderText: dialog.placeholder
        inputMethodHints: dialog.purpose === "link" || dialog.purpose === "url" ? Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                                                                                : Qt.ImhNone
        onAccepted: dialog.accept()
    }
}
