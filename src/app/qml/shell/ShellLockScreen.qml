pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// ShellLockScreen — the app lock's screen (docs/UI.md "App lock"; AppLock): a modal popup over the
// whole window, above every other popup and the toasts, while AppLock.locked — Main.qml also hides
// the stage under it, so nothing of the notebook is drawn, read by a screen reader or reached with
// the keyboard. The PIN: a keypad of large keys on touch screens (the digits typed on a keyboard
// too), a field on the desktop; after five wrong PINs the keys wait (the time left counts down).
// The phone's own check (method "device"): asked as the screen opens and with its button; "Use the
// PIN" is not offered then (the lock has none).
T.Popup {
    id: lock
    objectName: "lockScreen"

    parent: T.Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    z: 3000
    modal: true
    dim: false
    focus: true
    closePolicy: T.Popup.NoAutoClose
    padding: 0

    /// The digits typed so far (never shown, only counted).
    property string entered
    property string error
    property bool deviceMethod: AppLock.method === "device"
    /// Seconds before the next PIN may be tried (AppLock.retrySeconds, counted down here).
    property int wait: 0

    function typeDigit(digit: string): void {
        if (AppLock.checking || lock.wait > 0 || lock.entered.length >= 16)
            return
        lock.error = ""
        lock.entered = lock.entered + digit
    }
    function backspace(): void {
        if (lock.entered.length > 0)
            lock.entered = Js.slice(lock.entered, 0, lock.entered.length - 1)
    }
    function submit(): void {
        if (AppLock.checking || lock.wait > 0)
            return
        const pin = pinField.visible ? pinField.text : lock.entered
        if (!Js.test(pin, "^[0-9]{4,16}$", "")) {
            lock.error = qsTr("A PIN is 4 to 16 digits.")
            return
        }
        AppLock.unlock(pin)
        lock.entered = ""
        pinField.text = ""
    }
    function askDevice(): void {
        lock.error = ""
        AppLock.unlockWithDevice()
    }

    onOpened: {
        lock.entered = ""
        lock.error = ""
        lock.wait = AppLock.retrySeconds
        if (lock.deviceMethod)
            deviceLater.restart()
        else if (pinField.visible)
            pinField.forceActiveFocus()
        else
            content.forceActiveFocus()
    }
    Later {
        id: deviceLater
        onTriggered: if (lock.opened) lock.askDevice()
    }

    Connections {
        target: AppLock
        function onUnlockFinished(ok: bool, error: string) {
            lock.error = ok ? "" : error
            lock.wait = AppLock.retrySeconds
            if (!ok && pinField.visible)
                pinField.forceActiveFocus()
            if (ok && error !== "")
                AppController.toast(error, "warning")
        }
    }
    Timer {
        interval: 1000
        repeat: true
        running: lock.opened && lock.wait > 0
        onTriggered: lock.wait = AppLock.retrySeconds
    }

    background: Rectangle {
        color: Theme.bg
        // Taps anywhere stay on the lock screen.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: (wheel) => wheel.accepted = true
        }
    }

    contentItem: Item {
        id: content
        focus: true
        // Digits from a keyboard on the keypad too (a phone with a keyboard, the desktop's keypad).
        Keys.onPressed: (event) => {
            const key = event.key
            if (key >= Qt.Key_0 && key <= Qt.Key_9) {
                lock.typeDigit("" + (key - Qt.Key_0))
                event.accepted = true
            } else if (key === Qt.Key_Backspace) {
                lock.backspace()
                event.accepted = true
            } else if (key === Qt.Key_Return || key === Qt.Key_Enter) {
                lock.submit()
                event.accepted = true
            } else if (key === Qt.Key_Back || key === Qt.Key_Escape) {
                // Back leaves the app (Android), never past the lock.
                event.accepted = true
                if (AppController.isAndroid)
                    AppController.moveToBackground()
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 2 * Theme.space6, 360)
            spacing: Theme.space4

            BIcon {
                Layout.alignment: Qt.AlignHCenter
                name: "lock-keyhole"
                size: 44
                color: Theme.accent
            }
            BText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                variant: "title"
                text: qsTr("Brulion is locked")
            }
            BText {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                variant: "muted"
                text: lock.deviceMethod ? qsTr("Unlock with your fingerprint, face or the phone's screen lock.")
                                        : qsTr("Enter your PIN.")
            }

            // The PIN's dots (touch keypad): how many digits, never which.
            Row {
                visible: !lock.deviceMethod && !pinField.visible
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.space3
                height: 16
                Repeater {
                    model: Math.max(4, lock.entered.length)
                    delegate: Rectangle {
                        required property int index
                        width: 12
                        height: 12
                        radius: 6
                        color: index < lock.entered.length ? Theme.accent : "transparent"
                        border.color: Theme.fgMuted
                        border.width: 1.5
                    }
                }
            }

            BTextField {
                id: pinField
                objectName: "lockPinField"
                // A keyboard and a pointer (the desktop, the browser): a field; touch screens: the keypad.
                visible: !lock.deviceMethod && !Theme.touchUi
                Layout.fillWidth: true
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                maximumLength: 16
                placeholderText: qsTr("PIN")
                horizontalAlignment: TextInput.AlignHCenter
                enabled: lock.wait === 0 && !AppLock.checking
                onAccepted: lock.submit()
            }

            GridLayout {
                id: keypad
                objectName: "lockKeypad"
                visible: !lock.deviceMethod && Theme.touchUi
                Layout.alignment: Qt.AlignHCenter
                columns: 3
                rowSpacing: Theme.space3
                columnSpacing: Theme.space5
                readonly property list<string> keys: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "back", "0", "ok"]
                Repeater {
                    model: keypad.keys
                    delegate: T.AbstractButton {
                        id: key
                        required property string modelData
                        objectName: "lockKey_" + key.modelData
                        implicitWidth: 64
                        implicitHeight: 64
                        enabled: lock.wait === 0 && !AppLock.checking
                        Accessible.name: key.modelData === "back" ? qsTr("Delete digit")
                                         : key.modelData === "ok" ? qsTr("Unlock") : key.modelData
                        background: Rectangle {
                            radius: width / 2
                            color: key.modelData === "ok" ? (key.pressed ? Theme.primaryPressed : Theme.primary)
                                                          : (key.pressed ? Theme.bgPressed : Theme.bgSunken)
                            opacity: key.enabled ? 1 : 0.5
                        }
                        contentItem: Item {
                            BText {
                                anchors.centerIn: parent
                                visible: key.modelData !== "back" && key.modelData !== "ok"
                                text: key.modelData
                                font.pixelSize: 24
                            }
                            BIcon {
                                anchors.centerIn: parent
                                visible: key.modelData === "back" || key.modelData === "ok"
                                name: key.modelData === "back" ? "delete" : "lock-open"
                                size: 24
                                color: key.modelData === "ok" ? Theme.primaryFg : Theme.fg
                            }
                        }
                        onClicked: {
                            if (key.modelData === "back")
                                lock.backspace()
                            else if (key.modelData === "ok")
                                lock.submit()
                            else
                                lock.typeDigit(key.modelData)
                        }
                    }
                }
            }

            BButton {
                objectName: "lockUnlock"
                visible: lock.deviceMethod || pinField.visible
                Layout.alignment: Qt.AlignHCenter
                variant: "primary"
                iconName: lock.deviceMethod ? "fingerprint" : "lock-open"
                text: qsTr("Unlock")
                busy: AppLock.checking
                enabled: lock.wait === 0 && !AppLock.checking
                onClicked: lock.deviceMethod ? lock.askDevice() : lock.submit()
            }

            BText {
                objectName: "lockError"
                visible: lock.error !== "" || lock.wait > 0
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.dangerText
                text: lock.wait > 0 ? qsTr("Too many wrong PINs. Try again in %n second(s).", "", lock.wait) : lock.error
            }
        }
    }
}
