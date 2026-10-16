pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import Brulion

// ReminderSoundSetting — the sound of ⏰ reminders or ⏰! alarms in Settings → Reminders, over the
// ReminderSounds singleton (src/app/notify/remindersounds.h):
//
//   * the choice: Default (the system's sound) / Ringtone / Vibrate (Android) / Silent — only
//     what this platform offers (ReminderSounds.modes);
//   * "Choose ringtone…": Android opens the system's ringtone picker; the desktop a menu of
//     sound-theme sounds and "Sound file…" (FileDialog). "Ringtone" without a ringtone chosen
//     yet does the same instead of selecting nothing;
//   * "Play sample" (Android: the sound for three seconds; desktop: a sample notification).
//
// Laid out like ShellSettingRow: the text on the left, the choice and the buttons on the right;
// on compact windows everything below each other, the choice as wide as the card.
//
//   ReminderSoundSetting { kind: "alarm"; title: qsTr("Alarm sound") }
GridLayout {
    id: setting

    /// "reminder" | "alarm"
    required property string kind
    property string title

    readonly property var choice: kind === "alarm" ? ReminderSounds.alarm : ReminderSounds.reminder
    readonly property string mode: choice.mode || "default"
    readonly property bool hasCustom: (choice.uri || "") !== ""
    readonly property bool offersCustom: ReminderSounds.modes.indexOf("custom") >= 0

    function modeLabel(m) {
        switch (m) {
        case "custom": return qsTr("Ringtone")
        case "vibrate": return qsTr("Vibrate")
        case "silent": return qsTr("Silent")
        default: return qsTr("Default")
        }
    }
    function themeSoundLabel(name) {
        switch (name) {
        case "message-new-instant": return qsTr("New message")
        case "bell": return qsTr("Bell")
        case "complete": return qsTr("Completed")
        case "alarm-clock-elapsed": return qsTr("Alarm clock")
        case "phone-incoming-call": return qsTr("Incoming call")
        default: return name
        }
    }
    /// The shown name of the custom sound: the ringtone's title or file name, else the theme sound's.
    function customName() {
        return choice.title ? choice.title : themeSoundLabel(choice.uri || "")
    }
    readonly property string summary: {
        switch (mode) {
        case "custom":
            return qsTr("Ringtone: %1").arg(customName())
        case "vibrate":
            return qsTr("Vibration only, no sound")
        case "silent":
            return AppController.isAndroid ? qsTr("No sound and no vibration") : qsTr("No sound")
        default:
            if (kind === "alarm")
                return AppController.isAndroid ? qsTr("The phone's alarm sound")
                     : AppController.isWasm ? qsTr("A beeping tone while Brulion is open in the browser")
                     : qsTr("Alarm clock, from the desktop's sound theme")
            return AppController.isAndroid ? qsTr("The phone's notification sound")
                 : AppController.isWasm ? qsTr("The browser's notification sound")
                 : qsTr("As the desktop's notifications sound")
        }
    }

    function selectMode(m) {
        // "Ringtone" with none chosen yet: choose one first (the selection stays meanwhile).
        if (m === "custom" && !hasCustom)
            chooseSound()
        else
            ReminderSounds.setMode(kind, m)
    }
    function chooseSound() {
        if (ReminderSounds.systemPicker)
            ReminderSounds.pick(kind)
        else
            soundMenu.popup(pickButton, 0, pickButton.height)
    }

    objectName: kind + "SoundSetting"
    Layout.fillWidth: true
    columns: Theme.compact ? 1 : 2
    columnSpacing: Theme.space4
    rowSpacing: Theme.space2

    ColumnLayout {
        Layout.row: 0
        Layout.column: 0
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        spacing: 2
        BText {
            Layout.fillWidth: true
            text: setting.title
            variant: "label"
            wrapMode: Text.Wrap
        }
        BText {
            objectName: setting.kind + "SoundSummary"
            Layout.fillWidth: true
            text: setting.summary
            variant: "muted"
            font.pixelSize: Theme.textSm
            wrapMode: Text.Wrap
        }
    }

    BSegmented {
        id: modeControl
        objectName: setting.kind + "SoundMode"
        Layout.row: Theme.compact ? 1 : 0
        Layout.column: Theme.compact ? 0 : 1
        Layout.alignment: Theme.compact ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
        Layout.fillWidth: Theme.compact
        fill: Theme.compact
        accessibleName: setting.title
        model: ReminderSounds.modes.map(m => ({ value: m, text: setting.modeLabel(m) }))
        currentValue: setting.mode
        onActivated: (value) => {
            setting.selectMode(value)
            // A choice that did not happen (no ringtone yet, the picker open): BSegmented took
            // the value itself — follow the setting again.
            Qt.callLater(() => modeControl.currentValue = Qt.binding(() => setting.mode))
        }
    }

    Flow {
        Layout.row: Theme.compact ? 2 : 1
        Layout.column: Theme.compact ? 0 : 1
        Layout.alignment: Theme.compact ? Qt.AlignLeft : Qt.AlignRight
        Layout.fillWidth: Theme.compact
        spacing: Theme.space2
        BButton {
            id: pickButton
            objectName: setting.kind + "SoundPick"
            visible: setting.offersCustom
            size: "small"
            iconName: "music"
            text: qsTr("Choose ringtone…")
            onClicked: setting.chooseSound()
        }
        BButton {
            objectName: setting.kind + "SoundPreview"
            size: "small"
            variant: "ghost"
            iconName: "play"
            text: qsTr("Play sample")
            enabled: setting.mode !== "silent"
            onClicked: ReminderSounds.preview(setting.kind)
        }
    }

    // Desktop: the sound-theme sounds and a sound file (Android uses the system's picker).
    BMenu {
        id: soundMenu
        objectName: setting.kind + "SoundMenu"
        BMenuSeparator {}
        BMenuItem {
            objectName: setting.kind + "SoundFile"
            enabled: ReminderSounds.soundFiles
            iconName: "file"
            text: qsTr("Sound file…")
            onTriggered: soundFileDialog.open()
        }
    }
    Instantiator {
        model: ReminderSounds.themeSounds
        delegate: BMenuItem {
            id: themeItem
            required property string modelData
            objectName: setting.kind + "ThemeSound_" + modelData
            text: setting.themeSoundLabel(modelData)
            checkable: true
            checked: setting.mode === "custom" && setting.choice.uri === modelData
            onTriggered: {
                ReminderSounds.setCustom(setting.kind, modelData, "")
                // A click toggled `checked`: follow the setting again.
                checked = Qt.binding(() => setting.mode === "custom" && setting.choice.uri === themeItem.modelData)
            }
        }
        onObjectAdded: (index, object) => soundMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => soundMenu.removeItem(object)
    }
    FileDialog {
        id: soundFileDialog
        title: qsTr("Choose a sound file")
        nameFilters: [qsTr("Sound files (*.oga *.ogg *.wav)"), qsTr("All files (*)")]
        onAccepted: ReminderSounds.setCustom(setting.kind, selectedFile.toString(), "")
    }
}
