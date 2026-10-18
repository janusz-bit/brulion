pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Brulion

// NextcloudForm — the Nextcloud account (NextcloudSetup over AppSettings): on/off, server address,
// user, app password (how to create one, a button to Nextcloud's security page), server folder
// (default "Notatki"), how often to sync, this device's name; "Testuj połączenie" explains what
// went wrong (and offers to create a missing folder), "Zapisz" applies everything at once. Used by
// SyncView and the settings' sync section.
//
//   NextcloudForm { Layout.fillWidth: true }
IoCard {
    id: form

    readonly property NextcloudSetup setup: setupObject
    property bool showPassword: false

    iconName: "server"
    title: qsTr("Nextcloud account")
    subtitle: qsTr("Your notes sync with a folder on your own Nextcloud server (WebDAV). Every device that signs in to it gets the same notebook.")
    contentSpacing: Theme.space4

    NextcloudSetup {
        id: setupObject
        settings: AppSettings
    }

    /// A labelled form field.
    component Field: ColumnLayout {
        id: field
        property string label
        property string help
        default property alias control: slot.data
        Layout.fillWidth: true
        spacing: Theme.space1
        BText {
            Layout.fillWidth: true
            text: field.label
            variant: "label"
            wrapMode: Text.Wrap
        }
        ColumnLayout {
            id: slot
            Layout.fillWidth: true
            spacing: Theme.space1
        }
        BText {
            Layout.fillWidth: true
            visible: field.help !== ""
            text: field.help
            variant: "small"
            color: Theme.fgMuted
            wrapMode: Text.Wrap
        }
    }

    // In the browser the server must allow this page (CORS).
    Rectangle {
        Layout.fillWidth: true
        visible: form.setup.browser
        implicitHeight: corsColumn.implicitHeight + 2 * Theme.space3
        radius: Theme.radius
        color: Theme.infoBg
        ColumnLayout {
            id: corsColumn
            anchors.fill: parent
            anchors.margins: Theme.space3
            spacing: Theme.space1
            BText {
                Layout.fillWidth: true
                text: qsTr("In the browser")
                variant: "label"
            }
            BText {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                variant: "small"
                text: qsTr("A web page may only talk to a server that allows it (CORS). Easiest: open Brulion from the same address as your Nextcloud (on NixOS: services.brulion-web), or use the app for Linux or Android.")
            }
        }
    }

    BSwitch {
        Layout.fillWidth: true
        text: qsTr("Sync with Nextcloud")
        description: qsTr("Off: nothing is sent; the notes stay on this device.")
        iconName: "cloud"
        checked: form.setup.enabled
        onToggled: form.setup.enabled = checked
    }

    GridLayout {
        Layout.fillWidth: true
        columns: Theme.compact ? 1 : 2
        columnSpacing: Theme.space4
        rowSpacing: Theme.space4

        Field {
            Layout.columnSpan: Theme.compact ? 1 : 2
            label: qsTr("Server address")
            help: form.setup.urlWarning !== "" ? form.setup.urlWarning
                                               : qsTr("The address you open Nextcloud with in a browser.")
            BTextField {
                id: urlField
                Layout.fillWidth: true
                text: form.setup.url
                placeholderText: qsTr("https://cloud.example.com")
                accessibleName: qsTr("Server address")
                leadingIcon: "server"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.type: Qt.EnterKeyNext // on to the user name (touch keyboards)
                errorText: form.setup.urlError
                onTextEdited: {
                    form.setup.url = text
                    form.setup.clearTestResult()
                }
            }
        }
        Field {
            label: qsTr("User name")
            BTextField {
                Layout.fillWidth: true
                text: form.setup.user
                placeholderText: qsTr("e.g. jan")
                accessibleName: qsTr("User name")
                leadingIcon: "at-sign"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.type: Qt.EnterKeyNext // on to the password
                errorText: form.setup.userError
                onTextEdited: {
                    form.setup.user = text
                    form.setup.clearTestResult()
                }
            }
        }
        Field {
            //: Polish: "Hasło aplikacji" (Nextcloud's own name for it)
            label: qsTr("App password")
            BTextField {
                id: passwordField
                Layout.fillWidth: true
                text: form.setup.password
                echoMode: form.showPassword ? TextInput.Normal : TextInput.Password
                placeholderText: form.setup.storedPasswordApplies ? qsTr("Saved – type to replace") : qsTr("xxxxx-xxxxx-xxxxx-xxxxx-xxxxx")
                accessibleName: qsTr("App password")
                leadingIcon: "lock"
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhHiddenText
                errorText: form.setup.passwordError
                trailingItem: BIconButton {
                    iconName: form.showPassword ? "eye-off" : "eye"
                    label: form.showPassword ? qsTr("Hide the password") : qsTr("Show the password")
                    size: "small"
                    onClicked: form.showPassword = !form.showPassword
                }
                onTextEdited: {
                    form.setup.password = text
                    form.setup.clearTestResult()
                }
            }
        }
        // How to get an app password.
        Rectangle {
            Layout.columnSpan: Theme.compact ? 1 : 2
            Layout.fillWidth: true
            implicitHeight: helpColumn.implicitHeight + 2 * Theme.space3
            radius: Theme.radius
            color: Theme.bgSunken
            ColumnLayout {
                id: helpColumn
                anchors.fill: parent
                anchors.margins: Theme.space3
                spacing: Theme.space2
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space2
                    BIcon { Layout.alignment: Qt.AlignTop; name: "info"; size: Theme.iconSize; color: Theme.infoText }
                    BText {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        variant: "small"
                        text: qsTr("Use an app password, not your account password: in Nextcloud open Settings → Security, type a name such as “Brulion – phone” under “Devices & sessions” and click “Create new app password”. You can revoke it there at any time.")
                    }
                }
                BButton {
                    visible: form.setup.securityUrl !== ""
                    Layout.leftMargin: Theme.iconSize + Theme.space2
                    text: qsTr("Open the security settings")
                    iconName: "external-link"
                    size: "small"
                    variant: "ghost"
                    onClicked: Qt.openUrlExternally(form.setup.securityUrl)
                }
            }
        }
        Field {
            label: qsTr("Folder on the server")
            help: form.setup.davUrl !== "" ? form.setup.davUrl : qsTr("Created by the first sync when it does not exist.")
            BTextField {
                Layout.fillWidth: true
                text: form.setup.folder
                placeholderText: "Notatki"
                accessibleName: qsTr("Folder on the server")
                leadingIcon: "folder"
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                errorText: form.setup.folderError
                onTextEdited: {
                    form.setup.folder = text
                    form.setup.clearTestResult()
                }
            }
        }
        Field {
            label: qsTr("Sync")
            help: qsTr("Also right after your edits and when the connection comes back.")
            BButton {
                id: intervalButton
                Layout.fillWidth: true
                iconName: "chevron-down"
                iconRight: true
                text: {
                    const options = form.setup.intervalOptions
                    for (let i = 0; i < options.length; ++i)
                        if (options[i].value === form.setup.interval)
                            return options[i].text
                    return qsTr("Every %n minute(s)", "", form.setup.interval)
                }
                Accessible.name: qsTr("How often to sync: %1").arg(text)
                onClicked: intervalMenu.popup(intervalButton, 0, intervalButton.height)
            }
        }
        Field {
            Layout.columnSpan: Theme.compact ? 1 : 2
            label: qsTr("Name of this device")
            help: qsTr("Appears in the names of conflict copies, so you know where a version came from.")
            BTextField {
                Layout.fillWidth: true
                Layout.maximumWidth: Theme.compact ? -1 : 360
                text: form.setup.deviceName
                placeholderText: qsTr("e.g. Laptop or Phone")
                accessibleName: qsTr("Name of this device")
                leadingIcon: AppController.isAndroid ? "smartphone" : "laptop"
                onTextEdited: form.setup.deviceName = text
            }
        }
    }

    BMenu {
        id: intervalMenu
    }
    Instantiator {
        model: form.setup.intervalOptions
        delegate: BMenuItem {
            required property var modelData
            text: modelData.text
            checkable: true
            checked: form.setup.interval === modelData.value
            onTriggered: form.setup.interval = modelData.value
        }
        onObjectAdded: (index, object) => intervalMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => intervalMenu.removeItem(object)
    }

    // ---- Test result ---------------------------------------------------------------------------
    BCard {
        id: result
        readonly property var r: form.setup.testResult
        Layout.fillWidth: true
        visible: r.title !== undefined
        tone: r.ok ? "success" : r.code === "no-folder" ? "warning" : "danger"
        padding: Theme.space3
        contentItem: ColumnLayout {
            spacing: Theme.space2
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space2
                BIcon {
                    Layout.alignment: Qt.AlignTop
                    name: result.r.ok ? "circle-check" : result.r.code === "no-folder" ? "folder" : "circle-alert"
                    size: Theme.iconSize
                    color: result.r.ok ? Theme.successText : result.r.code === "no-folder" ? Theme.warningText : Theme.dangerText
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    BText {
                        Layout.fillWidth: true
                        text: result.r.title || ""
                        variant: "label"
                        wrapMode: Text.Wrap
                    }
                    BText {
                        Layout.fillWidth: true
                        text: result.r.message || ""
                        wrapMode: Text.Wrap
                    }
                    BText {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: result.r.hint || ""
                        variant: "small"
                        color: Theme.fgMuted
                        wrapMode: Text.Wrap
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space2
                visible: createButton.visible || securityButton.visible || detailsButton.visible
                BButton {
                    id: createButton
                    visible: !!result.r.canCreateFolder
                    text: qsTr("Create the folder")
                    iconName: "plus"
                    size: "small"
                    variant: "primary"
                    busy: form.setup.testing
                    onClicked: form.setup.createFolder()
                }
                BButton {
                    id: securityButton
                    visible: !!result.r.securityUrl
                    text: qsTr("Create an app password")
                    iconName: "external-link"
                    size: "small"
                    onClicked: Qt.openUrlExternally(result.r.securityUrl)
                }
                BButton {
                    id: detailsButton
                    property bool open: false
                    visible: !!result.r.details
                    text: qsTr("Technical details")
                    iconName: open ? "chevron-down" : "chevron-right"
                    size: "small"
                    variant: "ghost"
                    onClicked: open = !open
                }
            }
            BText {
                Layout.fillWidth: true
                visible: detailsButton.open && detailsButton.visible
                text: result.r.details || ""
                variant: "mono"
                color: Theme.fgMuted
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
        }
    }

    // ---- Actions ------------------------------------------------------------------------------
    Flow {
        Layout.fillWidth: true
        spacing: Theme.space2
        BButton {
            //: Polish: "Testuj połączenie"
            text: qsTr("Test connection")
            iconName: "network"
            busy: form.setup.testing
            onClicked: form.setup.testConnection()
        }
        BButton {
            text: qsTr("Save")
            iconName: "check"
            variant: "primary"
            enabled: form.setup.dirty
            onClicked: {
                if (form.setup.save())
                    AppController.toast(form.setup.enabled ? qsTr("Saved. Syncing starts in a moment.") : qsTr("Saved. Nextcloud sync is off."), "success")
            }
        }
        BButton {
            visible: form.setup.dirty
            text: qsTr("Discard changes")
            variant: "ghost"
            onClicked: form.setup.reload()
        }
        BButton {
            visible: !form.setup.dirty && form.setup.hasStoredPassword
            text: qsTr("Sign out")
            iconName: "x"
            variant: "ghost"
            onClicked: signOut.open()
        }
    }

    BConfirmDialog {
        id: signOut
        title: qsTr("Sign out of Nextcloud?")
        message: qsTr("Syncing stops and this device forgets the app password. The notes stay here and on the server.")
        confirmText: qsTr("Sign out")
        destructive: true
        onAccepted: form.setup.disconnectAccount()
    }
}
