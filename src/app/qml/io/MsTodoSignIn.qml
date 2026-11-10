pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// MsTodoSignIn — the "Sign in to Microsoft" block of the Microsoft To Do card (ImportSourcePicker):
// the app registration's Application (client) ID, the authority under "More", the sign-in button;
// then the device code in large monospace with "Copy code", the address to open (text and a
// button), a spinner, the time left and "Cancel"; while downloading the progress. When the lists
// are in, the ImportController shows its preview. In the browser only a hint: Microsoft does not
// let web pages use this sign-in, so there the user picks a file saved from Graph Explorer.
//
//   MsTodoSignIn { Layout.fillWidth: true; importer: importController }
ColumnLayout {
    id: block

    required property ImportController importer
    readonly property MsTodoImport signIn: signInObject
    property bool moreOpen: false
    /// Signing in works outside the browser only (see the hint below).
    readonly property bool canSignIn: !AppController.isWasm
    readonly property bool waiting: signIn.state === "waiting"

    component Hint: BText {
        Layout.fillWidth: true
        variant: "small"
        color: Theme.fgMuted
        wrapMode: Text.Wrap
    }

    spacing: Theme.space3

    MsTodoImport {
        id: signInObject
        objectName: "msTodoImport"
        settings: AppSettings
        importer: block.importer
    }

    Hint {
        visible: AppController.isWasm
        text: qsTr("In the browser version choose a file saved from Graph Explorer – signing in to Microsoft works in the app for Linux, Windows and Android.")
    }

    // ---- The app registration and the button --------------------------------------------------
    ColumnLayout {
        Layout.fillWidth: true
        visible: block.canSignIn && !block.signIn.busy
        spacing: Theme.space2

        BText {
            Layout.fillWidth: true
            text: qsTr("Application (client) ID")
            variant: "label"
        }
        BTextField {
            objectName: "msTodoClientId"
            Layout.fillWidth: true
            text: block.signIn.clientId
            placeholderText: qsTr("e.g. 3f2504e0-4f89-41d3-9a0c-0305e82c3301")
            accessibleName: qsTr("Application (client) ID")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            errorText: block.signIn.clientIdError
            onTextEdited: block.signIn.clientId = text
            onAccepted: block.signIn.signIn()
        }
        Hint { text: qsTr("From your own free app registration in Microsoft Entra (the guide “Import i eksport” explains it step by step). Brulion remembers it on this device; nothing is stored about your account.") }

        BButton {
            text: block.moreOpen ? qsTr("Less") : qsTr("More")
            iconName: block.moreOpen ? "chevron-down" : "chevron-right"
            size: "small"
            variant: "ghost"
            onClicked: block.moreOpen = !block.moreOpen
        }
        ColumnLayout {
            Layout.fillWidth: true
            visible: block.moreOpen
            spacing: Theme.space1
            BText {
                Layout.fillWidth: true
                text: qsTr("Authority")
                variant: "label"
            }
            BTextField {
                objectName: "msTodoAuthority"
                Layout.fillWidth: true
                text: block.signIn.authority
                placeholderText: block.signIn.defaultAuthority
                accessibleName: qsTr("Authority")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                errorText: block.signIn.authorityError
                onTextEdited: block.signIn.authority = text
            }
            Hint { text: qsTr("consumers – personal accounts (outlook.com, hotmail.com); organizations – work or school accounts; common – both (the app must allow both); or your directory's tenant ID or domain.") }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.space2
            BButton {
                objectName: "msTodoSignIn"
                text: qsTr("Sign in to Microsoft")
                iconName: "lock"
                variant: "primary"
                onClicked: block.signIn.signIn()
            }
        }
    }

    // ---- Waiting for the code / for the user / downloading ------------------------------------
    Rectangle {
        Layout.fillWidth: true
        visible: block.canSignIn && block.signIn.busy
        implicitHeight: waitColumn.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.bgSunken

        ColumnLayout {
            id: waitColumn
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2

            BText {
                Layout.fillWidth: true
                visible: block.waiting
                text: qsTr("Open %1 on this or any other device, enter the code and sign in with the account that has your tasks:").arg(block.signIn.verificationText)
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.fillWidth: true
                visible: block.waiting
                spacing: Theme.space3
                BText {
                    objectName: "msTodoUserCode"
                    text: block.signIn.userCode
                    variant: "mono"
                    font.pixelSize: Theme.text2xl
                    font.weight: Theme.weightSemiBold
                    font.letterSpacing: 2
                    lineHeightMode: Text.ProportionalHeight
                    lineHeight: 1
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Sign-in code: %1").arg(block.signIn.userCode)
                }
                BButton {
                    text: qsTr("Copy code")
                    iconName: "copy"
                    size: "small"
                    onClicked: {
                        if (block.signIn.copyCode())
                            AppController.toast(qsTr("Code copied"), "success")
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                visible: block.waiting
                spacing: Theme.space2
                BButton {
                    text: qsTr("Open %1").arg(block.signIn.verificationText)
                    iconName: "external-link"
                    size: "small"
                    variant: "secondary"
                    onClicked: Qt.openUrlExternally(block.signIn.verificationUrl)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BSpinner { size: Theme.iconSizeSm; running: block.signIn.busy }
                Hint {
                    objectName: "msTodoProgress"
                    text: block.signIn.progressText
                }
            }
            Hint {
                visible: block.waiting && block.signIn.secondsLeft > 0
                text: qsTr("The code expires in %n minute(s).", "", Math.ceil(block.signIn.secondsLeft / 60))
            }
            BButton {
                objectName: "msTodoCancel"
                text: qsTr("Cancel")
                size: "small"
                variant: "ghost"
                onClicked: block.signIn.cancel()
            }
        }
    }

    // ---- What went wrong ---------------------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        visible: block.signIn.state === "error"
        implicitHeight: errorRow.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.dangerBg

        RowLayout {
            id: errorRow
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space2
            BIcon { Layout.alignment: Qt.AlignTop; name: "circle-alert"; size: Theme.iconSize; color: Theme.dangerText }
            BText {
                objectName: "msTodoError"
                Layout.fillWidth: true
                text: block.signIn.errorText
                wrapMode: Text.Wrap
            }
        }
    }

    Hint {
        visible: block.canSignIn
        text: qsTr("Or choose a JSON file saved from Graph Explorer:")
    }
}
