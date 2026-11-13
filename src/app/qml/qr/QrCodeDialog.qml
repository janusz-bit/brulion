pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// QrCodeDialog — "QR code" (docs/UI.md "QR code"): the note on screen, or the selected blocks, as
// QR codes another phone's camera reads as text — no network, no account, no app on the other
// side. The codes carry the Markdown as written (a notes app there gets the note whole) or the
// plain text "Copy as plain text" gives, the note's title first or not; both choices are kept on
// the device (settings "qr/format", "qr/title"). A text too long for one code is cut into several
// of about the same size (QrShare, at most QrShare.maxCodes), walked with ‹ › or ← → on the code:
// scanned in turn and joined, they are the note. The code is drawn black on white in any theme,
// as large as the window allows (QrCodeView); its accessible name says which code it is.
//
//   QrCodeDialog { id: qrDialog }
//   qrDialog.openFor(title, markdown, 0)    // a note (title "" for none)
//   qrDialog.openFor(title, markdown, n)    // n selected blocks of the note `title`
BDialog {
    id: dialog
    objectName: "qrCodeDialog"

    /// What the codes are of: the note's title, the blocks selected (0: the whole note).
    property string noteTitle
    property int blockCount: 0
    /// What the codes carry: "markdown" | "plain".
    property string format: "markdown"
    /// The title goes first (where the note has none of its own: QrShare.titleOptional).
    property bool withTitle: true
    /// The codes' text (QrShare), for the shell's self-test.
    readonly property QrShare share: codes

    readonly property list<var> formats: [
        { value: "markdown", text: qsTr("Markdown"), icon: "code" },
        { value: "plain", text: qsTr("Plain text"), icon: "type" }
    ]
    /// The code's side: the dialog's width, less where the window is low (the rest of the dialog
    /// keeps its room above and below it).
    readonly property real codeSide: Math.floor(Math.max(Theme.space16 * 3,
            Math.min(dialog.availableWidth, dialog.roomAboveKeyboard - (dialog.sheet ? 330 : 370))))

    title: qsTr("QR code")
    subtitle: blockCount <= 0 ? noteTitle
              : noteTitle === "" ? qsTr("%n block(s)", "", blockCount)
              : qsTr("%1 — %n block(s)", "", blockCount).arg(noteTitle)
    iconName: "qr-code"
    showReject: false
    acceptText: qsTr("Close")
    preferredWidth: Theme.dialogWidthLg
    initialFocusItem: codeFrame

    function openFor(noteName: string, markdown: string, blocks: int): void {
        dialog.noteTitle = noteName
        dialog.blockCount = blocks
        const stored = "" + AppSettings.value("qr/format", "markdown")
        dialog.format = stored === "plain" ? "plain" : "markdown"
        dialog.withTitle = dialog.flag(AppSettings.value("qr/title", true))
        codes.markdown = markdown
        codes.index = 0
        dialog.open()
    }
    /// QSettings hands booleans back as strings after a restart.
    function flag(value: var): bool {
        return value === true || value === "true"
    }
    function setFormat(value: string): void {
        dialog.format = value
        AppSettings.setValue("qr/format", value)
    }
    function setWithTitle(on: bool): void {
        dialog.withTitle = on
        AppSettings.setValue("qr/title", on)
    }

    // The note's text leaves with the dialog.
    onClosed: codes.markdown = ""

    QrShare {
        id: codes
        host: Workspace
        title: dialog.noteTitle
        plainText: dialog.format === "plain"
        withTitle: dialog.withTitle
    }

    // The code, or why there is none. ← and → walk the codes.
    T.Control {
        id: codeFrame
        objectName: "qrCodeFrame"
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: dialog.codeSide
        Layout.preferredHeight: codes.state === "ok" ? dialog.codeSide : codeMessage.implicitHeight + 2 * Theme.space6
        focusPolicy: Qt.TabFocus
        Accessible.role: Accessible.Graphic
        Accessible.name: codes.count > 1 ? qsTr("QR code %1 of %2").arg(codes.index + 1).arg(codes.count) : qsTr("QR code")
        Keys.onLeftPressed: codes.previous()
        Keys.onRightPressed: codes.next()

        contentItem: Item {
            QrCodeView {
                objectName: "qrCodeView"
                anchors.fill: parent
                visible: codes.state === "ok"
                text: codes.partText
            }
            BText {
                id: codeMessage
                objectName: "qrCodeMessage"
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space4
                visible: codes.state !== "ok"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                variant: "muted"
                text: codes.state === "empty" ? qsTr("Nothing to share: the note is empty.")
                      : qsTr("Too long for QR codes: it would take %n code(s), at most %1 are made. Select a part of the note and share that from the selection's menu.", "", codes.codesNeeded)
                        .arg(codes.maxCodes)
            }
        }
        background: Item {
            BFocusRing {
                visible: codeFrame.visualFocus
                targetRadius: Theme.radiusSm
            }
        }
    }

    // Which code, with the previous and the next one (several codes only).
    RowLayout {
        objectName: "qrCodeSteps"
        visible: codes.count > 1
        Layout.alignment: Qt.AlignHCenter
        spacing: Theme.space2
        BIconButton {
            objectName: "qrCodePrevious"
            iconName: "chevron-left"
            label: qsTr("Previous code")
            shortcut: "Left"
            enabled: codes.index > 0
            onClicked: codes.previous()
        }
        BText {
            objectName: "qrCodeStep"
            text: qsTr("Code %1 of %2").arg(codes.index + 1).arg(codes.count)
            variant: "label"
        }
        BIconButton {
            objectName: "qrCodeNext"
            iconName: "chevron-right"
            label: qsTr("Next code")
            shortcut: "Right"
            enabled: codes.index < codes.count - 1
            onClicked: codes.next()
        }
    }

    BSegmented {
        objectName: "qrCodeFormat"
        Layout.alignment: Qt.AlignHCenter
        visible: codes.state !== "empty"
        accessibleName: qsTr("What the code carries")
        model: dialog.formats
        currentValue: dialog.format
        onActivated: (value) => dialog.setFormat("" + value)
    }
    BCheckBox {
        objectName: "qrCodeTitle"
        Layout.alignment: Qt.AlignHCenter
        visible: codes.titleOptional && codes.state !== "empty"
        text: qsTr("Include the title")
        onToggled: dialog.setWithTitle(checked)
        Binding on checked { value: dialog.withTitle }
    }
    BText {
        objectName: "qrCodeHint"
        Layout.fillWidth: true
        visible: codes.state === "ok"
        variant: "muted"
        font.pixelSize: Theme.textSm
        wrapMode: Text.Wrap
        text: (codes.count > 1 ? qsTr("It does not fit one code: scan the codes in turn, each holds the next part of the text.")
                               : qsTr("Point another phone's camera at the code: the text appears there, no internet needed."))
              + " " + (dialog.format === "plain" ? qsTr("Plain text: as the note shows it, links by their labels.")
                                                 : qsTr("Markdown: as written, for a notes app such as Brulion or Obsidian."))
    }
}
