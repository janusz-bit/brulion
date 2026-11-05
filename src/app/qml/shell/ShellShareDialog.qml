pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ShellShareDialog — "Zapisz w Brulionie": text or .md/.txt files another app shared
// (AppController.pendingShare) go to today's journal or to a new page. They come from Android's
// share sheet ("Udostępnij → Brulion", docs/ANDROID.md), a desktop's "Otwórz za pomocą → Brulion"
// (`--open`, docs/WINDOWS.md and docs/NIXOS.md: source "open" — a copy is saved, the file stays as
// it is; a file of the open notebook opens as its page instead), the browser version's share target
// (docs/WEB.md) or `--share`. Text is edited here before it is saved; files are listed with their
// first lines and why one cannot be saved. Text goes to the journal by default, a single file to
// a new page (its title from the file name). Nothing is written before "Save"
// (AppController.saveShare); "Cancel" drops the share and its copies (AppController.dismissShare).
// Shared content is shown as plain text only.
BDialog {
    id: dialog

    readonly property bool hasFiles: AppController.routeText(AppController.pendingShare, "kind") === "files"
    /// [{ name, title, preview, size, error }] (AppController.pendingShare).
    readonly property list<var> files: AppController.routeList(AppController.pendingShare, "files")
    readonly property int readable: AppController.routeInt(AppController.pendingShare, "readable", 0)
    /// Files opened with Brulion on a desktop: copies go into the notebook, the files stay.
    readonly property bool openedWithBrulion: AppController.routeText(AppController.pendingShare, "source") === "open"
    /// "journal" | "page"
    property string target: "journal"
    /// The last save failed (a page of that name exists): changed input tries again.
    property string saveError
    property bool saved: false

    title: qsTr("Save to Brulion")
    subtitle: hasFiles ? (openedWithBrulion ? qsTr("%n file(s) to save in the notebook", "", files.length)
                                 : qsTr("%n file(s) shared", "", files.length))
                       : ""
    iconName: "share"
    acceptText: qsTr("Save")
    // Once saved, pendingShare is empty: the dialog still closes as accepted.
    acceptEnabled: saved || saveError === "" && (hasFiles ? readable > 0 : Js.trim(textField.typedText) !== "")
                   && (target !== "page" || !titleField.visible || Js.trim(titleField.typedText) !== "")
    initialFocusItem: hasFiles ? null : textField

    /// Fill the dialog from pendingShare (on open, and when a newer share replaces the shown one).
    property var _result: ({})
    function errorOf(result: var): string {
        return AppController.routeText(result, "error")
    }
    function reload(): void {
        dialog.saved = false
        dialog.saveError = ""
        dialog.target = AppController.routeText(AppController.pendingShare, "target") === "page" ? "page" : "journal"
        titleField.text = AppController.routeText(AppController.pendingShare, "title")
        textField.text = AppController.routeText(AppController.pendingShare, "text")
    }

    onAboutToShow: reload()
    onAcceptRequested: {
        // (Kept in a property: a call's result read after another call is not compiled.)
        dialog._result = AppController.saveShare({ target: dialog.target, title: Js.trim(titleField.text),
                                                   text: textField.text })
        const error = dialog.errorOf(dialog._result)
        if (AppController.routeFlag(dialog._result, "ok"))
            dialog.saved = true
        else
            dialog.saveError = error !== "" ? error : qsTr("Nothing to save.")
    }
    onRejected: {
        if (!saved)
            AppController.dismissShare()
    }

    BSegmented {
        id: targetChoice
        objectName: "shareTarget"
        Layout.fillWidth: true
        fill: true
        accessibleName: qsTr("Save to")
        model: [{ value: "journal", text: qsTr("Today's journal"), icon: "calendar" },
                { value: "page", text: dialog.hasFiles && dialog.readable > 1 ? qsTr("New pages") : qsTr("New page"),
                  icon: "square-pen" }]
        currentValue: dialog.target
        onActivated: (value) => {
            dialog.target = value
            dialog.saveError = ""
        }
    }

    BTextField {
        id: titleField
        objectName: "shareTitle"
        Layout.fillWidth: true
        // One page gets a title here; several files are named after their files.
        visible: dialog.target === "page" && (!dialog.hasFiles || dialog.readable === 1)
        placeholderText: qsTr("Page title")
        leadingIcon: "file-text"
        onTextEdited: dialog.saveError = ""
        onAccepted: dialog.tryAccept()
    }

    BTextArea {
        id: textField
        objectName: "shareText"
        visible: !dialog.hasFiles
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.touchUi ? 160 : 140
        placeholderText: qsTr("Shared text")
        wrapMode: TextEdit.Wrap
        onTextChanged: dialog.saveError = ""
    }

    Repeater {
        // With text: the files that could not be saved (a link's preview image, say).
        model: dialog.files
        delegate: ColumnLayout {
            id: fileRow
            // (The rows of AppController.pendingShare's files.)
            required property string name
            required property string preview
            required property string error
            Layout.fillWidth: true
            spacing: Theme.space1

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon {
                    name: fileRow.error !== "" ? "circle-alert" : "file-text"
                    color: fileRow.error !== "" ? Theme.dangerText : Theme.fgMuted
                }
                BText {
                    Layout.fillWidth: true
                    text: fileRow.name
                    textFormat: Text.PlainText
                    elide: Text.ElideMiddle
                    variant: "label"
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
            BText {
                Layout.fillWidth: true
                visible: text !== ""
                text: fileRow.error !== "" ? fileRow.error : fileRow.preview
                textFormat: Text.PlainText
                variant: "faint"
                color: fileRow.error !== "" ? Theme.dangerText : Theme.fgFaint
                font.pixelSize: Theme.textSm
                wrapMode: Text.Wrap
                maximumLineCount: 6
                elide: Text.ElideRight
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }

    BText {
        objectName: "shareCopyHint"
        Layout.fillWidth: true
        visible: dialog.openedWithBrulion
        text: qsTr("Brulion saves a copy in the notebook. The file itself stays where it is, unchanged.")
        variant: "faint"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }

    BText {
        Layout.fillWidth: true
        text: dialog.target === "journal"
              ? (dialog.hasFiles && dialog.readable > 1 ? qsTr("Each file becomes one block at the end of today's journal.")
                                                       : qsTr("Goes to the end of today's journal."))
              : qsTr("New pages go to the notebook's folder for new pages.")
        variant: "faint"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
    }

    BText {
        objectName: "shareError"
        Layout.fillWidth: true
        visible: dialog.saveError !== ""
        text: dialog.saveError
        textFormat: Text.PlainText
        color: Theme.dangerText
        wrapMode: Text.Wrap
        Accessible.role: Accessible.AlertMessage
        Accessible.name: text
    }
}
