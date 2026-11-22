pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellPrivacySettings — the "Privacy" section of SettingsView (docs/UI.md "App lock", "Encrypted
// notes"): the app lock (AppLock: a PIN — or on Android the phone's fingerprint, face or screen lock —,
// when it locks, what it hides: the recent apps' preview and screenshots on Android, the note's text
// in notifications, the widgets) and the encrypted notes' session (Workspace.encryption: how long a
// password and an open encrypted page stay unlocked, whether passwords are remembered between pages,
// "Lock now"). Everything here is this device's setting, never synced. Last, the privacy policy
// (docs/PRIVACY.md on the project's GitHub Pages; also in the About dialog).
ColumnLayout {
    id: section

    readonly property bool android: AppController.isAndroid
    readonly property bool browser: AppController.isWasm
    /// The PIN dialog's purpose: "set" (on, or a new PIN), "off" (the lock off), "device" (the
    /// phone's check instead of the PIN).
    property string pinPurpose: "set"
    MapHolder { id: pinResult }

    Layout.fillWidth: true
    spacing: Theme.space2

    function openPin(purpose: string): void {
        section.pinPurpose = purpose
        currentPin.text = ""
        newPin.text = ""
        repeatPin.text = ""
        pinError.text = ""
        pinDialog.open()
    }
    function useDevice(): void {
        pinResult.map = AppLock.useDevice("")
        if (pinResult.map.ok === true)
            AppController.toast(qsTr("Brulion unlocks with the phone's own lock"), "success")
        else
            AppController.toast(AppController.routeText(pinResult.map, "error"), "error")
    }
    function turnOffDevice(): void {
        pinResult.map = AppLock.turnOff("")
        if (pinResult.map.ok === true)
            AppController.toast(qsTr("The app lock is off"), "success")
    }
    /// The PIN dialog's last try was refused (its reason shown) until a field changes.
    property bool pinRejected: false
    function applyPin(): bool {
        if (section.pinPurpose === "set")
            pinResult.map = AppLock.setPin(currentPin.text, newPin.text)
        else if (section.pinPurpose === "device")
            pinResult.map = AppLock.useDevice(currentPin.text)
        else
            pinResult.map = AppLock.turnOff(currentPin.text)
        currentPin.text = ""
        newPin.text = ""
        repeatPin.text = ""
        if (pinResult.map.ok === true) {
            AppController.toast(section.pinPurpose === "off" ? qsTr("The app lock is off")
                                : section.pinPurpose === "device" ? qsTr("Brulion unlocks with the phone's own lock")
                                : qsTr("The PIN is set"), "success")
            return true
        }
        pinError.text = AppController.routeText(pinResult.map, "error")
        return false
    }

    BText {
        Layout.fillWidth: true
        text: qsTr("App lock")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        variant: "muted"
        text: qsTr("Brulion hides your notes behind a lock screen on this device. Anyone with the notebook's folder can still read them: encrypt pages for that.")
    }

    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: lockRows.implicitWidth
        contentHeight: lockRows.implicitHeight

        ColumnLayout {
            id: lockRows
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            ShellSettingRow {
                objectName: "privacyLockRow"
                title: qsTr("App lock")
                description: !AppLock.enabled ? qsTr("Off")
                             : AppLock.method === "device" ? qsTr("On: the phone's fingerprint, face or screen lock")
                             : qsTr("On: a PIN")
                BButton {
                    objectName: "privacySetPin"
                    text: AppLock.method === "pin" && AppLock.enabled ? qsTr("Change PIN…") : qsTr("Set a PIN…")
                    iconName: "lock"
                    onClicked: section.openPin("set")
                }
                BButton {
                    objectName: "privacyLockOff"
                    visible: AppLock.enabled
                    variant: "ghost"
                    text: qsTr("Turn off")
                    onClicked: {
                        if (AppLock.method === "pin")
                            section.openPin("off")
                        else
                            section.turnOffDevice()
                    }
                }
            }
            ShellSettingRow {
                objectName: "privacyDeviceRow"
                visible: section.android
                title: qsTr("Unlock with the phone's lock")
                description: AppLock.deviceAvailable ? qsTr("Your fingerprint, face or the phone's own PIN, pattern or password instead of a PIN for Brulion.")
                                                     : qsTr("Set a screen lock in the phone's settings first (Android 10 or newer).")
                BButton {
                    objectName: "privacyDeviceButton"
                    visible: AppLock.method !== "device"
                    enabled: AppLock.deviceAvailable
                    iconName: "fingerprint"
                    text: qsTr("Use the phone's lock")
                    onClicked: {
                        if (AppLock.hasPin)
                            section.openPin("device")
                        else
                            section.useDevice()
                    }
                }
                BButton {
                    objectName: "privacyPinInstead"
                    visible: AppLock.method === "device"
                    variant: "ghost"
                    text: qsTr("Use a PIN instead…")
                    onClicked: section.openPin("set")
                }
            }
            ShellSettingRow {
                visible: AppLock.enabled
                title: qsTr("Lock when Brulion starts")
                BSwitch {
                    objectName: "privacyLockOnStart"
                    checked: AppLock.lockOnStart
                    onToggled: AppLock.lockOnStart = checked
                }
            }
            ShellSettingRow {
                visible: AppLock.enabled
                title: qsTr("Lock after leaving Brulion")
                description: section.android ? qsTr("After switching to another app or turning the screen off.")
                                             : qsTr("After the window has been in the background, minimised or hidden in the tray.")
                BSegmented {
                    objectName: "privacyBackgroundMinutes"
                    accessibleName: qsTr("Lock after leaving Brulion")
                    model: [{ value: 0, text: qsTr("At once") }, { value: 1, text: qsTr("1 min") },
                            { value: 5, text: qsTr("5 min") }, { value: 15, text: qsTr("15 min") },
                            { value: 60, text: qsTr("1 h") }]
                    currentValue: AppLock.backgroundMinutes
                    onActivated: (value) => AppLock.backgroundMinutes = value
                }
            }
            ShellSettingRow {
                visible: AppLock.enabled && !section.android
                title: qsTr("Lock after no activity")
                description: qsTr("While Brulion is in front, without a key, a click or a touch.")
                BSegmented {
                    objectName: "privacyIdleMinutes"
                    accessibleName: qsTr("Lock after no activity")
                    model: [{ value: 0, text: qsTr("Never") }, { value: 5, text: qsTr("5 min") },
                            { value: 15, text: qsTr("15 min") }, { value: 30, text: qsTr("30 min") },
                            { value: 60, text: qsTr("1 h") }]
                    currentValue: AppLock.idleMinutes
                    onActivated: (value) => AppLock.idleMinutes = value
                }
            }
            ShellSettingRow {
                visible: AppLock.enabled && section.android
                title: qsTr("Hide Brulion's screen")
                description: AppLock.secureWindow === "always"
                             ? qsTr("Always: no screenshots, screen recordings or previews in recent apps.")
                             : qsTr("While locked, and in the recent apps' preview (Android 13 and newer; older Android: choose Always).")
                BSegmented {
                    objectName: "privacySecureWindow"
                    accessibleName: qsTr("Hide Brulion's screen")
                    model: [{ value: "whileLocked", text: qsTr("While locked") }, { value: "always", text: qsTr("Always") }]
                    currentValue: AppLock.secureWindow
                    onActivated: (value) => AppLock.secureWindow = value
                }
            }
            ShellSettingRow {
                visible: AppLock.enabled
                title: qsTr("Hide the note's text in notifications")
                description: qsTr("Reminders say only “Reminder” and the time.")
                BSwitch {
                    objectName: "privacyHideNotifications"
                    checked: AppLock.hideNotificationText
                    onToggled: AppLock.hideNotificationText = checked
                }
            }
            ShellSettingRow {
                visible: AppLock.enabled && section.android
                title: qsTr("Hide notes in widgets")
                description: qsTr("The home screen's widgets say “Brulion is locked”; quick add still works.")
                BSwitch {
                    objectName: "privacyHideWidgets"
                    checked: AppLock.hideWidgets
                    onToggled: AppLock.hideWidgets = checked
                }
            }
            ShellSettingRow {
                visible: AppLock.enabled
                title: qsTr("Lock now")
                BButton {
                    objectName: "privacyLockNow"
                    text: qsTr("Lock now")
                    iconName: "lock-keyhole"
                    onClicked: AppLock.lockNow()
                }
            }
        }
    }

    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space6
        text: qsTr("Encrypted notes")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        variant: "muted"
        text: qsTr("Pages and parts of pages locked with a password, readable in Obsidian too. Encrypt a page from its menu, or type /encrypt.")
    }
    BCard {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space1
        flat: Theme.compact
        padding: Theme.space5
        contentWidth: encRows.implicitWidth
        contentHeight: encRows.implicitHeight

        ColumnLayout {
            id: encRows
            anchors.fill: parent
            spacing: Theme.compact ? Theme.space6 : Theme.space8

            ShellSettingRow {
                title: qsTr("Lock encrypted notes after")
                description: qsTr("Without use: passwords typed are forgotten and open encrypted pages lock.")
                BSegmented {
                    objectName: "privacyEncryptionMinutes"
                    accessibleName: qsTr("Lock encrypted notes after")
                    model: [{ value: 1, text: qsTr("1 min") }, { value: 5, text: qsTr("5 min") },
                            { value: 15, text: qsTr("15 min") }, { value: 60, text: qsTr("1 h") }]
                    currentValue: Workspace.encryption.lockMinutes
                    onActivated: (value) => Workspace.encryption.lockMinutes = value
                }
            }
            ShellSettingRow {
                title: qsTr("Remember passwords between pages")
                description: qsTr("A password typed once opens the notebook's other encrypted text with the same password until they lock.")
                BSwitch {
                    objectName: "privacyRememberPasswords"
                    checked: Workspace.encryption.rememberPasswords
                    onToggled: Workspace.encryption.rememberPasswords = checked
                }
            }
            ShellSettingRow {
                title: qsTr("Lock encrypted notes now")
                description: Workspace.encryption.unlocked ? qsTr("Something is unlocked now.") : qsTr("Nothing is unlocked.")
                BButton {
                    objectName: "privacyLockEncrypted"
                    text: qsTr("Lock")
                    iconName: "lock"
                    enabled: Workspace.encryption.unlocked
                    onClicked: Workspace.encryption.lockAll()
                }
            }
        }
    }

    BText {
        Layout.fillWidth: true
        Layout.topMargin: Theme.space6
        text: qsTr("Privacy policy")
        variant: "heading"
        wrapMode: Text.Wrap
        Accessible.role: Accessible.Heading
    }
    BText {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        variant: "muted"
        text: qsTr("Brulion sends nothing to its makers. Your notes stay on this device and go only where you send them: your sync server, the AI provider you chose, GitHub.")
    }
    BButton {
        objectName: "privacyPolicy"
        Layout.topMargin: Theme.space1
        text: qsTr("Read the privacy policy")
        iconName: "external-link"
        variant: "secondary"
        onClicked: Workspace.openUrl("https://janusz-bit.github.io/brulion/privacy.html")
    }

    BDialog {
        id: pinDialog
        objectName: "privacyPinDialog"
        parent: T.Overlay.overlay
        title: section.pinPurpose === "off" ? qsTr("Turn the app lock off")
               : section.pinPurpose === "device" ? qsTr("Use the phone's lock")
               : AppLock.hasPin ? qsTr("Change PIN") : qsTr("Set a PIN")
        iconName: "lock"
        preferredWidth: Theme.dialogWidthSm
        initialFocusItem: AppLock.hasPin ? currentPin : newPin
        acceptText: section.pinPurpose === "off" ? qsTr("Turn off") : qsTr("OK")
        acceptEnabled: !section.pinRejected && (!AppLock.hasPin || currentPin.typedText !== "")
                       && (section.pinPurpose !== "set" || (AppLock.validPin(newPin.text) && newPin.text === repeatPin.text))
        // Closed when it worked; kept open with the reason when the PIN was wrong (acceptEnabled
        // turns false: the dialog's own accept() does not follow).
        onAcceptRequested: {
            if (section.applyPin())
                pinDialog.close()
            else
                section.pinRejected = true
        }
        onClosed: section.pinRejected = false

        BTextField {
            id: currentPin
            objectName: "privacyCurrentPin"
            visible: AppLock.hasPin
            Layout.fillWidth: true
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            maximumLength: 16
            placeholderText: qsTr("Current PIN")
            onTextChanged: {
                pinError.text = ""
                section.pinRejected = false
            }
        }
        BTextField {
            id: newPin
            objectName: "privacyNewPin"
            visible: section.pinPurpose === "set"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            maximumLength: 16
            placeholderText: qsTr("New PIN (4 to 16 digits)")
            onTextChanged: section.pinRejected = false
            errorText: newPin.text !== "" && !AppLock.validPin(newPin.text) ? qsTr("A PIN is 4 to 16 digits.") : ""
        }
        BTextField {
            id: repeatPin
            objectName: "privacyRepeatPin"
            visible: section.pinPurpose === "set"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            maximumLength: 16
            placeholderText: qsTr("Repeat the PIN")
            onTextChanged: section.pinRejected = false
            errorText: repeatPin.text !== "" && repeatPin.text !== newPin.text ? qsTr("The PINs differ.") : ""
            onAccepted: pinDialog.tryAccept()
        }
        BText {
            Layout.fillWidth: true
            visible: section.pinPurpose === "set"
            wrapMode: Text.Wrap
            variant: "muted"
            text: qsTr("A forgotten PIN can be removed only by clearing Brulion's settings on this device; the notes stay.")
        }
        BText {
            id: pinError
            objectName: "privacyPinError"
            visible: text !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Theme.dangerText
        }
    }
}
