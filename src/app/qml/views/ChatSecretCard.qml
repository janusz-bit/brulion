pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// ChatSecretCard — a secret an AI agent asks for in its conversation (docs/UI.md "AI chat" → "A
// secret the agent asks for"; C++: Workspace.chats.secrets, shell/chatsecrets.h): who asks, the
// label and the reason as plain text (the agent's words, never HTML), a sign-in or setup page's
// button when it gave one, a masked field with Show and Paste, Send and Cancel. The value goes only
// to C++ (send()): never into the page, the field's draft or a toast; the remote control never reads
// the field, shown or not (`secretText`). The field is emptied when the card goes.
Rectangle {
    id: card

    /// The conversation shown.
    required property string path
    readonly property ChatSecrets secrets: Workspace.chats.secrets
    readonly property int serial: card.secrets.serial
    readonly property bool waiting: card.path !== "" && card.secrets.pending(card.path, card.serial)
    readonly property var noRequest: ({})
    readonly property var request: card.waiting ? card.secrets.card(card.path, card.serial) : card.noRequest
    readonly property string agent: AppController.routeText(card.request, "agent")
    readonly property string label: AppController.routeText(card.request, "label")
    readonly property string reason: AppController.routeText(card.request, "reason")
    readonly property string placeholder: AppController.routeText(card.request, "placeholder")
    readonly property string url: AppController.routeText(card.request, "url")
    readonly property string host: AppController.routeText(card.request, "host")
    readonly property bool inlineValue: AppController.routeFlag(card.request, "inline")
    /// Show: the value in plain sight (for the user only).
    property bool reveal: false

    function forget(): void {
        secretField.clear()
        card.reveal = false
    }
    function send(): void {
        const value = secretField.text
        if (value === "")
            return
        const path = card.path
        card.forget()
        card.secrets.send(path, value)
    }
    function cancel(): void {
        const path = card.path
        card.forget()
        card.secrets.cancel(path)
    }

    objectName: "chatSecret"
    visible: card.waiting
    implicitHeight: secretColumn.implicitHeight + 2 * Theme.space3
    radius: Theme.radiusLg
    color: Theme.bgElevated
    border.color: Theme.accent
    onWaitingChanged: {
        card.forget()
        if (card.waiting && !Theme.touchUi)
            secretField.forceActiveFocus()
    }
    onPathChanged: card.forget()

    ColumnLayout {
        id: secretColumn
        x: Theme.space3
        y: Theme.space3
        width: parent.width - 2 * Theme.space3
        spacing: Theme.space2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            BIcon {
                Layout.alignment: Qt.AlignTop
                name: "key-round"
                size: Theme.iconSize
                color: Theme.accent
            }
            BText {
                objectName: "chatSecretTitle"
                Layout.fillWidth: true
                text: qsTr("%1 asks for a secret: %2").arg(card.agent).arg(card.label)
                textFormat: Text.PlainText
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
        }
        BText {
            objectName: "chatSecretReason"
            Layout.fillWidth: true
            visible: card.reason !== ""
            text: card.reason
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            maximumLineCount: 20
            elide: Text.ElideRight
        }
        BButton {
            objectName: "chatSecretOpen"
            visible: card.url !== ""
            size: "small"
            variant: "secondary"
            iconName: "external-link"
            text: qsTr("Open %1").arg(card.host)
            onClicked: Workspace.openUrl(card.url)
        }
        BTextField {
            id: secretField
            // (The remote control never reads it, also while Show shows it: remotecontrol.cpp.)
            property bool secretText: true
            objectName: "chatSecretField"
            Layout.fillWidth: true
            leadingIcon: "lock"
            echoMode: card.reveal ? TextInput.Normal : TextInput.Password
            placeholderText: card.placeholder !== "" ? card.placeholder : qsTr("Paste or type it here")
            accessibleName: card.label
            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
            onAccepted: card.send()
            trailingItem: BIconButton {
                objectName: "chatSecretShow"
                iconName: card.reveal ? "eye-off" : "eye"
                label: card.reveal ? qsTr("Hide") : qsTr("Show")
                size: "small"
                onClicked: card.reveal = !card.reveal
            }
        }
        BText {
            objectName: "chatSecretNote"
            Layout.fillWidth: true
            text: card.inlineValue
                  ? qsTr("The agent asked for the value itself: it goes into its context and its AI provider's logs. Send it only if you trust them with it.")
                  : qsTr("It goes to the agent as a file only you can read, deleted when this answer ends (15 minutes at most) — never into the conversation.")
            color: card.inlineValue ? Theme.warningText : Theme.fgMuted
            variant: "caption"
            wrapMode: Text.Wrap
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "chatSecretSend"
                variant: "primary"
                iconName: "arrow-up"
                text: qsTr("Send")
                enabled: secretField.length > 0
                onClicked: card.send()
            }
            BButton {
                objectName: "chatSecretPaste"
                variant: "secondary"
                iconName: "clipboard-paste"
                text: qsTr("Paste")
                onClicked: {
                    secretField.selectAll()
                    secretField.paste()
                }
            }
            BButton {
                objectName: "chatSecretCancel"
                variant: "ghost"
                text: qsTr("Cancel")
                onClicked: card.cancel()
            }
        }
    }
}
