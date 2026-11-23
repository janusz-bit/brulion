pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellAiReportDialog — "Report response" (docs/UI.md "AI chat"): an answer of the AI the user finds
// offensive, harmful or wrong reported to Brulion's developers without leaving the app. The user
// picks a reason, may add a comment, and sees what the report holds — the answer (editable: the
// user may shorten it or take a private part out), the model and the app; the question before it
// only when the box is ticked — and where it goes: Send report opens the report as an email in the
// mail app (a build that names an address: BRULION_AI_REPORT_EMAIL) or as a new public issue on
// GitHub in the browser, which the user sends there (AiReportDraft: core aireport). Brulion sends
// nothing itself. "Hide this response" (the chat) folds the answer without sending anything.
//
//   ShellAiReportDialog { id: report; onSent: (n) => chats.setFlag(path, n, "reported") }
//   report.openFor("chat", answer, question, Workspace.chats.modelName(path), number, true)
BDialog {
    id: dialog
    objectName: "aiReportDialog"

    /// The message reported (a chat's: its number in the page, from 1); 0 none.
    property int number: 0
    /// "Hide this response" is offered (a chat's message: the view folds it).
    property bool canHide: false
    /// The box that adds the question, and what it says (the writing assistant: the text it
    /// worked on).
    property string questionLabel: qsTr("Add my question before it")
    property string questionNote: qsTr("Off: your question and the rest of the conversation stay out of the report.")
    /// The report's whole text is shown.
    property bool previewShown: false
    readonly property AiReportDraft report: draft

    /// Send report opened the link (a chat marks its message reported).
    signal sent(int number)
    /// Hide this response: nothing is sent.
    signal hideChosen(int number)

    /// Opens the dialog for an answer: `where` what showed it ("chat", "writing/rewrite/shorter"),
    /// `response` its text, `question` the question before it ("" none), `model` who answered.
    function openFor(where: string, response: string, question: string, model: string, number: int, canHide: bool): void {
        draft.reason = ""
        draft.where = where
        draft.model = model
        dialog.number = number
        dialog.canHide = canHide
        dialog.previewShown = false
        commentField.text = ""
        responseField.text = response
        questionField.text = question
        questionBox.checked = false
        dialog.open()
    }

    /// Scrolls `scroll` so the caret's rectangle `r` is in view.
    function keepCaret(scroll: Flickable, r: rect): void {
        if (r.y < scroll.contentY)
            scroll.contentY = r.y
        else if (r.y + r.height > scroll.contentY + scroll.height)
            scroll.contentY = r.y + r.height - scroll.height
    }

    AiReportDraft {
        id: draft
        comment: commentField.text
        response: responseField.text
        question: questionField.text
        includeQuestion: questionBox.checked
    }

    title: qsTr("Report response")
    subtitle: qsTr("Tell Brulion's developers about an answer of the AI that is offensive, harmful or wrong.")
    iconName: "flag"
    preferredWidth: 600
    acceptText: qsTr("Send report")
    acceptEnabled: draft.ready
    onAccepted: {
        const number = dialog.number
        Workspace.openUrl(draft.url)
        AppController.toast(draft.byEmail ? qsTr("The report is open in your email app: send it there.")
                                          : qsTr("The report is open on GitHub: send it there."), "success")
        dialog.sent(number)
    }

    // Why.
    BText {
        Layout.fillWidth: true
        text: qsTr("What is wrong with it?")
        variant: "label"
    }
    Flow {
        objectName: "aiReportReasons"
        Layout.fillWidth: true
        spacing: Theme.space2
        Repeater {
            model: draft.reasons
            delegate: BChip {
                id: reasonChip
                required property string modelData
                objectName: "aiReportReason_" + reasonChip.modelData
                text: draft.reasonLabel(reasonChip.modelData)
                interactive: true
                selected: draft.reason === reasonChip.modelData
                iconName: "check"
                showIcon: reasonChip.selected
                Accessible.role: Accessible.RadioButton
                Accessible.checkable: true
                Accessible.checked: reasonChip.selected
                onClicked: draft.reason = reasonChip.modelData
            }
        }
    }
    BTextArea {
        id: commentField
        objectName: "aiReportComment"
        Layout.fillWidth: true
        placeholderText: qsTr("A comment (optional)")
    }

    // What the report holds: the answer as the user leaves it, the question only when asked for,
    // who answered and the app.
    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        text: qsTr("What the report holds")
        variant: "label"
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("The response, as you leave it here — shorten it or take out what is private:")
        variant: "caption"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    // (A long answer scrolls in its field, the caret kept in view.)
    Flickable {
        id: responseScroll
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(responseField.implicitHeight, 64), dialog.sheet ? 160 : 220)
        contentWidth: width
        contentHeight: responseField.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        BTextArea {
            id: responseField
            objectName: "aiReportResponse"
            width: responseScroll.width
            monospace: true
            accessibleName: qsTr("The response")
            onCursorRectangleChanged: dialog.keepCaret(responseScroll, responseField.cursorRectangle)
        }
    }
    BCheckBox {
        id: questionBox
        objectName: "aiReportQuestionBox"
        Layout.fillWidth: true
        visible: questionField.text !== "" || questionBox.checked
        text: dialog.questionLabel
        description: dialog.questionNote
    }
    Flickable {
        id: questionScroll
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(questionField.implicitHeight, 48), 140)
        visible: questionBox.checked
        contentWidth: width
        contentHeight: questionField.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        BTextArea {
            id: questionField
            objectName: "aiReportQuestion"
            width: questionScroll.width
            monospace: true
            accessibleName: qsTr("Your question")
            onCursorRectangleChanged: dialog.keepCaret(questionScroll, questionField.cursorRectangle)
        }
    }
    BText {
        objectName: "aiReportFacts"
        Layout.fillWidth: true
        // Plain text: the model's name comes from the settings or an agent.
        text: qsTr("Model: %1").arg(draft.model) + "\n" + qsTr("App: Brulion %1 · %2").arg(draft.version).arg(draft.device)
        textFormat: Text.PlainText
        variant: "caption"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
    BText {
        objectName: "aiReportCut"
        Layout.fillWidth: true
        visible: draft.cut
        text: qsTr("Too long for the link: the texts are cut, and the report says so.")
        variant: "caption"
        color: Theme.warningText
        wrapMode: Text.Wrap
    }
    BButton {
        objectName: "aiReportPreviewToggle"
        text: dialog.previewShown ? qsTr("Hide the report's text") : qsTr("Show the report's text")
        iconName: dialog.previewShown ? "chevron-down" : "chevron-right"
        variant: "ghost"
        size: "small"
        onClicked: dialog.previewShown = !dialog.previewShown
    }
    Rectangle {
        objectName: "aiReportPreview"
        Layout.fillWidth: true
        visible: dialog.previewShown
        implicitHeight: previewText.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.bgSunken
        border.color: Theme.borderSubtle
        BText {
            id: previewText
            x: Theme.space3
            y: Theme.space3
            width: parent.width - 2 * Theme.space3
            // Plain text: exactly what is sent, the AI's words and the user's never as markup.
            text: dialog.previewShown ? draft.title + "\n\n" + draft.body : ""
            textFormat: Text.PlainText
            font.family: Theme.fontMono
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
    }

    // Where it goes.
    RowLayout {
        objectName: "aiReportDestination"
        Layout.fillWidth: true
        spacing: Theme.space2
        BIcon {
            Layout.alignment: Qt.AlignTop
            name: draft.byEmail ? "at-sign" : "globe"
            size: Theme.iconSize
            color: Theme.fgMuted
        }
        BText {
            Layout.fillWidth: true
            text: draft.byEmail
                  ? qsTr("“Send report” opens an email to %1 in your email app. You send it there; Brulion sends nothing by itself.").arg(draft.destination)
                  : qsTr("“Send report” opens a new issue on GitHub (%1) in your browser. Issues there are public: anyone can read the report. You send it there; Brulion sends nothing by itself.").arg(draft.destination)
            textFormat: Text.PlainText
            variant: "caption"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // Only fold it: nothing is sent.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space2
        visible: dialog.canHide
        spacing: Theme.space3
        BButton {
            objectName: "aiReportHide"
            text: qsTr("Hide this response")
            iconName: "eye-off"
            variant: "secondary"
            onClicked: {
                const number = dialog.number
                dialog.close()
                dialog.hideChosen(number)
            }
        }
        BText {
            Layout.fillWidth: true
            text: qsTr("Nothing is sent; the conversation shows it folded, and you can open it again.")
            variant: "caption"
            color: Theme.fgFaint
            wrapMode: Text.Wrap
        }
    }
}
