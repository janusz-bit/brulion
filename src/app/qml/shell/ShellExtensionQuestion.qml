pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQml.Models
import Brulion

// ShellExtensionQuestion — the question an extension's command or action waits on
// (Workspace.extensions, docs/EXTENSIONS.md "Questions"): b.ask a line of text, b.choose one of a
// list, b.form labelled fields — numbers with steppers (the gym's weight and reps) and text fields
// (kind "text": a habit's name and schedule, with an example as the placeholder). Cancelling gives the extension ""
// (ask) or nil (choose, form), and its command ends itself. A question that follows right after
// (choose a plan, then a form) opens the dialog again. A question of the AI chat's turn
// (Workspace.extensions.questionChat) is asked in the conversation while a chat view shows it
// (Workspace.chats.isShown); the dialog asks it once none does. The shell makes it with the first
// question (AppShell.extensionQuestion).
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

    /// The question's first line is the title; the lines after it (b.choose's preview of a change
    /// the AI assistant wants to make, say) show under it, as plain text: they quote notes.
    readonly property string questionText: extensions.questionTitle
    readonly property int titleEnd: Js.indexOf(questionText, "\n")
    readonly property string details: titleEnd >= 0 ? Js.trim(Js.sliceFrom(questionText, titleEnd + 1)) : ""
    title: titleEnd >= 0 ? Js.slice(questionText, 0, titleEnd) : questionText
    iconName: "circle-help"
    showFooter: kind !== "choose"
    acceptText: kind === "form" ? qsTr("Save") : qsTr("OK")
    acceptEnabled: kind !== "ask" || Js.trim(textField.typedText) !== ""
    initialFocusItem: kind === "ask" ? textField : dialog.formFocus
    /// A form's first text field: it has the focus when the dialog opens.
    property Item formFocus: null

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

    /// The question is a turn's of a conversation that a chat view shows: it is answered there.
    function inChat(): bool {
        const conversation = dialog.extensions.questionChat
        return conversation !== "" && Workspace.chats.isShown(conversation)
    }

    Later {
        id: openLater
        onTriggered: {
            if (dialog.extensions.questionKind === "" || dialog.inChat())
                return
            textField.text = ""
            const first = dialog.extensions.questionKind === "form" ? dialog.extensions.questionFields.firstTextField() : -1
            const row = first >= 0 ? formFields.itemAt(first) as FormTextRow : null
            dialog.formFocus = row ? row.input : null
            dialog.shownSerial = dialog.extensions.questionSerial
            dialog.open()
        }
    }
    /// A question came or went (Workspace.extensions.questionChanged; the shell calls it for the
    /// question it makes this dialog with).
    function questionChanged(): void {
        // (Answered elsewhere — in the chat —: the dialog of it goes, asking nothing.)
        if (dialog.opened && dialog.shownSerial !== dialog.extensions.questionSerial) {
            dialog.shownSerial = -1
            dialog.close()
        }
        if (dialog.extensions.questionKind !== "")
            openLater.restart() // after the dialog that answered the last one has closed
    }
    Connections {
        target: dialog.extensions
        function onQuestionChanged() { dialog.questionChanged() }
    }
    Connections {
        target: Workspace.chats
        function onShownChanged() {
            // The chat that showed the question went (another view, the panel closed): asked here.
            if (dialog.extensions.questionKind !== "" && !dialog.opened && !dialog.inChat())
                openLater.restart()
        }
    }

    Rectangle {
        objectName: "extensionQuestionDetails"
        Layout.fillWidth: true
        visible: dialog.details !== ""
        implicitHeight: detailsText.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.bgSunken
        BText {
            id: detailsText
            x: Theme.space3
            y: Theme.space3
            width: parent.width - 2 * Theme.space3
            text: dialog.details
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            // Everything the assistant asks a yes for is shown (it sends at most 40 lines of 600
            // characters and says when that is a beginning): the dialog scrolls. The limit is
            // for an extension that asks with a text of any length.
            maximumLineCount: 120
            elide: Text.ElideRight
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

    /// A form's text field (kind "text"): its label above it; what is typed is its answer.
    component FormTextRow: ColumnLayout {
        id: textRow
        required property int index
        required property string fieldLabel
        required property string fieldText
        required property string fieldPlaceholder
        readonly property BTextField input: textInput
        Layout.fillWidth: true
        visible: dialog.kind === "form"
        spacing: Theme.space1
        BText {
            Layout.fillWidth: true
            text: textRow.fieldLabel
            variant: "label"
            elide: Text.ElideRight
        }
        BTextField {
            id: textInput
            objectName: "extensionQuestionField"
            Layout.fillWidth: true
            placeholderText: textRow.fieldPlaceholder
            accessibleName: textRow.fieldLabel
            Component.onCompleted: textInput.text = textRow.fieldText
            onTextChanged: dialog.extensions.questionFields.setText(textRow.index, textInput.text)
            onAccepted: dialog.tryAccept()
        }
    }

    Repeater {
        id: formFields
        model: dialog.extensions.questionFields
        delegate: DelegateChooser {
            role: "fieldKind"
            DelegateChoice {
                roleValue: "text"
                FormTextRow {}
            }
            DelegateChoice {
                NumberRow {}
            }
        }
    }

    /// A form's number: − / + and typing ("82,5", "82.5", "82,5 kg").
    component NumberRow: RowLayout {
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
