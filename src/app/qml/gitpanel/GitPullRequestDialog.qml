pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// GitPullRequestDialog — the changes panel's "Create PR…": a pull request on GitHub for the
// branch checked out, through the GitHub CLI (`gh pr create`; the branch is pushed first when it
// has no upstream or is ahead of it), into a base branch (GitBasePicker: the default branch unless
// another is chosen); from a fork (an "upstream" remote) into the upstream repository. The result
// comes from `git` as actionFinished("pullRequest", ok, message, url). T3 Code's AI-written title and
// description: "Write with AI" (while the owner says the AI can: aiAvailable) asks git for the
// branch's commits and diff against the base (CodeGit.requestPullRequestQuestion), then the owner
// (writeRequested(system, prompt)) for the text, which comes back by aiWritten() into both fields.
//
//   GitPullRequestDialog { id: prDialog; git: panel.git }   …   prDialog.openFor()
BDialog {
    id: dialog
    objectName: "gitPullRequestDialog"

    property CodeGit git: null
    property string base

    property bool aiAvailable: false
    property bool aiWriting: false
    property string aiError
    signal writeRequested(string system, string prompt)
    function writeWithAi(): void {
        if (dialog.git === null || dialog.aiWriting)
            return
        dialog.aiError = ""
        dialog.aiWriting = true
        dialog.git.requestPullRequestQuestion(dialog.base)
    }
    function aiWritten(ok: bool, text: string): void {
        if (!dialog.aiWriting)
            return
        dialog.aiWriting = false
        if (!ok) {
            dialog.aiError = text
            return
        }
        dialog.written.map = dialog.git.pullRequestText(text)
        const title = "" + dialog.written.map.title
        if (title === "") {
            dialog.aiError = qsTr("The AI gave no title: try again.")
            return
        }
        titleField.text = title
        bodyField.text = "" + dialog.written.map.body
    }
    /// CodeGit.pullRequestText's {title, body} (a map read in compiled QML: MapHolder).
    readonly property MapHolder written: MapHolder {}
    Connections {
        target: dialog.git
        function onPullRequestQuestionReady(system: string, prompt: string, error: string) {
            if (!dialog.aiWriting)
                return
            if (prompt === "") {
                dialog.aiWriting = false
                dialog.aiError = error
                return
            }
            dialog.writeRequested(system, prompt)
        }
    }

    function openFor(): void {
        titleField.text = ""
        bodyField.text = ""
        draftSwitch.checked = false
        dialog.base = ""
        dialog.aiWriting = false
        dialog.aiError = ""
        if (dialog.git !== null)
            dialog.git.branches()
        dialog.open()
    }

    title: qsTr("Create pull request")
    subtitle: dialog.git !== null && dialog.git.branch !== "" ? qsTr("From the branch %1").arg(dialog.git.branch) : ""
    iconName: "git-pull-request"
    acceptText: qsTr("Create PR")
    acceptEnabled: dialog.git !== null && !dialog.git.busy && Js.trim(titleField.typedText) !== ""
    busy: dialog.aiWriting
    initialFocusItem: titleField
    onAccepted: dialog.git.createPullRequestTo(titleField.text, bodyField.text, draftSwitch.checked, dialog.base)
    onClosed: dialog.aiWriting = false

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space2
        BText {
            Layout.fillWidth: true
            text: qsTr("Title")
            variant: "label"
        }
        BSpinner {
            visible: dialog.aiWriting
            size: Theme.iconSizeSm
        }
        BButton {
            objectName: "gitPullRequestWriteAi"
            visible: dialog.aiAvailable
            size: "small"
            variant: "ghost"
            iconName: "sparkles"
            text: dialog.aiWriting ? qsTr("Writing…") : qsTr("Write with AI")
            enabled: !dialog.aiWriting
            onClicked: dialog.writeWithAi()
        }
    }
    BText {
        objectName: "gitPullRequestAiError"
        Layout.fillWidth: true
        visible: dialog.aiError !== ""
        text: dialog.aiError
        textFormat: Text.PlainText
        variant: "caption"
        color: Theme.dangerText
        wrapMode: Text.Wrap
    }
    BTextField {
        id: titleField
        objectName: "gitPullRequestTitle"
        Layout.fillWidth: true
        enabled: !dialog.aiWriting
        accessibleName: qsTr("Pull request title")
        placeholderText: qsTr("What the change does")
        onAccepted: dialog.tryAccept()
    }
    BText {
        Layout.fillWidth: true
        text: qsTr("Description")
        variant: "label"
    }
    BTextArea {
        id: bodyField
        objectName: "gitPullRequestBody"
        Layout.fillWidth: true
        Layout.preferredHeight: 96
        enabled: !dialog.aiWriting
        wrapMode: TextEdit.Wrap
        accessibleName: qsTr("Pull request description")
        placeholderText: qsTr("What and why (optional)…")
    }
    GitBasePicker {
        objectName: "gitPullRequestBase"
        Layout.fillWidth: true
        enabled: !dialog.aiWriting
        git: dialog.git
        base: dialog.base
        onPicked: (name) => dialog.base = name
    }
    BSwitch {
        id: draftSwitch
        objectName: "gitPullRequestDraft"
        Layout.fillWidth: true
        text: qsTr("Draft pull request")
        iconName: "git-pull-request"
    }
    BText {
        Layout.fillWidth: true
        visible: dialog.git !== null && (dialog.git.upstream === "" || dialog.git.ahead > 0)
        text: qsTr("The branch is pushed first: GitHub needs its commits.")
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }
}
