pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellExtensionQuestion — the question an extension's command or action waits on
// (Workspace.extensions, docs/EXTENSIONS.md "Questions"): b.ask a line of text, b.choose one of a
// list, b.form numbers with steppers (the gym's weight and reps). Cancelling gives the extension ""
// (ask) or nil (choose, form), and its command ends itself. A question that follows right after
// (choose a plan, then a form) opens the dialog again.
BDialog {
    id: dialog
    objectName: "extensionQuestion"

    readonly property WorkspaceExtensions extensions: Workspace.extensions
    readonly property string kind: extensions.questionKind
    /// The question shown (Workspace.extensions.questionSerial): a late close cancels only it.
    property int shownSerial: -1

    function choose(option: string): void {
        dialog.shownSerial = -1
        dialog.close()
        dialog.extensions.answerChoice(option)
    }

    title: extensions.questionTitle
    iconName: "circle-help"
    showFooter: kind !== "choose"
    acceptText: kind === "form" ? qsTr("Save") : qsTr("OK")
    acceptEnabled: kind !== "ask" || Js.trim(textField.typedText) !== ""
    initialFocusItem: kind === "ask" ? textField : null

    onAccepted: {
        const serial = dialog.shownSerial
        dialog.shownSerial = -1
        if (serial !== dialog.extensions.questionSerial)
            return
        if (dialog.kind === "ask")
            dialog.extensions.answerText(textField.typedText)
        else if (dialog.kind === "form")
            dialog.extensions.answerForm()
    }
    onRejected: {
        if (dialog.shownSerial >= 0 && dialog.shownSerial === dialog.extensions.questionSerial)
            dialog.extensions.cancelQuestion()
        dialog.shownSerial = -1
    }

    Later {
        id: openLater
        onTriggered: {
            if (dialog.extensions.questionKind === "")
                return
            textField.text = ""
            dialog.shownSerial = dialog.extensions.questionSerial
            dialog.open()
        }
    }
    Connections {
        target: dialog.extensions
        function onQuestionChanged() {
            if (dialog.extensions.questionKind !== "")
                openLater.restart() // after the dialog that answered the last one has closed
        }
    }

    BTextField {
        id: textField
        objectName: "extensionQuestionText"
        Layout.fillWidth: true
        visible: dialog.kind === "ask"
        onAccepted: {
            if (Js.trim(textField.typedText) !== "")
                dialog.tryAccept()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.kind === "choose"
        spacing: 0
        Repeater {
            model: dialog.extensions.questionOptions
            delegate: BListItem {
                id: option
                required property string modelData
                objectName: "extensionQuestionOption"
                Layout.fillWidth: true
                text: option.modelData
                iconName: "chevron-right"
                onClicked: dialog.choose(option.modelData)
            }
        }
    }

    Repeater {
        model: dialog.extensions.questionFields
        delegate: RowLayout {
            id: field
            required property int index
            required property string fieldLabel
            required property string fieldDisplay
            required property string fieldUnit
            Layout.fillWidth: true
            visible: dialog.kind === "form"
            spacing: Theme.space2
            BText {
                Layout.fillWidth: true
                text: field.fieldLabel
                variant: "label"
                elide: Text.ElideRight
            }
            BIconButton {
                label: qsTr("Less")
                iconName: "minus"
                onClicked: dialog.extensions.questionFields.step(field.index, -1)
            }
            BTextField {
                objectName: "extensionQuestionNumber"
                Layout.preferredWidth: 104
                text: field.fieldDisplay
                horizontalAlignment: TextInput.AlignHCenter
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                accessibleName: field.fieldLabel
                onEditingFinished: dialog.extensions.questionFields.setNumberText(field.index, text)
            }
            BText {
                Layout.preferredWidth: 28
                text: field.fieldUnit
                variant: "muted"
            }
            BIconButton {
                label: qsTr("More")
                iconName: "plus"
                onClicked: dialog.extensions.questionFields.step(field.index, 1)
            }
        }
    }
}
