pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// EncryptedTextDialog — an encrypted fragment of a page (docs/UI.md "Encrypted notes";
// Workspace.encryption): the lock asks for the password (the ones remembered are tried first, the
// hint shown), then shows the decrypted text to read, edit and save encrypted again — under the
// same password, hint and look; an old format becomes Meld Encrypt's v3 —, to copy (marked
// sensitive), to decrypt permanently (the plaintext written where the fragment was) or to put
// under a new password. "new" mode encrypts a selection or new text ("Encrypt selection", "Insert
// encrypted text"): the text, the password twice, a public hint, and whether Obsidian's reading view
// shows a 🔐 for it (a visible fragment) or nothing (an Obsidian comment, the default).
//
// The decrypted text lives in this dialog's field only: closing it — or the encrypted notes locking
// (the timeout, the app lock) — empties the field and forgets the fragment's password. The editor
// writes the results (fragmentSaved / fragmentCreated): the block's text changes as one undo step.
//
//   EncryptedTextDialog { onFragmentSaved: (fragment, text) => … ; onFragmentCreated: (fragment) => … }
//   dialog.openFragment(path, fragmentText)     dialog.openNew(path, selectedText, fromSelection)
BDialog {
    id: dialog
    objectName: "encryptedTextDialog"

    /// "unlock" | "edit" | "password" (a new password for the open fragment) | "new".
    property string mode: "unlock"
    /// The page the fragment is in (the remembered password is per page).
    property string path
    /// The fragment's text as written in the page (the one to replace).
    property string fragment
    property string hint
    property bool oldFormat: false
    /// The open fragment (Workspace.encryption's session), 0 none.
    property int session: 0
    /// The request whose answer this dialog waits for (-1 none).
    property int request: -1
    property bool working: false
    property string error
    /// The decrypted text as opened (Save is offered once it changed).
    property string original
    /// "new": the text is a selection that the fragment replaces (else it is inserted).
    property bool fromSelection: false

    /// The fragment `fragment` of the page is to become `text` (its new encryption, or with
    /// `decrypted` its plaintext).
    signal fragmentSaved(string fragment, string text, bool decrypted)
    /// "new": the new fragment's text, to put where the selection was or at the caret.
    signal fragmentCreated(string text)

    function openFragment(pagePath: string, fragmentText: string): void {
        dialog.clearState()
        dialog.path = pagePath
        dialog.fragment = fragmentText
        info.map = Workspace.encryption.fragmentInfo(fragmentText)
        dialog.hint = AppController.routeText(info.map, "hint")
        dialog.oldFormat = AppController.routeFlag(info.map, "old")
        dialog.mode = "unlock"
        dialog.open()
        // The passwords remembered first: the right one opens it at once, without a question.
        dialog.working = true
        dialog.request = Workspace.encryption.openFragment(pagePath, fragmentText, "", true)
    }
    function openNew(pagePath: string, text: string, selection: bool): void {
        dialog.clearState()
        dialog.path = pagePath
        dialog.fromSelection = selection
        dialog.mode = "new"
        editor.text = text
        dialog.open()
    }

    // (Not reset(): Dialog has a signal of that name, which a function cannot override.)
    function clearState(): void {
        if (dialog.session > 0)
            Workspace.encryption.closeFragment(dialog.session)
        dialog.session = 0
        dialog.request = -1
        dialog.working = false
        dialog.error = ""
        dialog.original = ""
        dialog.fragment = ""
        dialog.hint = ""
        dialog.oldFormat = false
        // (The decrypted text and the passwords leave the fields.)
        editor.text = ""
        passwordField.text = ""
        confirmField.text = ""
        hintField.text = ""
        visibleSwitch.checked = false
    }

    MapHolder { id: info }

    readonly property bool passwordsMatch: passwordField.text === confirmField.text
    readonly property bool needsNewPassword: dialog.mode === "new" || dialog.mode === "password"

    title: dialog.mode === "new" ? qsTr("Encrypt text")
           : dialog.mode === "password" ? qsTr("Change password")
           : dialog.mode === "edit" ? qsTr("Encrypted text") : qsTr("Unlock encrypted text")
    iconName: dialog.mode === "edit" ? "lock-open" : "lock"
    busy: dialog.working
    rejectText: dialog.mode === "edit" ? qsTr("Close") : qsTr("Cancel")
    preferredWidth: Theme.dialogWidth
    initialFocusItem: dialog.mode === "edit" || dialog.mode === "new" ? editor : passwordField
    acceptText: dialog.mode === "unlock" ? qsTr("Unlock")
                : dialog.mode === "edit" ? qsTr("Save")
                : dialog.mode === "password" ? qsTr("Change password") : qsTr("Encrypt")
    acceptEnabled: !dialog.working
                   && (dialog.mode === "unlock" ? passwordField.typedText !== ""
                       : dialog.mode === "edit" ? (editor.text !== dialog.original || dialog.oldFormat)
                       : passwordField.typedText !== "" && dialog.passwordsMatch
                         && (dialog.mode !== "new" || editor.text !== "")
                         && Workspace.encryption.validHint(hintField.text))

    // The work is asynchronous (a key derivation): the dialog stays open until it answered.
    onAcceptRequested: dialog.start()
    onClosed: dialog.clearState()

    function start(): void {
        if (dialog.working)
            return
        dialog.error = ""
        if (dialog.mode === "unlock") {
            dialog.working = true
            dialog.request = Workspace.encryption.openFragment(dialog.path, dialog.fragment, passwordField.text, true)
            passwordField.text = ""
        } else if (dialog.mode === "edit") {
            dialog.working = true
            dialog.request = Workspace.encryption.sealFragment(dialog.session, editor.text)
        } else if (dialog.mode === "password") {
            dialog.working = true
            dialog.request = Workspace.encryption.changeFragmentPassword(dialog.session, editor.text, passwordField.text,
                                                                          hintField.text)
        } else {
            dialog.working = true
            dialog.request = Workspace.encryption.encryptFragment(dialog.path, editor.text, passwordField.text,
                                                                   hintField.text, !visibleSwitch.checked, true)
        }
    }

    Connections {
        target: Workspace.encryption
        function onFragmentOpened(id: int, session: int, text: string, error: string) {
            if (id !== dialog.request)
                return
            dialog.request = -1
            dialog.working = false
            if (session <= 0) {
                // "" — only the remembered passwords did not fit: the question, no error.
                dialog.error = error
                dialog.mode = "unlock"
                passwordField.forceActiveFocus()
                return
            }
            dialog.session = session
            dialog.original = text
            editor.text = text
            dialog.mode = "edit"
            editor.forceActiveFocus()
        }
        function onTextReady(id: int, text: string, error: string) {
            if (id !== dialog.request)
                return
            dialog.request = -1
            dialog.working = false
            if (error !== "" || text === "") {
                dialog.error = error !== "" ? error : qsTr("The encrypted text could not be saved.")
                return
            }
            if (dialog.mode === "new")
                dialog.fragmentCreated(text)
            else
                dialog.fragmentSaved(dialog.fragment, text, false)
            dialog.close()
        }
        // Everything locked (the timeout, the app lock): the text goes with it.
        function onAllLocked() {
            if (dialog.opened && dialog.mode !== "new")
                dialog.close()
        }
    }

    /// "Decrypt permanently": the plaintext where the fragment was (after a confirmation).
    function decryptPermanently(): void {
        dialog.fragmentSaved(dialog.fragment, editor.text, true)
        dialog.close()
    }
    function copyText(): void {
        if (Workspace.encryption.copySecret(editor.text))
            AppController.toast(qsTr("Copied. The text is not encrypted on the clipboard."), "info")
    }

    footerLeading: [
        BButton {
            visible: dialog.mode === "edit"
            objectName: "encryptedCopy"
            variant: "ghost"
            iconName: "copy"
            text: Theme.compact ? "" : qsTr("Copy")
            Accessible.name: qsTr("Copy")
            onClicked: dialog.copyText()
        },
        BIconButton {
            id: moreButton
            visible: dialog.mode === "edit"
            objectName: "encryptedMore"
            iconName: "ellipsis"
            label: qsTr("More")
            onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
        }
    ]

    BMenu {
        id: moreMenu
        BMenuItem {
            text: qsTr("Change password…")
            iconName: "key-round"
            onTriggered: {
                passwordField.text = ""
                confirmField.text = ""
                hintField.text = dialog.hint
                dialog.mode = "password"
                passwordField.forceActiveFocus()
            }
        }
        BMenuItem {
            text: qsTr("Decrypt permanently…")
            iconName: "lock-open"
            onTriggered: decryptConfirm.open()
        }
    }

    BConfirmDialog {
        id: decryptConfirm
        title: qsTr("Decrypt permanently?")
        message: qsTr("The text is written into the page unencrypted, where everyone who can read the notebook — other devices, sync servers, backups — can read it.")
        confirmText: qsTr("Decrypt")
        destructive: true
        onAccepted: dialog.decryptPermanently()
    }

    // ---- unlock ------------------------------------------------------------------------------
    BText {
        visible: dialog.mode === "unlock"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: dialog.working && dialog.request >= 0 && passwordField.text === ""
              ? qsTr("Unlocking…")
              : qsTr("This text is encrypted. Enter its password to read or change it.")
    }
    BText {
        visible: dialog.mode === "unlock" && dialog.hint !== ""
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        variant: "muted"
        // (A hint is the note's text: plain text, never markup.)
        textFormat: Text.PlainText
        text: qsTr("Hint: %1").arg(dialog.hint)
    }

    // ---- the text (edit, new) ----------------------------------------------------------------
    BText {
        visible: dialog.mode === "edit" && dialog.oldFormat
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        variant: "muted"
        text: qsTr("Written in an older format: saving writes the current one, which Meld Encrypt 3 reads too.")
    }
    Flickable {
        id: textScroll
        visible: dialog.mode === "edit" || dialog.mode === "new"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(Math.max(editor.implicitHeight, 96), Theme.compact ? 200 : 320)
        contentWidth: width
        contentHeight: editor.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        BTextArea {
            id: editor
            objectName: "encryptedTextField"
            width: textScroll.width
            wrapMode: TextEdit.Wrap
            placeholderText: qsTr("Text to encrypt")
            // No suggestions or learned words from the keyboard for a secret.
            inputMethodHints: Qt.ImhNoPredictiveText
            onTextChanged: Workspace.encryption.touch()
            onCursorRectangleChanged: {
                if (cursorRectangle.y < textScroll.contentY)
                    textScroll.contentY = cursorRectangle.y
                else if (cursorRectangle.y + cursorRectangle.height > textScroll.contentY + textScroll.height)
                    textScroll.contentY = cursorRectangle.y + cursorRectangle.height - textScroll.height
            }
        }
    }

    // ---- passwords (unlock, password, new) ---------------------------------------------------
    BTextField {
        id: passwordField
        objectName: "encryptedPasswordField"
        visible: dialog.mode !== "edit"
        Layout.fillWidth: true
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: dialog.needsNewPassword ? qsTr("New password") : qsTr("Password")
        leadingIcon: "key-round"
        EnterKey.type: dialog.needsNewPassword ? Qt.EnterKeyNext : Qt.EnterKeyDone
        onAccepted: {
            if (dialog.needsNewPassword)
                confirmField.forceActiveFocus()
            else
                dialog.tryAccept()
        }
    }
    BTextField {
        id: confirmField
        objectName: "encryptedConfirmField"
        visible: dialog.needsNewPassword
        Layout.fillWidth: true
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
        placeholderText: qsTr("Repeat the password")
        leadingIcon: "key-round"
        errorText: confirmField.text !== "" && !dialog.passwordsMatch ? qsTr("The passwords differ.") : ""
        onAccepted: hintField.forceActiveFocus()
    }
    BTextField {
        id: hintField
        objectName: "encryptedHintField"
        visible: dialog.needsNewPassword
        Layout.fillWidth: true
        placeholderText: qsTr("Hint (optional, readable by anyone)")
        leadingIcon: "info"
        errorText: Workspace.encryption.validHint(hintField.text) ? "" : qsTr("A hint cannot contain 🔐, 💡 or a line break.")
        onAccepted: dialog.tryAccept()
    }
    BSwitch {
        id: visibleSwitch
        objectName: "encryptedVisibleSwitch"
        visible: dialog.mode === "new"
        Layout.fillWidth: true
        text: qsTr("Show a lock in Obsidian's reading view")
        description: qsTr("Off: Obsidian shows nothing there (an Obsidian comment). Brulion shows the lock either way.")
    }
    BText {
        visible: dialog.needsNewPassword
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        variant: "muted"
        text: qsTr("There is no way to recover a forgotten password. Obsidian's Meld Encrypt plugin opens the text with the same password.")
    }

    BText {
        visible: dialog.error !== ""
        objectName: "encryptedError"
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.dangerText
        text: dialog.error
    }
}
